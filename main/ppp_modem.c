#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/uart.h"

#include "netif/ppp/pppapi.h"
#include "netif/ppp/pppos.h"

#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"

#include "ppp_modem.h"

static const char *TAG = "SIM7670_PPP_UART";

#define MODEM_UART_NUM UART_NUM_1

// PPP state
static ppp_pcb *ppp = NULL;
static struct netif ppp_netif;

TaskHandle_t modem_task_handle = NULL;  // to signal redial from PPP callback
static bool ppp_test_started = false;   // ensure we run the test only once per boot
static int ppp_link_up_count = 0;       // count PPPERR_NONE events

// Forward declarations
static void ppp_connectivity_test_task(void *arg);
static void ppp_uart_rx_task(void *arg);

// PPP output callback: lwIP PPP -> modem UART
static u32_t ppp_output_cb(ppp_pcb *pcb, const void *data, u32_t len, void *ctx)
{
    (void)pcb;
    (void)ctx;

    int written = uart_write_bytes(MODEM_UART_NUM, (const char *)data, len);
    if (written < 0) {
        ESP_LOGE(TAG, "PPP output: uart_write_bytes error: %d", written);
        written = 0;
    }
    return (u32_t)written;
}

static void ppp_connectivity_test_task(void *arg)
{
    ESP_LOGI(TAG, "PPP test task: waiting 5 seconds before connectivity check...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Use a well-known IPv4 address (no DNS needed). Let's try Cloudflare DNS: 1.1.1.1:80
    const char *test_ip_str = "1.1.1.1";
    uint16_t test_port = 80;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "PPP test: socket() failed, errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(test_port);
    dest.sin_addr.s_addr = inet_addr(test_ip_str);

    ESP_LOGI(TAG, "PPP test: connecting to %s:%u ...", test_ip_str, (unsigned)test_port);

    int ret = connect(sock, (struct sockaddr *)&dest, sizeof(dest));
    if (ret == 0) {
        ESP_LOGI(TAG, "PPP test: connect() SUCCESS to %s:%u", test_ip_str, (unsigned)test_port);
    } else {
        ESP_LOGE(TAG, "PPP test: connect() FAILED, errno=%d", errno);
    }

    close(sock);
    ESP_LOGI(TAG, "PPP test task finished.");
    vTaskDelete(NULL);
}

// PPP link-status callback
static void ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    (void)pcb;
    (void)ctx;

    switch (err_code) {
    case PPPERR_NONE: {
        ESP_LOGI(TAG, "PPP status: LINK UP (PPPERR_NONE)");

        const ip4_addr_t *ip4  = &ppp_netif.ip_addr.u_addr.ip4;
        const ip4_addr_t *gw4  = &ppp_netif.gw.u_addr.ip4;
        const ip4_addr_t *msk4 = &ppp_netif.netmask.u_addr.ip4;

        ESP_LOGI(TAG, "PPP (ppp_netif) IP  : %s", ip4addr_ntoa(ip4));
        ESP_LOGI(TAG, "PPP (ppp_netif) GW  : %s", ip4addr_ntoa(gw4));
        ESP_LOGI(TAG, "PPP (ppp_netif) MASK: %s", ip4addr_ntoa(msk4));

        extern struct netif *netif_default;
        ESP_LOGI(TAG, "netif_default ptr = %p, ppp_netif ptr = %p",
                 (void *)netif_default, (void *)&ppp_netif);

        if (netif_default != NULL) {
            const ip4_addr_t *ip4_2  = &netif_default->ip_addr.u_addr.ip4;
            const ip4_addr_t *gw4_2  = &netif_default->gw.u_addr.ip4;
            const ip4_addr_t *msk4_2 = &netif_default->netmask.u_addr.ip4;

            ESP_LOGI(TAG, "PPP (netif_default) IP  : %s", ip4addr_ntoa(ip4_2));
            ESP_LOGI(TAG, "PPP (netif_default) GW  : %s", ip4addr_ntoa(gw4_2));
            ESP_LOGI(TAG, "PPP (netif_default) MASK: %s", ip4addr_ntoa(msk4_2));
        }

        ppp_link_up_count++;
        ESP_LOGI(TAG, "PPP status: LINK UP count = %d", ppp_link_up_count);

        if (!ppp_test_started && ppp_link_up_count >= 2) {
            ppp_test_started = true;

            const char *ip_str = ipaddr_ntoa(&ppp_netif.ip_addr);
            ESP_LOGI(TAG,
                     "PPP status: starting connectivity test task after %d LINK UPs (current IP string = %s).",
                     ppp_link_up_count,
                     ip_str ? ip_str : "(null)");

            xTaskCreate(ppp_connectivity_test_task,
                        "ppp_test",
                        4096,
                        NULL,
                        5,
                        NULL);
        }
        break;
    }

    case PPPERR_CONNECT:
        ESP_LOGE(TAG, "PPP status: PPPERR_CONNECT (connection lost or failed)");

        if (modem_task_handle != NULL) {
            ESP_LOGI(TAG, "Signaling modem_task to redial PPP...");
            xTaskNotifyGive(modem_task_handle);
        } else {
            ESP_LOGW(TAG, "modem_task_handle is NULL, cannot signal redial");
        }
        break;

    default:
        ESP_LOGE(TAG, "PPP status error: %d", err_code);
        break;
    }
}

// Create and start the PPP stack after the modem has returned CONNECT.
void start_ppp_after_connect(void)
{
    if (ppp != NULL) {
        ESP_LOGW(TAG, "PPP already created, skipping");
        return;
    }

    ESP_LOGI(TAG, "Creating PPP control block (PPPoS)...");
    memset(&ppp_netif, 0, sizeof(ppp_netif));

    ppp = pppapi_pppos_create(&ppp_netif, ppp_output_cb, ppp_status_cb, NULL);
    if (ppp == NULL) {
        ESP_LOGE(TAG, "pppapi_pppos_create failed");
        return;
    }

    ESP_LOGI(TAG, "Starting PPP negotiation...");
    pppapi_set_default(ppp);
    pppapi_connect(ppp, 0);

    // Start UART->PPP RX bridge task
    xTaskCreate(ppp_uart_rx_task, "ppp_uart_rx_task",
                4096, NULL, 6, NULL);
}

// UART -> PPP receiver task (unchanged)
static void ppp_uart_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[256];

    ESP_LOGI(TAG, "PPP UART RX task started");

    while (1) {
        int len = uart_read_bytes(MODEM_UART_NUM, buf, sizeof(buf),
                                  pdMS_TO_TICKS(1000));
        if (len > 0) {
            ESP_LOGI(TAG, "PPP RX: got %d bytes from UART", len);

            if (ppp != NULL) {
                pppos_input_tcpip(ppp, buf, len);
            } else {
                ESP_LOGW(TAG, "PPP not ready yet, dropping %d bytes", len);
            }
        }
    }
}

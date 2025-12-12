#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "netif/ppp/pppapi.h" // NEW: PPP API
#include "netif/ppp/pppos.h"  // NEW: PPP-over-serial
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"   // NEW: for socket(), connect(), etc.
#include "lwip/inet.h"      // NEW: for inet_addr (optional but handy)
#include "lwip/ip4_addr.h"


static const char *TAG = "SIM7670_PPP_UART";

/*
 * Adjust these to your board wiring.
 * For now, we assume UART1 is wired to the SIM7670 module.
 */
#define MODEM_UART_NUM        UART_NUM_1
#define MODEM_TX_GPIO         GPIO_NUM_18   // TX pin to modem
#define MODEM_RX_GPIO         GPIO_NUM_17   // RX pin from modem
#define MODEM_BAUD_RATE       115200
#define MODEM_UART_BUF_SIZE   (4096)



//*PPP NEW: PPP globals 
static ppp_pcb *ppp = NULL;
static struct netif ppp_netif;
static TaskHandle_t modem_task_handle = NULL;  // to signal redial from PPP callback
static bool ppp_test_started = false;          // ensure we run the test only once per boot
static int ppp_link_up_count = 0;              // count PPPERR_NONE events

// Forward declaration
static void ppp_connectivity_test_task(void *arg);

static void ppp_uart_rx_task(void *arg);

// NEW: PPP output callback: PPP -> UART 
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
    dest.sin_addr.s_addr = inet_addr(test_ip_str);  // convert "1.1.1.1" to binary

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

// NEW: PPP status callback (minimal for now) 
static void ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    switch (err_code) {
    case PPPERR_NONE: {
        ESP_LOGI(TAG, "PPP status: LINK UP (PPPERR_NONE)");

        // Use explicit IPv4 view of the netif addresses
        const ip4_addr_t *ip4  = &ppp_netif.ip_addr.u_addr.ip4;
        const ip4_addr_t *gw4  = &ppp_netif.gw.u_addr.ip4;
        const ip4_addr_t *msk4 = &ppp_netif.netmask.u_addr.ip4;

        ESP_LOGI(TAG, "PPP (ppp_netif) IP  : %s", ip4addr_ntoa(ip4));
        ESP_LOGI(TAG, "PPP (ppp_netif) GW  : %s", ip4addr_ntoa(gw4));
        ESP_LOGI(TAG, "PPP (ppp_netif) MASK: %s", ip4addr_ntoa(msk4));

        // ALSO log whatever netif_default points to, in case PPP attached there
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

        // ---- LINK UP counter + connectivity-test trigger ----
        ppp_link_up_count++;
        ESP_LOGI(TAG, "PPP status: LINK UP count = %d", ppp_link_up_count);

        // Start the connectivity test after the 2nd LINK UP, once
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

// NEW: Called once after we get CONNECT from the modem 
static void start_ppp_after_connect(void)
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

    ESP_LOGI(TAG, "Starting PPP negotiation (no UART RX bridge yet)...");
    pppapi_set_default(ppp);
    pppapi_connect(ppp, 0);  // 0 = no blocking timeout

	// NEW: Start UART->PPP RX bridge task
    xTaskCreate(ppp_uart_rx_task, "ppp_uart_rx_task",
                4096, NULL, 6, NULL);
}

// NEW: UART -> PPP receiver task 
static void ppp_uart_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[256];

    ESP_LOGI(TAG, "PPP UART RX task started");

    while (1) {
        int len = uart_read_bytes(MODEM_UART_NUM, buf, sizeof(buf),
                                  pdMS_TO_TICKS(1000));
         if (len > 0) {
            ESP_LOGI(TAG, "PPP RX: got %d bytes from UART", len);  // NEW

            if (ppp != NULL) {
                pppos_input_tcpip(ppp, buf, len);
            } else {
                ESP_LOGW(TAG, "PPP not ready yet, dropping %d bytes", len);
            }
        }
    }
}
//*/

/*
 * Simple helper: send an AT command and log all responses for a short time.
 */
static void send_at_command(const char *cmd)
{
    // Send command
    size_t len = strlen(cmd);
    ESP_LOGI(TAG, "Sending: %s", cmd);
    uart_write_bytes(MODEM_UART_NUM, cmd, len);

    // Read response for ~500 ms
    uint8_t buf[256];
    int total = 0;
    int64_t start = esp_timer_get_time(); // microseconds

    while ((esp_timer_get_time() - start) < 500000) {
        int n = uart_read_bytes(MODEM_UART_NUM, buf, sizeof(buf),
                                pdMS_TO_TICKS(50));
        if (n > 0) {
            total += n;
            // Not null-terminated, so copy into temp
            char tmp[257];
            int copy_len = (n < 256) ? n : 256;
            memcpy(tmp, buf, copy_len);
            tmp[copy_len] = '\0';
            ESP_LOGI(TAG, "RX: %s", tmp);
        }
    }

    ESP_LOGI(TAG, "Total bytes received for this command: %d", total);
}

//*PPP
static void test_ppp_dial(void)
{
    ESP_LOGI(TAG, "Starting PPP DIAL test: ATD*99#");

    // This command often returns CONNECT quickly, then PPP binary data starts
    send_at_command("ATD*99#\r\n");  // allow up to ~10s

    ESP_LOGI(TAG, "PPP DIAL command sent, check above for CONNECT.");
}
//*/	

static void modem_task(void *arg)
{
    // Remember our task handle so PPP callback can wake us for redial
    modem_task_handle = xTaskGetCurrentTaskHandle();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // wait a bit after boot / between redials

        // Basic sanity check
        send_at_command("AT\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // PIN status
        send_at_command("AT+CPIN?\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Signal quality
        send_at_command("AT+CSQ\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Registration
        send_at_command("AT+CREG?\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // PDP context
        send_at_command("AT+CGDCONT?\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Modem AT test sequence finished. Moving to PPP DIAL…");

        ESP_LOGI(TAG, "Starting PPP DIAL test: ATD*99#");
        test_ppp_dial();

        ESP_LOGI(TAG, "PPP DIAL command sent. Now in PPP data mode, creating PPP stack...");
        start_ppp_after_connect();

        ESP_LOGI(TAG, "Modem task: waiting for PPP disconnect / redial request...");
        // Block here until PPP callback notifies us about disconnect
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGW(TAG, "Modem task: got redial signal from PPP, restarting dial loop...");
        // Loop repeats, re-running AT+... and ATD*99#
    }
}

static void init_uart_for_modem(void)
{
    const uart_config_t uart_config = {
        .baud_rate = MODEM_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART_NUM,
                                        MODEM_UART_BUF_SIZE,
                                        MODEM_UART_BUF_SIZE,
                                        0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART_NUM,
                                 MODEM_TX_GPIO,
                                 MODEM_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART for modem initialized: UART%d, TX=%d, RX=%d, %d baud",
             MODEM_UART_NUM, MODEM_TX_GPIO, MODEM_RX_GPIO, MODEM_BAUD_RATE);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SIM7670 PPP UART demo (Stage 2: minimal PPP init)");

	    // Raise log level so PPP/lwIP debug messages are visible
    esp_log_level_set("*", ESP_LOG_DEBUG);


    /* NEW: initialize networking stack base (needed by lwIP/PPP) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize UART to talk to the SIM7670
    init_uart_for_modem();

    // Start modem task
    xTaskCreate(modem_task, "modem_task", 4096, NULL, 5, NULL);
}

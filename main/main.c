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
#include "driver/gpio.h"

#include "sim7670_ppp.h"

static const char *TAG = "SIM7670_PPP_UART";

/*
 * Waveshare ESP32-S3-SIM7670G-4G tested UART configuration.
 * The Waveshare ESP32-S3-SIM7670G-4G board uses UART1
 * with TX on GPIO18 and RX on GPIO17 for this implementation.
 */
#define MODEM_UART_NUM        UART_NUM_1
#define MODEM_TX_GPIO         GPIO_NUM_18   // TX pin to modem
#define MODEM_RX_GPIO         GPIO_NUM_17   // RX pin from modem
#define MODEM_BAUD_RATE       115200
#define MODEM_UART_BUF_SIZE   (4096)

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

    ESP_LOGI(TAG, "Waveshare ESP32-S3-SIM7670G-4G PPP-over-UART");

    // Raise log level so PPP/lwIP debug messages are visible
    esp_log_level_set("*", ESP_LOG_DEBUG);

    // Initialize the ESP-IDF networking stack required by lwIP/PPP.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize UART to talk to the SIM7670
    init_uart_for_modem();

    // Start the modem management task.
    xTaskCreate(modem_task, "modem_task", 4096, NULL, 5, NULL);
}

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"

#include "ppp_modem.h"

// Keep the same TAG text as the working file
static const char *TAG = "SIM7670_PPP_UART";

/*
 * UART config must match main.c (same working wiring assumptions)
 */
#define MODEM_UART_NUM        UART_NUM_1

// PPP globals / handles live in ppp_modem.c, but we need the handle here too
extern TaskHandle_t modem_task_handle;

/*
 * Simple helper: send an AT command and log all responses for a short time.
 * (unchanged)
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

static bool test_ppp_dial_wait_connect(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Starting PPP DIAL test: ATD*99# (wait CONNECT)");

    const char *cmd = "ATD*99#\r\n";
    uart_write_bytes(MODEM_UART_NUM, cmd, strlen(cmd));

    int64_t start = esp_timer_get_time();
    char acc[512];
    int acc_len = 0;
    memset(acc, 0, sizeof(acc));

    while ((esp_timer_get_time() - start) < (int64_t)timeout_ms * 1000) {
        uint8_t buf[128];
        int n = uart_read_bytes(MODEM_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n > 0) {
            // Append into accumulator (truncate if too big)
            int copy = n;
            if (acc_len + copy >= (int)sizeof(acc) - 1) {
                copy = ((int)sizeof(acc) - 1) - acc_len;
            }
            if (copy > 0) {
                memcpy(&acc[acc_len], buf, copy);
                acc_len += copy;
                acc[acc_len] = '\0';
            }

            // Log raw chunk (same style as before)
            char tmp[129];
            int tlen = (n < 128) ? n : 128;
            memcpy(tmp, buf, tlen);
            tmp[tlen] = '\0';
            ESP_LOGI(TAG, "RX: %s", tmp);

            // Success / failure markers
            if (strstr(acc, "CONNECT") != NULL) {
                ESP_LOGI(TAG, "Dial success: CONNECT detected");
                return true;
            }
            if (strstr(acc, "NO CARRIER") != NULL ||
                strstr(acc, "BUSY") != NULL ||
                strstr(acc, "NO DIALTONE") != NULL ||
                strstr(acc, "ERROR") != NULL) {
                ESP_LOGE(TAG, "Dial failed (modem replied with failure): %s", acc);
                return false;
            }
        }
    }

    ESP_LOGE(TAG, "Dial timeout: CONNECT not seen within %u ms", (unsigned)timeout_ms);
    return false;
}


void modem_task(void *arg)
{
    (void)arg;

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

		bool ok = test_ppp_dial_wait_connect(30000); // 30s
		if (!ok) {
			ESP_LOGW(TAG, "Dial did not reach CONNECT; will retry from top of loop");
			continue;
		}

		ESP_LOGI(TAG, "CONNECT received. Now in PPP data mode, creating PPP stack...");
		start_ppp_after_connect();

        ESP_LOGI(TAG, "Modem task: waiting for PPP disconnect / redial request...");
        // Block here until PPP callback notifies us about disconnect
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGW(TAG, "Modem task: got redial signal from PPP, restarting dial loop...");
        // Loop repeats, re-running AT+... and ATD*99#
    }
}

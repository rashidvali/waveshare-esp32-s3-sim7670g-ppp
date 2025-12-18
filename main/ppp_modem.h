#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// exported because modem_task stores its handle in here (same behavior)
extern TaskHandle_t modem_task_handle;

void start_ppp_after_connect(void);

#ifdef __cplusplus
}
#endif

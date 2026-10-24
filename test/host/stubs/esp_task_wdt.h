#pragma once

/* Stub Task Watchdog cho host test: cac ham la no-op, vi task tick
 * khong chay that trong test (test goi treadmill_ctrl_tick truc tiep). */

#include "esp_err.h"

static inline esp_err_t esp_task_wdt_add(void *task)
{
    (void)task;
    return ESP_OK;
}

static inline esp_err_t esp_task_wdt_reset(void)
{
    return ESP_OK;
}

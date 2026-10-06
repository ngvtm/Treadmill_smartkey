#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   1
#define pdFAIL   0

#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
#define configTICK_RATE_HZ 100

#define pdMS_TO_TICKS(ms) ((TickType_t)(((TickType_t)(ms) * configTICK_RATE_HZ) / 1000))

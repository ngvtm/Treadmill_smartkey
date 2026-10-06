#pragma once

#include "freertos/FreeRTOS.h"

/*
 * Mutex giả, đơn luồng. Đếm độ sâu để phát hiện lồng khoá — mutex thật
 * của FreeRTOS không đệ quy nên lồng nhau là deadlock trên phần cứng.
 */
typedef struct {
    int depth;
} fake_mutex_t;

typedef fake_mutex_t *SemaphoreHandle_t;

extern int  fake_mutex_max_depth;
extern int  fake_mutex_imbalance;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
int               xSemaphoreTake(SemaphoreHandle_t m, TickType_t wait);
int               xSemaphoreGive(SemaphoreHandle_t m);
void              fake_mutex_reset(void);

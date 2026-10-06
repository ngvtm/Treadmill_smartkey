#pragma once

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

/* Task giả: ghi lại lời gọi chứ không chạy thân task, để test tự điều
 * khiển từng bước render. */
extern TaskFunction_t fake_task_fn;
extern int            fake_notify_count;

int  xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                 void *arg, uint32_t prio, TaskHandle_t *out);
void xTaskNotifyGive(TaskHandle_t t);
uint32_t ulTaskNotifyTake(int clear, TickType_t wait);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

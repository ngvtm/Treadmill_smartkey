#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TAG              "BUTTON"
#define DEBOUNCE_MS      50
#define RELEASE_MS       30
#define MIN_PRESS_GAP_MS 250
#define BTN_TASK_STACK   4096
#define BTN_TASK_PRIO    10

static const int BTN_GPIO[BUTTON_ID_MAX] = {
    [BUTTON_ID_TOGGLE]     = 21,
    [BUTTON_ID_SPEED_UP]   = 22,
    [BUTTON_ID_SPEED_DOWN] = 23,
};

static QueueHandle_t     s_btn_queue = NULL;
static button_callback_t s_callback  = NULL;
static TickType_t        s_last_press_tick[BUTTON_ID_MAX] = {0};

static void IRAM_ATTR btn_isr_handler(void *arg)
{
    uint32_t id = (uint32_t)arg;
    xQueueSendFromISR(s_btn_queue, &id, NULL);
}

static void btn_task(void *arg)
{
    uint32_t id;

    while (1) {
        if (!xQueueReceive(s_btn_queue, &id, portMAX_DELAY)) {
            continue;
        }

        if (id >= BUTTON_ID_MAX) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(BTN_GPIO[id]) != 0) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - s_last_press_tick[id]) < pdMS_TO_TICKS(MIN_PRESS_GAP_MS)) {
            continue;
        }
        s_last_press_tick[id] = now;

        ESP_LOGD(TAG, "Button pressed: id=%lu gpio=%d", id, BTN_GPIO[id]);

        if (s_callback) {
            s_callback((button_id_t)id);
        }

        while (gpio_get_level(BTN_GPIO[id]) == 0) {
            vTaskDelay(pdMS_TO_TICKS(RELEASE_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
    }
}

void button_init(button_callback_t cb)
{
    s_callback  = cb;
    s_btn_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_NEGEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = (1ULL << BTN_GPIO[BUTTON_ID_TOGGLE])
                      | (1ULL << BTN_GPIO[BUTTON_ID_SPEED_UP])
                      | (1ULL << BTN_GPIO[BUTTON_ID_SPEED_DOWN]),
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        gpio_isr_handler_add(BTN_GPIO[i], btn_isr_handler, (void *)(uint32_t)i);
    }

    xTaskCreate(btn_task, "btn_task", BTN_TASK_STACK, NULL, BTN_TASK_PRIO, NULL);
    ESP_LOGI(TAG, "Initialized: GPIO21=Toggle, GPIO22=SpeedUp, GPIO23=SpeedDown");
}

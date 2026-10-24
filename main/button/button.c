#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#define TAG              "BUTTON"
#define POLL_MS          20
#define BTN_TASK_STACK   4096
#define BTN_TASK_PRIO    10

/* Phase 1, S1.3 timings (menuconfig "Smartkey Control"). */
#ifndef CONFIG_TREADMILL_BUTTON_HOLD_START_MS
#define CONFIG_TREADMILL_BUTTON_HOLD_START_MS 1500
#endif
#ifndef CONFIG_TREADMILL_BUTTON_STUCK_MS
#define CONFIG_TREADMILL_BUTTON_STUCK_MS 5000
#endif

static const int BTN_GPIO[BUTTON_ID_MAX] = {
    [BUTTON_ID_POWER]   = 21,
    [BUTTON_ID_ESTOP_1] = 22,
    [BUTTON_ID_ESTOP_2] = 23,
};

static button_callback_t s_callback = NULL;
static button_sm_t s_sm[BUTTON_ID_MAX];

// Per-button timings. The power button needs a longer hold (2 s) than
// the old 1.5 s start hold: sleeping the key mid-run stops the machine
// (via BLE disconnect), so pocket presses must not trigger it.
static const button_sm_cfg_t s_sm_cfg[BUTTON_ID_MAX] = {
    [BUTTON_ID_POWER] = {
        .debounce_ms = 50,
        .hold_ms = 2000,
        .stuck_ms = CONFIG_TREADMILL_BUTTON_STUCK_MS,
        .min_gap_ms = 250,
    },
    [BUTTON_ID_ESTOP_1] = {
        .debounce_ms = 50,
        .hold_ms = CONFIG_TREADMILL_BUTTON_HOLD_START_MS,
        .stuck_ms = CONFIG_TREADMILL_BUTTON_STUCK_MS,
        .min_gap_ms = 250,
    },
    [BUTTON_ID_ESTOP_2] = {
        .debounce_ms = 50,
        .hold_ms = CONFIG_TREADMILL_BUTTON_HOLD_START_MS,
        .stuck_ms = CONFIG_TREADMILL_BUTTON_STUCK_MS,
        .min_gap_ms = 250,
    },
};

static void btn_task(void *arg)
{
    (void)arg;
    // Phase 1, S1.5: watchdog-supervised (see app_main).
    esp_task_wdt_add(NULL);
    while (1) {
        esp_task_wdt_reset();
        uint32_t now_ms =
            (uint32_t)((uint64_t)xTaskGetTickCount() * 1000u / configTICK_RATE_HZ);
        for (int i = 0; i < BUTTON_ID_MAX; i++) {
            // Button to GND with pull-up: low level means pressed.
            bool pressed = gpio_get_level(BTN_GPIO[i]) == 0;
            button_event_t ev = button_sm_update(&s_sm[i], pressed, now_ms, &s_sm_cfg[i]);
            if (ev != BTN_EV_NONE && s_callback) {
                ESP_LOGD(TAG, "Button event: id=%d event=%d", i, (int)ev);
                s_callback((button_id_t)i, ev);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void button_init(button_callback_t cb)
{
    s_callback = cb;

    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = (1ULL << BTN_GPIO[BUTTON_ID_POWER])
                      | (1ULL << BTN_GPIO[BUTTON_ID_ESTOP_1])
                      | (1ULL << BTN_GPIO[BUTTON_ID_ESTOP_2]),
    };
    gpio_config(&io_conf);

    // A button already pressed at boot starts STUCK (see button_logic.h):
    // a key wedged down before power-on must never start the belt.
    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        button_sm_init(&s_sm[i], gpio_get_level(BTN_GPIO[i]) == 0);
    }

    xTaskCreate(btn_task, "btn_task", BTN_TASK_STACK, NULL, BTN_TASK_PRIO, NULL);
    ESP_LOGI(TAG, "Initialized (poll %d ms): GPIO21=Power, GPIO22/23=E-Stop",
             POLL_MS);
}

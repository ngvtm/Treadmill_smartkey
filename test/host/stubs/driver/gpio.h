#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef enum { GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2 } gpio_mode_t;
typedef enum { GPIO_INTR_DISABLE = 0, GPIO_INTR_NEGEDGE = 2 } gpio_int_type_t;
typedef enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 } gpio_pulldown_t;

typedef struct {
    uint64_t        pin_bit_mask;
    gpio_mode_t     mode;
    gpio_pullup_t   pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

/* Ghi lại mặt nạ chân đã cấu hình để test kiểm tra xung đột GPIO. */
extern uint64_t fake_gpio_configured_mask;
extern int      fake_gpio_level[64];

esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_level(int pin, int level);
int       gpio_get_level(int pin);

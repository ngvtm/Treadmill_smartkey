#include "sdkconfig.h"
#include "touch_xpt2046.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "lcd/lcd_gfx.h"

#ifndef CONFIG_TREADMILL_TOUCH_CS_GPIO
#define CONFIG_TREADMILL_TOUCH_CS_GPIO 13
#endif
#ifndef CONFIG_TREADMILL_TOUCH_IRQ_GPIO
#define CONFIG_TREADMILL_TOUCH_IRQ_GPIO 14
#endif

#define TAG "TOUCH"
// XPT2046 control bytes (START=1, channel, MODE=0 for 12-bit, power 000):
// X channel 101 -> 0xD0, Y channel 001 -> 0x90.
#define XPT2046_CMD_X 0xD0
#define XPT2046_CMD_Y 0x90
#define XPT2046_SAMPLES 5

static spi_device_handle_t s_dev = NULL;
static bool s_ready = false;

bool touch_xpt2046_init(void)
{
    gpio_config_t irq_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_TREADMILL_TOUCH_IRQ_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&irq_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "IRQ GPIO%d config failed",
                 CONFIG_TREADMILL_TOUCH_IRQ_GPIO);
        return false;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2000000,  // XPT2046 ceiling; display runs 40 MHz
        .mode = 0,
        .spics_io_num = CONFIG_TREADMILL_TOUCH_CS_GPIO,
        .queue_size = 1,
    };
    if (spi_bus_add_device(SPI2_HOST, &devcfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed");
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "XPT2046 ready: CS=GPIO%d IRQ=GPIO%d MISO=GPIO%d",
             CONFIG_TREADMILL_TOUCH_CS_GPIO,
             CONFIG_TREADMILL_TOUCH_IRQ_GPIO, TFT_PIN_TOUCH_MISO);
    return true;
}

bool touch_xpt2046_is_touched(void)
{
    if (!s_ready) {
        return false;
    }
    return gpio_get_level(CONFIG_TREADMILL_TOUCH_IRQ_GPIO) == 0;
}

// One axis read: send the control byte, then clock out 12 bits
// (first rx byte is garbage clocked during the control byte).
static bool read_axis(uint8_t cmd, uint16_t *out)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0, 0, 0 };
    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_transmit(s_dev, &t) != ESP_OK) {
        return false;
    }
    *out = (uint16_t)(((rx[1] << 4) | (rx[2] >> 4)) & 0x0FFF);
    return true;
}

static bool read_axis_median(uint8_t cmd, uint16_t *out)
{
    uint16_t samples[XPT2046_SAMPLES];
    for (int i = 0; i < XPT2046_SAMPLES; i++) {
        if (!read_axis(cmd, &samples[i])) {
            return false;
        }
    }
    // Insertion sort of 5 elements; the middle one is the median.
    for (int i = 1; i < XPT2046_SAMPLES; i++) {
        uint16_t v = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > v) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = v;
    }
    *out = samples[XPT2046_SAMPLES / 2];
    return true;
}

bool touch_xpt2046_read_raw(touch_raw_t *out)
{
    if (!s_ready || !touch_xpt2046_is_touched()) {
        return false;
    }
    uint16_t x = 0, y = 0;
    if (!read_axis_median(XPT2046_CMD_X, &x) ||
        !read_axis_median(XPT2046_CMD_Y, &y)) {
        return false;
    }
    out->raw_x = (int32_t)x;
    out->raw_y = (int32_t)y;
    return true;
}

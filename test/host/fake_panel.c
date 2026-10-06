#include "fake_panel.h"
#include "lcd_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/*
 * Panel e-paper gia: giai ma lenh SSD1680, dung lai anh 1 bpp thanh
 * framebuffer 1 byte/pixel de test assert tren pixel that.
 */
uint8_t fp_fb[FP_H][FP_W];

int fp_flushes;
int fp_bytes;
int fp_oob;

int fp_saw_swreset;
int fp_saw_master_act;
int fp_driver_mux = -1;
int fp_data_entry = -1;
int fp_ram_x_end  = -1;
int fp_ram_y_end  = -1;
int fp_update_seq = -1;
int fp_deep_sleep;

void fp_reset(void)
{
    fp_flushes = 0;
    fp_bytes   = 0;
    fp_oob     = 0;
}

void fp_clear(uint8_t color)
{
    for (int y = 0; y < FP_H; y++) {
        for (int x = 0; x < FP_W; x++) {
            fp_fb[y][x] = color;
        }
    }
}

int fp_count(int x, int y, int w, int h, uint8_t c)
{
    int n = 0;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (j >= 0 && j < FP_H && i >= 0 && i < FP_W && fp_fb[j][i] == c) {
                n++;
            }
        }
    }
    return n;
}

int fp_uniform(int x, int y, int w, int h, uint8_t c)
{
    return fp_count(x, y, w, h, c) == w * h;
}

int fp_ink(int x, int y, int w, int h)
{
    return fp_count(x, y, w, h, LCD_INK);
}

/* ---------------- spi ---------------- */

spi_bus_config_t fake_spi_bus_cfg;

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *cfg, int dma)
{
    (void)host;
    (void)dma;
    fake_spi_bus_cfg = *cfg;
    return ESP_OK;
}

/* ---------------- esp_lcd panel io ---------------- */

esp_lcd_panel_io_spi_config_t fake_io_cfg;

static struct esp_lcd_panel_io_t *s_io_obj = (struct esp_lcd_panel_io_t *)0x2000;

esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
                                   const esp_lcd_panel_io_spi_config_t *cfg,
                                   esp_lcd_panel_io_handle_t *out)
{
    (void)bus;
    fake_io_cfg = *cfg;
    *out = s_io_obj;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int cmd,
                                    const void *param, size_t len)
{
    (void)io;
    const uint8_t *p = (const uint8_t *)param;

    switch (cmd) {
    case 0x12: fp_saw_swreset = 1; break;
    case 0x20: fp_saw_master_act++; break;
    case 0x10: if (len >= 1) { fp_deep_sleep = p[0]; } break;
    case 0x01: if (len >= 3) { fp_driver_mux = p[0] | (p[1] << 8); } break;
    case 0x11: if (len >= 1) { fp_data_entry = p[0]; } break;
    case 0x44: if (len >= 2) { fp_ram_x_end = p[1]; } break;
    case 0x45: if (len >= 4) { fp_ram_y_end = p[2] | (p[3] << 8); } break;
    case 0x22: if (len >= 1) { fp_update_seq = p[0]; } break;
    default:   break;
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int cmd,
                                    const void *color, size_t len)
{
    (void)io;
    if (cmd != 0x24) {
        return ESP_OK;
    }

    const uint8_t *raw    = (const uint8_t *)color;
    const int      stride = EPD_PANEL_W / 8;

    fp_flushes++;
    fp_bytes += (int)len;

    if ((int)len != stride * EPD_PANEL_H) {
        fp_oob++;
        return ESP_OK;
    }

    for (int py = 0; py < EPD_PANEL_H; py++) {
        for (int px = 0; px < EPD_PANEL_W; px++) {
            const uint8_t byte = raw[py * stride + (px >> 3)];
            const int     bit  = (byte >> (7 - (px & 7))) & 1;
            /* RAM SSD168x: bit 1 = trang, 0 = den. */
            const uint8_t v = bit ? LCD_PAPER : LCD_INK;

            /* Gio toa do panel -> toa do logic, nguoc voi put_px(). */
#if EPD_ROTATION
            const int lx = EPD_PANEL_H - 1 - py;
            const int ly = px;
#else
            const int lx = px;
            const int ly = py;
#endif
            if (lx >= 0 && lx < FP_W && ly >= 0 && ly < FP_H) {
                fp_fb[ly][lx] = v;
            } else {
                fp_oob++;
            }
        }
    }
    return ESP_OK;
}

/* ---------------- gpio ---------------- */

uint64_t fake_gpio_configured_mask;
int      fake_gpio_level[64];
int      fake_gpio_busy_pin = -1;

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    fake_gpio_configured_mask |= cfg->pin_bit_mask;
    return ESP_OK;
}

esp_err_t gpio_set_level(int pin, int level)
{
    if (pin >= 0 && pin < 64) {
        fake_gpio_level[pin] = level;
    }
    return ESP_OK;
}

int gpio_get_level(int pin)
{
    /* BUSY luon bao ranh, khong thi wait_busy() se treo het timeout. */
    if (pin == EPD_PIN_BUSY) {
        return 0;
    }
    return (pin >= 0 && pin < 64) ? fake_gpio_level[pin] : 1;
}

/* ---------------- freertos ---------------- */

int fake_mutex_max_depth;
int fake_mutex_imbalance;

static fake_mutex_t s_mutexes[8];
static int          s_mutex_count;

void fake_mutex_reset(void)
{
    fake_mutex_max_depth = 0;
    fake_mutex_imbalance = 0;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    if (s_mutex_count >= 8) {
        return NULL;
    }
    fake_mutex_t *m = &s_mutexes[s_mutex_count++];
    m->depth = 0;
    return m;
}

int xSemaphoreTake(SemaphoreHandle_t m, TickType_t wait)
{
    (void)wait;
    m->depth++;
    if (m->depth > fake_mutex_max_depth) {
        fake_mutex_max_depth = m->depth;
    }
    return pdTRUE;
}

int xSemaphoreGive(SemaphoreHandle_t m)
{
    m->depth--;
    if (m->depth < 0) {
        fake_mutex_imbalance++;
    }
    return pdTRUE;
}

TaskFunction_t fake_task_fn;
int            fake_notify_count;

int xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                void *arg, uint32_t prio, TaskHandle_t *out)
{
    (void)name; (void)stack; (void)arg; (void)prio;
    fake_task_fn = fn;              /* khong chay: test tu goi render() */
    if (out) {
        *out = (TaskHandle_t)0x3000;
    }
    return pdPASS;
}

void xTaskNotifyGive(TaskHandle_t t)
{
    (void)t;
    fake_notify_count++;
}

uint32_t ulTaskNotifyTake(int clear, TickType_t wait)
{
    (void)clear; (void)wait;
    return 0;
}

void vTaskDelay(TickType_t ticks) { (void)ticks; }

TickType_t xTaskGetTickCount(void) { return 0; }

int64_t esp_timer_get_time(void) { return 0; }

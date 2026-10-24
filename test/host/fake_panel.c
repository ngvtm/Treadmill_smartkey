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
 * Panel TFT gia: giai ma lenh ILI9341/ST7789, dung lai anh RGB565
 * thanh framebuffer palette de test assert tren pixel that.
 */
uint8_t fp_fb[FP_H][FP_W];

int fp_frames;
int fp_ramwr;
int fp_bytes;
int fp_oob;

int fp_saw_swreset;
int fp_saw_slpout;
int fp_saw_dispon;
int fp_saw_dispoff;
int fp_saw_slpin;
int fp_saw_invon;
int fp_colmod = -1;
int fp_madctl = -1;
int fp_win_x0 = -1, fp_win_x1 = -1, fp_win_y0 = -1, fp_win_y1 = -1;

static int s_cx, s_cy;   /* con tro ghi trong cua so RAM */

void fp_reset(void)
{
    fp_frames = 0;
    fp_ramwr  = 0;
    fp_bytes  = 0;
    fp_oob    = 0;
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

/* Vung fb (x, y, w, h) -> vung panel (LCD_H-(y+h), x, h, w). */
int fp_count_fb(int x, int y, int w, int h, uint8_t c)
{
    return fp_count(LCD_H - (y + h), x, h, w, c);
}

int fp_uniform_fb(int x, int y, int w, int h, uint8_t c)
{
    return fp_count_fb(x, y, w, h, c) == w * h;
}

int fp_ink_fb(int x, int y, int w, int h)
{
    return fp_count_fb(x, y, w, h, LCD_INK);
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
    case 0x01: fp_saw_swreset = 1; break;
    case 0x11: fp_saw_slpout++; break;
    case 0x29: fp_saw_dispon++; break;
    case 0x28: fp_saw_dispoff++; break;
    case 0x10: fp_saw_slpin++; break;
    case 0x21: fp_saw_invon = 1; break;
    case 0x3A: if (len >= 1) { fp_colmod = p[0]; } break;
    case 0x36: if (len >= 1) { fp_madctl = p[0]; } break;
    case 0x2A:
        if (len >= 4) {
            fp_win_x0 = (p[0] << 8) | p[1];
            fp_win_x1 = (p[2] << 8) | p[3];
        }
        break;
    case 0x2B:
        if (len >= 4) {
            fp_win_y0 = (p[0] << 8) | p[1];
            fp_win_y1 = (p[2] << 8) | p[3];
            s_cx = fp_win_x0;
            s_cy = fp_win_y0;
        }
        break;
    default:   break;
    }
    return ESP_OK;
}

static uint8_t palette_of(uint16_t rgb)
{
    switch (rgb) {
    case 0xFFFF: return LCD_PAPER;
    case 0x0000: return LCD_INK;
    case 0xF800: return LCD_RED;
    case 0x07E0: return LCD_GREEN;
    default:     return 0xEE;   /* mau la: test se bat qua fp_oob */
    }
}

esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int cmd,
                                    const void *color, size_t len)
{
    (void)io;
    if (cmd != 0x2C) {
        return ESP_OK;
    }

    const uint8_t *raw = (const uint8_t *)color;

    fp_ramwr++;
    fp_bytes += (int)len;
    if (fp_win_y0 == 0 && fp_win_x0 == 0 && s_cy == 0) {
        fp_frames++;            /* dai dau tien cua mot khung moi */
    }

    if ((len & 1) || fp_win_x0 < 0 || fp_win_y0 < 0) {
        fp_oob++;
        return ESP_OK;
    }

    for (size_t i = 0; i + 1 < len; i += 2) {
        const uint16_t rgb = (uint16_t)((raw[i] << 8) | raw[i + 1]);
        const uint8_t  v   = palette_of(rgb);
        if (v == 0xEE) {
            fp_oob++;
        }
        if (s_cx > fp_win_x1 || s_cy > fp_win_y1 ||
            s_cx < 0 || s_cx >= FP_W || s_cy < 0 || s_cy >= FP_H) {
            fp_oob++;
        } else {
            fp_fb[s_cy][s_cx] = v;
        }
        /* Tien con tro ghi: MV=0 tang CASET truoc (ghi theo hang);
         * MV=1 tang RASET truoc (ghi theo cot) — dung kieu panel that
         * the hien o ban d05cacd (gui hang duoi MV bi xao cheo thanh
         * vet ngang). Firmware phai gui cot-major khi MV=1. */
        if (fp_madctl & 0x20) {
            s_cy++;
            if (s_cy > fp_win_y1) {
                s_cy = fp_win_y0;
                s_cx++;
            }
        } else {
            s_cx++;
            if (s_cx > fp_win_x1) {
                s_cx = fp_win_x0;
                s_cy++;
            }
        }
    }
    return ESP_OK;
}

/* ---------------- gpio ---------------- */

uint64_t fake_gpio_configured_mask;
int      fake_gpio_level[64];

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

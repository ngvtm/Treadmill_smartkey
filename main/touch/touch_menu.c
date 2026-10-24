#include "sdkconfig.h"
#include "touch_menu.h"
#include "touch_xpt2046.h"
#include "touch_calibration.h"

#include "ble_client/ble_client.h"
#include "lcd/lcd_display.h"
#include "lcd/lcd_gfx.h"
#include "treadmill_ctrl/treadmill_ctrl.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#ifndef CONFIG_TREADMILL_MENU_HOLD_MS
#define CONFIG_TREADMILL_MENU_HOLD_MS 2000
#endif
#ifndef CONFIG_TREADMILL_MENU_IDLE_TIMEOUT_S
#define CONFIG_TREADMILL_MENU_IDLE_TIMEOUT_S 30
#endif

#define TAG "TOUCH_MENU"
// Feature ids mirror the server's settings_logic.h.
#define MENU_FEATURE_IR   0
#define MENU_FEATURE_REED 1

#define MENU_POLL_MS 20
#define MENU_HEADER_H 35
#define MENU_ROW_H 56
#define MENU_ROW_GAP 8
#define MENU_FIRST_ROW_Y 44

typedef enum {
    MENU_CLOSED = 0,
    MENU_OPEN,
    MENU_CALIBRATING,
} menu_state_t;

static menu_state_t s_state = MENU_CLOSED;
static touch_cal_t s_cal;
static bool s_calibrated = false;  // true only when loaded from NVS
static bool s_touch_ok = false;
static uint8_t s_last_mask = 0xFF;  // forces a first paint

// ---- NVS calibration store ----
static bool cal_load(void)
{
    nvs_handle_t h;
    if (nvs_open("smartkey_cfg", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = sizeof(s_cal);
    esp_err_t err = nvs_get_blob(h, "touch_cal", &s_cal, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(s_cal) && s_cal.valid;
}

static bool cal_save(void)
{
    nvs_handle_t h;
    if (nvs_open("smartkey_cfg", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, "touch_cal", &s_cal, sizeof(s_cal));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

// ---- Drawing (lcd_gfx directly; the normal task is paused) ----
static void draw_menu(void)
{
    lcd_gfx_clear(LCD_PAPER);
    lcd_gfx_text_center(0, LCD_W, 8, "CAI DAT AN TOAN", 2, LCD_INK, LCD_PAPER);
    lcd_gfx_fill_rect(0, MENU_HEADER_H, LCD_W, 2, LCD_INK);

    static const struct {
        const char *label;
        int feature;  // -1 = action row, not a toggle
    } rows[] = {
        { "BAO VE VUNG IR", MENU_FEATURE_IR },
        { "KHOA AN TOAN", MENU_FEATURE_REED },
        { "HIEU CHUAN CAM UNG", -1 },
        { "THOAT", -1 },
    };
    uint8_t mask = treadmill_ctrl_get_features_off_mask();
    for (int i = 0; i < 4; i++) {
        int y = MENU_FIRST_ROW_Y + i * (MENU_ROW_H + MENU_ROW_GAP);
        lcd_gfx_rect(4, y, LCD_W - 8, MENU_ROW_H, 2, LCD_INK);
        lcd_gfx_text(12, y + (MENU_ROW_H - 2 * LCD_GLYPH_H) / 2,
                     rows[i].label, 2, LCD_INK, LCD_PAPER);
        if (rows[i].feature >= 0) {
            bool off = (mask & (1u << rows[i].feature)) != 0;
            int pw = 64, ph = 28;
            int px = LCD_W - 12 - pw;
            int py = y + (MENU_ROW_H - ph) / 2;
            lcd_gfx_fill_rect(px, py, pw, ph,
                              off ? LCD_RED : LCD_GREEN);
            lcd_gfx_text_center(px, pw, py + (ph - LCD_GLYPH_H) / 2,
                                off ? "TAT" : "BAT", 1,
                                LCD_PAPER, off ? LCD_RED : LCD_GREEN);
        } else {
            lcd_gfx_text(LCD_W - 28,
                         y + (MENU_ROW_H - 2 * LCD_GLYPH_H) / 2,
                         ">", 2, LCD_INK, LCD_PAPER);
        }
    }
    lcd_gfx_text_center(0, LCD_W, MENU_FIRST_ROW_Y + 4 * MENU_ROW_H +
                        3 * MENU_ROW_GAP + 2,
                        "CHI DOI DUOC KHI MAY DUNG", 1,
                        LCD_INK, LCD_PAPER);
    lcd_gfx_flush();
}

static void draw_cal_target(int step, int x, int y)
{
    lcd_gfx_clear(LCD_PAPER);
    /* Buffer must hold "CHAM VAO DAU + (3/3)" = 21 chars + NUL = 22 bytes.
     * 16 bytes was a real overflow, caught by IDF 5.3 -Werror=format-truncation;
     * 32 bytes leaves margin. UI text itself is unchanged. */
    char title[32];
    snprintf(title, sizeof(title), "CHAM VAO DAU + (%d/3)", step + 1);
    lcd_gfx_text_center(0, LCD_W, 24, title, 2, LCD_INK, LCD_PAPER);
    lcd_gfx_rect(x - 16, y - 16, 33, 33, 2, LCD_RED);
    lcd_gfx_fill_rect(x - 1, y - 12, 3, 25, LCD_RED);
    lcd_gfx_fill_rect(x - 12, y - 1, 25, 3, LCD_RED);
    lcd_gfx_flush();
}

// Wait for one full tap (press then release) and return its raw
// sample; false on timeout (caller decides: retry or abort).
// Feeds the task watchdog inside the wait: a 60 s tap window far
// exceeds the 2 s watchdog, and a trip mid-calibration would be a
// false alarm, not a hung task.
static bool wait_for_tap(touch_raw_t *out, uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        esp_task_wdt_reset();
        if (touch_xpt2046_read_raw(out)) {
            // Wait for release so one tap cannot double-trigger.
            while (touch_xpt2046_is_touched() && waited < timeout_ms) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(MENU_POLL_MS));
                waited += MENU_POLL_MS;
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(MENU_POLL_MS));
        waited += MENU_POLL_MS;
    }
    return false;
}

static void run_calibration(void)
{
    static const touch_screen_t targets[3] = {
        { 30, 30 }, { 210, 30 }, { 120, 290 },
    };
    // Loop, never recurse: repeated degenerate taps must not grow
    // the stack.
    while (1) {
        touch_raw_t raw[3];
        bool aborted = false;
        for (int i = 0; i < 3; i++) {
            draw_cal_target(i, targets[i].x, targets[i].y);
            if (!wait_for_tap(&raw[i], 60000)) {
                ESP_LOGW(TAG, "calibration aborted at step %d", i + 1);
                aborted = true;
                break;
            }
        }
        if (aborted) {
            return;
        }
        touch_cal_t cal;
        if (!touch_cal_compute(&cal, raw, targets)) {
            ESP_LOGW(TAG, "calibration degenerate (collinear taps), retry");
            lcd_gfx_clear(LCD_PAPER);
            lcd_gfx_text_center(0, LCD_W, 140, "CHAM KHONG HOP LE,", 2,
                                LCD_INK, LCD_PAPER);
            lcd_gfx_text_center(0, LCD_W, 160, "THU LAI", 2,
                                LCD_INK, LCD_PAPER);
            lcd_gfx_flush();
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }
        s_cal = cal;
        s_calibrated = true;
        if (cal_save()) {
            ESP_LOGI(TAG, "touch calibration saved");
        } else {
            ESP_LOGE(TAG, "touch calibration NOT saved (NVS error)");
        }
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text_center(0, LCD_W, 140, "HIEU CHUAN XONG", 2,
                            LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
}

static int menu_row_at(int y)
{
    for (int i = 0; i < 4; i++) {
        int top = MENU_FIRST_ROW_Y + i * (MENU_ROW_H + MENU_ROW_GAP);
        if (y >= top && y < top + MENU_ROW_H) {
            return i;
        }
    }
    return -1;
}

static void enter_menu(void)
{
    lcd_display_pause();
    s_state = MENU_OPEN;
    s_last_mask = 0xFF;
    draw_menu();
    ESP_LOGI(TAG, "menu opened");
}

static void exit_menu(void)
{
    s_state = MENU_CLOSED;
    lcd_display_resume();
    ESP_LOGI(TAG, "menu closed");
}

static void touch_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    uint32_t hold_since_ms = 0;
    bool was_touched = false;
    uint32_t last_touch_ms = 0;

    while (1) {
        esp_task_wdt_reset();
        uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
        touch_raw_t raw;
        bool touched = touch_xpt2046_read_raw(&raw);
        touch_screen_t scr = touch_cal_map(&s_cal, raw);

        if (s_state == MENU_CLOSED) {
            // Entry gesture: hold the header area while stopped and
            // connected. Any release, move out of the header, start,
            // or disconnect resets the hold.
            bool gesture_ok = touched && scr.y >= 0 &&
                              scr.y < MENU_HEADER_H &&
                              !treadmill_ctrl_is_running() &&
                              ble_client_is_ready();
            if (gesture_ok) {
                if (hold_since_ms == 0) {
                    hold_since_ms = now_ms;
                } else if (now_ms - hold_since_ms >=
                           CONFIG_TREADMILL_MENU_HOLD_MS) {
                    hold_since_ms = 0;
                    if (!s_calibrated) {
                        lcd_display_pause();
                        s_state = MENU_CALIBRATING;
                        run_calibration();
                        s_state = MENU_CLOSED;
                        lcd_display_resume();
                    } else {
                        enter_menu();
                    }
                    last_touch_ms = now_ms;
                }
            } else {
                hold_since_ms = 0;
            }
            was_touched = touched;
        } else if (s_state == MENU_OPEN) {
            // Exit conditions, checked before input.
            if (treadmill_ctrl_is_running() || !ble_client_is_ready() ||
                now_ms - last_touch_ms >
                    (uint32_t)CONFIG_TREADMILL_MENU_IDLE_TIMEOUT_S * 1000) {
                exit_menu();
                was_touched = touched;
                continue;
            }
            // Repaint when the server's mask changed (SET_FEATURE
            // confirmed or refused — the toggle visibly follows).
            uint8_t mask = treadmill_ctrl_get_features_off_mask();
            if (mask != s_last_mask) {
                s_last_mask = mask;
                draw_menu();
            }
            // Rising edge only: one tap = one action.
            if (touched && !was_touched && scr.x >= 0 && scr.y >= 0) {
                last_touch_ms = now_ms;
                int row = menu_row_at(scr.y);
                if (row == 0 || row == 1) {
                    int feature = (row == 0) ? MENU_FEATURE_IR
                                             : MENU_FEATURE_REED;
                    bool currently_off =
                        (mask & (1u << feature)) != 0;
                    ble_client_write_feature(feature, currently_off);
                    // Do not repaint here: the mask refresh does it.
                } else if (row == 2) {
                    s_state = MENU_CALIBRATING;
                    run_calibration();
                    s_state = MENU_OPEN;
                    s_last_mask = 0xFF;
                    draw_menu();
                } else if (row == 3) {
                    exit_menu();
                }
            }
            was_touched = touched;
        }

        vTaskDelay(pdMS_TO_TICKS(MENU_POLL_MS));
    }
}

void touch_menu_init(void)
{
    if (!touch_xpt2046_init()) {
        ESP_LOGE(TAG, "touch unavailable: menu disabled");
        s_touch_ok = false;
        return;
    }
    s_touch_ok = true;
    if (cal_load()) {
        s_calibrated = true;
        ESP_LOGI(TAG, "touch calibration loaded from NVS");
    } else {
        ESP_LOGI(TAG, "no touch calibration: default map until first menu");
        s_cal = touch_cal_default(LCD_W, LCD_H);
    }
    xTaskCreate(touch_task, "touch_menu", 4096, NULL, 9, NULL);
}

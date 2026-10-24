#include "lcd_display.h"
#include "lcd_gfx.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG            "LCD"
#define LCD_TASK_STACK 4096
#define LCD_TASK_PRIO  4

/*
 * TFT refresh một khung chỉ ~35 ms nên không cần gộp khung nặng như
 * e-paper trước đây. Vẫn giữ một cửa sổ gộp nhỏ (SETTLE_MS) để dồn
 * các bản tin status đến sát nhau thành một lần vẽ, và MIN_GAP_MS để
 * chống rung khi trạng thái nhấp nháy.
 */
#define SETTLE_MS   60
#define MIN_GAP_MS 120

/*
 * Bố cục tự co theo kích thước tấm: chia màn thành bốn dải theo tỉ lệ,
 * và mỗi chuỗi tự chọn cỡ chữ lớn nhất còn lọt bề rộng. Nhờ vậy bố cục
 * không phụ thuộc cứng vào đúng 240x320.
 *
 *   +------------------------+
 *   | SMARTKEY   [DA KET NOI]|  dải tiêu đề
 *   +------------------------+
 *   | TRANG THAI             |  dải trạng thái
 *   | DANG CHAY              |
 *   | DIEU KHIEN: BAT        |
 *   +------------------------+
 *   | MUC TOC DO             |  dải tốc độ
 *   |          5             |
 *   | [#][#][#][ ][ ]        |
 *   | YC 5   VUNG 2          |
 *   +------------------------+
 *   | ### NGUY HIEM ###      |  dải cảnh báo, nền đảo khi nghiêm trọng
 *   +------------------------+
 */
#define PAD        3
#define INNER_W    (LCD_W - 2 * PAD)

#define HDR_H      (LCD_H * 11 / 100)
#define ST_Y       (HDR_H + 3)
#define ST_H       (LCD_H * 26 / 100)
#define SP_Y       (ST_Y + ST_H + 3)
#define SP_H       (LCD_H * 36 / 100)
#define AL_Y       (SP_Y + SP_H + 3)
#define AL_H       (LCD_H - AL_Y - PAD)

/* Trạng thái mong muốn (do module khác đẩy vào, có khoá bảo vệ). */
static SemaphoreHandle_t s_lock;
static lcd_status_t      s_want;
static lcd_link_state_t  s_want_link;

/* Trạng thái đã vẽ lên panel, chỉ task vẽ đụng tới. */
static lcd_status_t      s_shown;
static lcd_link_state_t  s_shown_link;
static bool              s_have_shown;

static TaskHandle_t      s_task;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Vẽ chuỗi canh giữa dải, tự chọn cỡ lớn nhất còn lọt. Trả về cỡ đã dùng,
 * 0 nếu không lọt nổi ngay cả ở cỡ 1. */
static int fit_center(int y, const char *s, int max_scale,
                      uint8_t fg, uint8_t bg)
{
    const int sc = lcd_gfx_fit_scale(s, INNER_W, max_scale);
    if (sc > 0) {
        lcd_gfx_text_center(PAD, INNER_W, y, s, sc, fg, bg);
    }
    return sc;
}

static void draw_header(lcd_link_state_t link)
{
    const char *txt;
    switch (link) {
    case LCD_LINK_CONNECTED: txt = "DA KET NOI";  break;
    case LCD_LINK_SCANNING:  txt = "DANG TIM";    break;
    default:                 txt = "MAT KET NOI"; break;
    }

    int sc = lcd_gfx_fit_scale(txt, LCD_W / 2, 2);
    if (sc < 1) {
        sc = 1;
    }
    const int bw = lcd_gfx_text_width(txt, sc) + 8;
    const int bh = LCD_GLYPH_H * sc + 4;
    const int bx = LCD_W - PAD - bw;
    const int by = (HDR_H - bh) / 2;

    lcd_gfx_text(PAD, (HDR_H - LCD_GLYPH_H) / 2, "SMARTKEY", 1,
                 LCD_INK, LCD_PAPER);

    /* Đã kết nối = nền xanh lá, chưa nối = viền đen trên nền trắng. */
    if (link == LCD_LINK_CONNECTED) {
        lcd_gfx_text_box(bx, by, bw, bh, txt, sc, LCD_PAPER, LCD_GREEN);
    } else {
        lcd_gfx_text_box(bx, by, bw, bh, txt, sc, LCD_INK, LCD_PAPER);
        lcd_gfx_rect(bx, by, bw, bh, 1, LCD_INK);
    }

    lcd_gfx_fill_rect(0, HDR_H, LCD_W, 2, LCD_INK);
}

static void draw_state(const lcd_status_t *st)
{
    int y = ST_Y;
    y += fit_center(y, "TRANG THAI", 1, LCD_INK, LCD_PAPER) * LCD_GLYPH_H + 2;
    y += fit_center(y, st->running ? "DANG CHAY" : "DA DUNG", 3,
                    LCD_INK, LCD_PAPER) * LCD_GLYPH_H + 2;
    fit_center(y, st->control_enabled ? "DIEU KHIEN: BAT" : "DIEU KHIEN: TAT",
               1, LCD_INK, LCD_PAPER);
}

static void draw_bar(int y, int h, uint8_t level)
{
    const int gap  = 2;
    const int segw = (INNER_W - (LCD_SPEED_MAX - 1) * gap) / LCD_SPEED_MAX;
    const int used = LCD_SPEED_MAX * segw + (LCD_SPEED_MAX - 1) * gap;
    const int x0   = PAD + (INNER_W - used) / 2;

    for (int i = 0; i < LCD_SPEED_MAX; i++) {
        const int x = x0 + i * (segw + gap);
        if (i < level) {
            lcd_gfx_fill_rect(x, y, segw, h, LCD_INK);
        } else {
            lcd_gfx_rect(x, y, segw, h, 1, LCD_INK);
        }
    }
}

static void draw_speed(const lcd_status_t *st)
{
    char buf[24];
    int  y = SP_Y;

    y += fit_center(y, "MUC TOC DO", 1, LCD_INK, LCD_PAPER) * LCD_GLYPH_H + 2;

    if (st->running) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)st->speed);
    } else {
        strcpy(buf, "--");
    }
    y += fit_center(y, buf, 6, LCD_INK, LCD_PAPER) * LCD_GLYPH_H + 4;

    const int bar_h = 8;
    draw_bar(y, bar_h, st->running ? st->speed : 0);
    y += bar_h + 3;

    /* "!" danh dau khi server dang gioi han toc do vi an toan. */
    snprintf(buf, sizeof(buf), "%sYC %u  VUNG %u",
             st->safety_limited ? "! " : "",
             (unsigned)st->requested_speed, (unsigned)st->zone);
    fit_center(y, buf, 1, LCD_INK, LCD_PAPER);
}

/* Mức nghiêm trọng mã hoá bằng cả hình khối lẫn màu:
 *   0 = viền mảnh   1 = viền dày   2 = nền đỏ đặc chữ trắng */
static int alert_weight(treadmill_alert_t alert)
{
    switch (alert) {
    case TREADMILL_ALERT_NONE:      return 0;
    case TREADMILL_ALERT_WARNING_1: return 1;
    case TREADMILL_ALERT_WARNING_2: return 1;
    default:                        return 2;
    }
}

static void draw_alert(treadmill_alert_t alert)
{
    const char *l1, *l2;

    switch (alert) {
    case TREADMILL_ALERT_NONE:
        l1 = "AN TOAN";        l2 = NULL;            break;
    case TREADMILL_ALERT_WARNING_1:
        l1 = "CANH BAO 1";     l2 = "GIAM TOC DO";   break;
    case TREADMILL_ALERT_WARNING_2:
        l1 = "CANH BAO 2";     l2 = "SAP DUNG MAY";  break;
    case TREADMILL_ALERT_DANGER:
        l1 = "NGUY HIEM";      l2 = "DUNG NGAY";     break;
    case TREADMILL_ALERT_NO_PERSON:
        l1 = "KHONG CO NGUOI"; l2 = "TREN BAN CHAY"; break;
    case TREADMILL_ALERT_CMD_REJECTED:
        l1 = "BI TU CHOI";     l2 = "KIEM TRA MAY";  break;
    default:
        l1 = "KHONG RO";       l2 = NULL;            break;
    }

    const int     w  = alert_weight(alert);
    const uint8_t bg = (w == 2) ? LCD_RED : LCD_PAPER;
    const uint8_t fg = (w == 2) ? LCD_PAPER : LCD_INK;

    lcd_gfx_fill_rect(PAD, AL_Y, INNER_W, AL_H, bg);
    lcd_gfx_rect(PAD, AL_Y, INNER_W, AL_H, (w == 1) ? 3 : (w == 2) ? 2 : 1,
                 (w == 2) ? LCD_RED : LCD_INK);

    const int inset = 6;
    const int bx    = PAD + inset;
    const int bw    = INNER_W - 2 * inset;
    const int s1    = lcd_gfx_fit_scale(l1, bw, 3);
    const int s2    = l2 ? lcd_gfx_fit_scale(l2, bw, 2) : 0;
    const int th    = s1 * LCD_GLYPH_H + (s2 ? s2 * LCD_GLYPH_H + 2 : 0);
    int       y     = AL_Y + (AL_H - th) / 2;

    if (s1 > 0) {
        lcd_gfx_text_center(bx, bw, y, l1, s1, fg, bg);
        y += s1 * LCD_GLYPH_H + 2;
    }
    if (s2 > 0) {
        lcd_gfx_text_center(bx, bw, y, l2, s2, fg, bg);
    }
}

/* Dựng lại toàn bộ khung trong framebuffer rồi đẩy một lần. */
static void render(const lcd_status_t *st, lcd_link_state_t link)
{
    lcd_gfx_reset_dropped();
    lcd_gfx_clear(LCD_PAPER);
    draw_header(link);
    draw_state(st);
    draw_speed(st);
    draw_alert(st->alert);
    lcd_gfx_flush();

    const uint32_t dropped = lcd_gfx_dropped();
    if (dropped) {
        ESP_LOGW(TAG, "%lu glyph bi bo - co chuoi khong lot o cua no",
                 (unsigned long)dropped);
    }
}

static bool changed(const lcd_status_t *a, const lcd_status_t *b)
{
    return a->running         != b->running
        || a->control_enabled != b->control_enabled
        || a->speed           != b->speed
        || a->requested_speed != b->requested_speed
        || a->safety_limited  != b->safety_limited
        || a->zone            != b->zone
        || a->alert           != b->alert;
}

static void lcd_task(void *arg)
{
    int64_t last_change  = 0;
    int64_t last_refresh = -MIN_GAP_MS;
    bool    pending      = true;      /* vẽ khung đầu ngay khi khởi động */

    while (1) {
        lcd_status_t     st;
        lcd_link_state_t link;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        st   = s_want;
        link = s_want_link;
        xSemaphoreGive(s_lock);

        if (!s_have_shown || changed(&st, &s_shown) || link != s_shown_link) {
            pending     = true;
            last_change = now_ms();
        }

        const int64_t t = now_ms();
        if (pending
            && (!s_have_shown || t - last_change >= SETTLE_MS)
            && (t - last_refresh >= MIN_GAP_MS)) {
            render(&st, link);
            s_shown      = st;
            s_shown_link = link;
            s_have_shown = true;
            last_refresh = now_ms();
            pending      = false;
        }

        /* Có dữ liệu mới thì được đánh thức sớm; nếu không thì tự dậy để
         * kiểm tra cửa sổ gộp đã hết chưa. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(pending ? 100 : 1000));
    }
}

esp_err_t lcd_display_init(void)
{
    if (s_task) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = lcd_gfx_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd_gfx_init that bai: %s", esp_err_to_name(err));
        return err;
    }

    memset(&s_want, 0, sizeof(s_want));
    s_want.alert = TREADMILL_ALERT_NONE;
    s_want_link  = LCD_LINK_DISCONNECTED;

    if (xTaskCreate(lcd_task, "lcd_task", LCD_TASK_STACK, NULL,
                    LCD_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc lcd_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Man hinh san sang");
    return ESP_OK;
}

void lcd_display_set_link(lcd_link_state_t link)
{
    if (!s_task) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_want_link = link;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

void lcd_display_set_status(const lcd_status_t *st)
{
    if (!s_task || !st) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_want = *st;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

void lcd_display_backlight(bool on)
{
    lcd_gfx_backlight(on);
}

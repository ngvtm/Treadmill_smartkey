#include "lcd_gfx.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG      "EPD"
#define EPD_HOST SPI2_HOST

/* Lệnh SSD1680 / SSD1681. */
#define CMD_DRIVER_CTRL   0x01
#define CMD_DATA_ENTRY    0x11
#define CMD_SWRESET       0x12
#define CMD_TEMP_SENSOR   0x18
#define CMD_MASTER_ACT    0x20
#define CMD_UPDATE_CTRL1  0x21
#define CMD_UPDATE_CTRL2  0x22
#define CMD_WRITE_RAM_BW  0x24
#define CMD_BORDER        0x3C
#define CMD_RAM_X_RANGE   0x44
#define CMD_RAM_Y_RANGE   0x45
#define CMD_RAM_X_COUNT   0x4E
#define CMD_RAM_Y_COUNT   0x4F
#define CMD_DEEP_SLEEP    0x10

#define EPD_STRIDE  (EPD_PANEL_W / 8)
#define EPD_FB_SIZE (EPD_STRIDE * EPD_PANEL_H)

static esp_lcd_panel_io_handle_t s_io;
static uint8_t                  *s_fb;        /* 1 bpp, native panel layout */
static SemaphoreHandle_t         s_lock;
static uint32_t                  s_dropped;
static bool                      s_ready;
static bool                      s_asleep;

/*
 * Font 5x7, ASCII 32..90 ('space' .. 'Z'). Mỗi glyph 7 byte, mỗi byte là
 * một hàng, bit4 = cột trái nhất.
 */
#define FONT_FIRST 32
#define FONT_LAST  90

static const uint8_t s_font[FONT_LAST - FONT_FIRST + 1][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, /* ! */
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, /* " */
    {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}, /* # */
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, /* $ */
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, /* % */
    {0x08, 0x14, 0x14, 0x08, 0x15, 0x12, 0x0D}, /* & */
    {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, /* ( */
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, /* ) */
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, /* * */
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, /* + */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, /* , */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, /* . */
    {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}, /* / */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, /* : */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}, /* ; */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, /* < */
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, /* = */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, /* > */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, /* ? */
    {0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E}, /* @ */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
};

static const uint8_t *glyph_of(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c < FONT_FIRST || c > FONT_LAST) {
        c = ' ';
    }
    return s_font[(int)c - FONT_FIRST];
}

static void tx_cmd(uint8_t cmd, const uint8_t *p, size_t n)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, cmd, p, n));
}

/* Panel kéo BUSY lên cao trong lúc refresh. Không có timeout thì một tấm
 * hỏng sẽ treo cả task, nên luôn giới hạn thời gian chờ. */
static bool wait_busy(int timeout_ms)
{
    const int step = 10;
    for (int waited = 0; waited < timeout_ms; waited += step) {
        if (gpio_get_level(EPD_PIN_BUSY) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
    }
    ESP_LOGE(TAG, "BUSY khong nha sau %d ms", timeout_ms);
    return false;
}

static void hw_reset(void)
{
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    wait_busy(2000);
}

static void panel_init(void)
{
    hw_reset();

    tx_cmd(CMD_SWRESET, NULL, 0);
    wait_busy(2000);

    /* Số gate = chiều cao thật của tấm. */
    const uint8_t drv[3] = {
        (uint8_t)((EPD_PANEL_H - 1) & 0xFF),
        (uint8_t)((EPD_PANEL_H - 1) >> 8),
        0x00,
    };
    tx_cmd(CMD_DRIVER_CTRL, drv, 3);

    const uint8_t entry[1] = {0x03};        /* X tăng, Y tăng */
    tx_cmd(CMD_DATA_ENTRY, entry, 1);

    const uint8_t xr[2] = {0x00, (uint8_t)(EPD_STRIDE - 1)};
    tx_cmd(CMD_RAM_X_RANGE, xr, 2);

    const uint8_t yr[4] = {
        0x00, 0x00,
        (uint8_t)((EPD_PANEL_H - 1) & 0xFF),
        (uint8_t)((EPD_PANEL_H - 1) >> 8),
    };
    tx_cmd(CMD_RAM_Y_RANGE, yr, 4);

    const uint8_t border[1] = {0x05};
    tx_cmd(CMD_BORDER, border, 1);

    const uint8_t ctrl1[2] = {0x00, 0x80};
    tx_cmd(CMD_UPDATE_CTRL1, ctrl1, 2);

    const uint8_t temp[1] = {0x80};         /* cảm biến nhiệt nội */
    tx_cmd(CMD_TEMP_SENSOR, temp, 1);

    wait_busy(2000);
    s_asleep = false;
}

void lcd_gfx_flush(void)
{
    if (!s_ready) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_asleep) {
        panel_init();       /* ngủ sâu xoá hết thanh ghi, phải init lại */
    }

    const uint8_t x0[1] = {0x00};
    const uint8_t y0[2] = {0x00, 0x00};
    tx_cmd(CMD_RAM_X_COUNT, x0, 1);
    tx_cmd(CMD_RAM_Y_COUNT, y0, 2);

    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, CMD_WRITE_RAM_BW,
                                              s_fb, EPD_FB_SIZE));

    const uint8_t seq[1] = {0xF7};          /* refresh toàn phần */
    tx_cmd(CMD_UPDATE_CTRL2, seq, 1);
    tx_cmd(CMD_MASTER_ACT, NULL, 0);
    wait_busy(20000);                       /* tấm lớn có thể tới ~15 s */

    xSemaphoreGive(s_lock);
}

void lcd_gfx_sleep(void)
{
    if (!s_ready || s_asleep) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t mode[1] = {0x01};
    tx_cmd(CMD_DEEP_SLEEP, mode, 1);
    s_asleep = true;
    xSemaphoreGive(s_lock);
}

esp_err_t lcd_gfx_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    s_fb = heap_caps_malloc(EPD_FB_SIZE, MALLOC_CAP_DMA);
    if (!s_fb) {
        ESP_LOGE(TAG, "Khong cap phat duoc framebuffer %d byte", EPD_FB_SIZE);
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb, 0xFF, EPD_FB_SIZE);        /* 1 = trắng trong RAM SSD168x */

    gpio_config_t out_cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EPD_PIN_RST,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    gpio_config_t busy_cfg = {
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << EPD_PIN_BUSY,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&busy_cfg));

    spi_bus_config_t buscfg = {
        .sclk_io_num     = EPD_PIN_SCK,
        .mosi_io_num     = EPD_PIN_SDI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = EPD_FB_SIZE + 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(EPD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = EPD_PIN_DC,
        .cs_gpio_num       = EPD_PIN_CS,
        .pclk_hz           = EPD_SPI_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EPD_HOST,
                                             &io_cfg, &s_io));

    s_ready = true;
    panel_init();

    ESP_LOGI(TAG,
             "San sang: SSD168x %dx%d (logic %dx%d, xoay %d), fb %d byte, "
             "BUSY=%d RES=%d DC=%d CS=%d SCK=%d SDI=%d",
             EPD_PANEL_W, EPD_PANEL_H, LCD_W, LCD_H, EPD_ROTATION,
             EPD_FB_SIZE, EPD_PIN_BUSY, EPD_PIN_RST, EPD_PIN_DC,
             EPD_PIN_CS, EPD_PIN_SCK, EPD_PIN_SDI);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Vẽ vào framebuffer. Toạ độ logic -> toạ độ panel qua phép xoay.     */
/* ------------------------------------------------------------------ */

static inline void put_px(int x, int y, uint8_t ink)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) {
        return;
    }
#if EPD_ROTATION
    const int px = y;
    const int py = EPD_PANEL_H - 1 - x;
#else
    const int px = x;
    const int py = y;
#endif
    uint8_t *b = &s_fb[py * EPD_STRIDE + (px >> 3)];
    const uint8_t mask = (uint8_t)(0x80 >> (px & 7));

    /* RAM của SSD168x: bit 1 = trắng, bit 0 = đen. */
    if (ink) {
        *b = (uint8_t)(*b & ~mask);
    } else {
        *b = (uint8_t)(*b | mask);
    }
}

void lcd_gfx_clear(uint8_t color)
{
    if (!s_ready) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_fb, color == LCD_INK ? 0x00 : 0xFF, EPD_FB_SIZE);
    xSemaphoreGive(s_lock);
}

void lcd_gfx_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    if (!s_ready) {
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) { w = LCD_W - x; }
    if (y + h > LCD_H) { h = LCD_H - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            put_px(x + i, y + j, color);
        }
    }
    xSemaphoreGive(s_lock);
}

void lcd_gfx_rect(int x, int y, int w, int h, int t, uint8_t color)
{
    if (t < 1) {
        t = 1;
    }
    lcd_gfx_fill_rect(x, y, w, t, color);
    lcd_gfx_fill_rect(x, y + h - t, w, t, color);
    lcd_gfx_fill_rect(x, y, t, h, color);
    lcd_gfx_fill_rect(x + w - t, y, t, h, color);
}

int lcd_gfx_text_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    if (n <= 0) {
        return 0;
    }
    /* Bỏ cột cách của ký tự cuối để chuỗi canh giữa cho cân. */
    return n * LCD_GLYPH_W * scale - scale;
}

int lcd_gfx_fit_scale(const char *s, int w, int max_scale)
{
    for (int sc = max_scale; sc >= 1; sc--) {
        if (lcd_gfx_text_width(s, sc) <= w) {
            return sc;
        }
    }
    return 0;
}

static void draw_char(int x, int y, char c, int scale, uint8_t fg, uint8_t bg)
{
    const int cw = LCD_GLYPH_W * scale;
    const int ch = LCD_GLYPH_H * scale;

    if (x < 0 || y < 0 || x + cw > LCD_W || y + ch > LCD_H) {
        /* Bỏ cả glyph thay vì cắt dở. Đếm lại để lỗi bố cục lộ ra ở log
         * chứ không chỉ là chữ bị mất trên màn. */
        s_dropped++;
        ESP_LOGW(TAG, "Bo glyph '%c' tai (%d,%d) scale %d: khong lot man",
                 c, x, y, scale);
        return;
    }

    const uint8_t *g = glyph_of(c);
    for (int row = 0; row < ch; row++) {
        const int gy = row / scale;                   /* 0..7 */
        const uint8_t bits = (gy < 7) ? g[gy] : 0;    /* hàng 8 là khoảng cách */
        for (int col = 0; col < cw; col++) {
            const int gx = col / scale;               /* 0..5 */
            const bool on = (gx < 5) && (bits & (0x10 >> gx));
            put_px(x + col, y + row, on ? fg : bg);
        }
    }
}

void lcd_gfx_text(int x, int y, const char *s, int scale,
                  uint8_t fg, uint8_t bg)
{
    if (!s_ready || !s || scale < 1) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (const char *p = s; *p; p++) {
        draw_char(x, y, *p, scale, fg, bg);
        x += LCD_GLYPH_W * scale;
    }
    xSemaphoreGive(s_lock);
}

void lcd_gfx_text_center(int x0, int w, int y, const char *s, int scale,
                         uint8_t fg, uint8_t bg)
{
    lcd_gfx_text(x0 + (w - lcd_gfx_text_width(s, scale)) / 2, y, s, scale, fg, bg);
}

void lcd_gfx_text_box(int x, int y, int w, int h, const char *s, int scale,
                      uint8_t fg, uint8_t bg)
{
    lcd_gfx_fill_rect(x, y, w, h, bg);
    lcd_gfx_text_center(x, w, y + (h - LCD_GLYPH_H * scale) / 2, s, scale, fg, bg);
}

uint32_t lcd_gfx_dropped(void)
{
    return s_dropped;
}

void lcd_gfx_reset_dropped(void)
{
    s_dropped = 0;
}

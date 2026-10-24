#include "lcd_gfx.h"

#include <string.h>

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG      "TFT"
#define TFT_HOST SPI2_HOST

/* Chon driver IC: menuconfig (CONFIG_TFT_PANEL_ST7789) hoac mac dinh. */
#if defined(CONFIG_TFT_PANEL_ST7789)
#define PANEL_ST7789 1
#else
#define PANEL_ST7789 0
#endif

#if defined(CONFIG_TFT_SPI_HZ)
#define TFT_SPI_HZ CONFIG_TFT_SPI_HZ
#else
#define TFT_SPI_HZ (40 * 1000 * 1000)
#endif

/* Lenh chung cho ca ILI9341 va ST7789. */
#define CMD_SWRESET 0x01
#define CMD_SLPIN   0x10
#define CMD_SLPOUT  0x11
#define CMD_DISPOFF 0x28
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C
#define CMD_MADCTL  0x36
#define CMD_COLMOD  0x3A
#define CMD_INVON   0x21

/* Palette 2 bpp -> RGB565. Thu tu khop LCD_PAPER/INK/RED/GREEN. */
static const uint16_t s_palette[4] = {
    0xFFFF, /* PAPER: trang */
    0x0000, /* INK:   den */
    0xF800, /* RED */
    0x07E0, /* GREEN */
};

#define FB_STRIDE  (LCD_W / 4)          /* 2 bpp: 4 pixel/byte */
#define FB_SIZE    (FB_STRIDE * LCD_H)
#define BAND_ROWS  16
/* Kinh tren module nay gan lech 90 do so voi chieu quet mac dinh:
 * framebuffer portrait 240x320 chi phu 240/320 pixel chieu dai cua
 * kinh -> 80 pixel (1/4 man) khong duoc ghi nen nhieu hat, phan con
 * lai bi cat. Sua bang cach xoay dau ra 90 do ngay luc flush: anh
 * landscape 320x240 + MADCTL MV. App, layout va touch giu nguyen
 * portrait; phep quay nam hoan toan o flush. Hieu chuan touch 3 diem
 * (affine) tu hap thu phep quay nen khong can sua touch.
 * NEU ANH XOAY NGUOC DAU (chu dung nhung lon nguoc): doi cap
 * MADCTL o bang init (ILI9341 0x28 <-> 0xE8, ST7789 0x20 <-> 0xE0)
 * VA doi anh xa transpose o flush (xem comment o do). */
#define BAND_BYTES (PANEL_W * BAND_ROWS * 2)

static esp_lcd_panel_io_handle_t s_io;
static uint8_t                  *s_fb;        /* palette 2 bpp */
static uint8_t                  *s_band;      /* RGB565 DMA, tung dai */
static SemaphoreHandle_t         s_lock;
static uint32_t                  s_dropped;
static bool                      s_ready;
static bool                      s_asleep;

/*
 * Font 5x7, ASCII 32..90 ('space' .. 'Z'). Moi glyph 7 byte, moi byte
 * la mot hang, bit4 = cot trai nhat.
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

/* Mot buoc trong bang init: lenh + toi da 16 byte tham so + tre sau. */
typedef struct {
    uint8_t        cmd;
    uint8_t        len;
    uint8_t        delay_ms;
    const uint8_t *data;
} init_step_t;

#define STEP(c, ...)        { (c), (uint8_t)sizeof((uint8_t[]){__VA_ARGS__}), 0, (const uint8_t[]){__VA_ARGS__} }
#define STEP_D(c, dly, ...) { (c), (uint8_t)sizeof((uint8_t[]){__VA_ARGS__}), (dly), (const uint8_t[]){__VA_ARGS__} }
#define STEP0(c, dly)       { (c), 0, (dly), NULL }

#if PANEL_ST7789
static const init_step_t s_init_seq[] = {
    STEP0(CMD_SWRESET, 150),
    STEP0(CMD_SLPOUT, 120),
    STEP(CMD_MADCTL, 0x20),                 /* MV, RGB: landscape (xoay 90) */
    STEP(CMD_COLMOD, 0x55),                 /* RGB565 */
    STEP(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33), /* porch */
    STEP(0xB7, 0x35),                       /* gate control */
    STEP(0xBB, 0x19),                       /* VCOM */
    STEP(0xC0, 0x2C),                       /* LCM control */
    STEP(0xC2, 0x01),                       /* VDV/VRH */
    STEP(0xC3, 0x12),                       /* VRH set */
    STEP(0xC4, 0x20),                       /* VDV set */
    STEP(0xC6, 0x0F),                       /* frame rate 60 Hz */
    STEP(0xD0, 0xA4, 0xA1),                 /* power control */
    STEP(0xE0, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
               0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23), /* gamma + */
    STEP(0xE1, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
               0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23), /* gamma - */
    STEP0(CMD_INVON, 10),                   /* tam ST7789 thuong dao mau mac dinh */
    STEP0(CMD_DISPON, 20),
};
#else
static const init_step_t s_init_seq[] = {
    STEP0(CMD_SWRESET, 120),
    STEP(0xCF, 0x00, 0xC1, 0x30),           /* power control B */
    STEP(0xED, 0x64, 0x03, 0x12, 0x81),     /* power on sequence */
    STEP(0xE8, 0x85, 0x00, 0x78),           /* driver timing A */
    STEP(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02), /* power control A */
    STEP(0xF7, 0x20),                       /* pump ratio */
    STEP(0xEA, 0x00, 0x00),                 /* driver timing B */
    STEP(0xC0, 0x23),                       /* PWCTRL1 */
    STEP(0xC1, 0x10),                       /* PWCTRL2 */
    STEP(0xC5, 0x3E, 0x28),                 /* VMCTRL1 */
    STEP(0xC7, 0x86),                       /* VMCTRL2 */
    STEP(CMD_MADCTL, 0x28),                 /* MV + BGR: landscape (xoay 90) */
    STEP(CMD_COLMOD, 0x55),                 /* RGB565 */
    STEP(0xB1, 0x00, 0x18),                 /* frame rate 79 Hz */
    STEP(0xB6, 0x08, 0x82, 0x27),           /* display function */
    STEP(0xF2, 0x00),                       /* gamma 3G off */
    STEP(0x26, 0x01),                       /* gamma curve 1 */
    STEP(0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E,
               0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00), /* gamma + */
    STEP(0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31,
               0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F), /* gamma - */
    STEP0(CMD_SLPOUT, 120),
    STEP0(CMD_DISPON, 20),
};
#endif

static void hw_reset(void)
{
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void panel_init(void)
{
    hw_reset();
    for (size_t i = 0; i < sizeof(s_init_seq) / sizeof(s_init_seq[0]); i++) {
        const init_step_t *s = &s_init_seq[i];
        tx_cmd(s->cmd, s->data, s->len);
        if (s->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(s->delay_ms));
        }
    }
    s_asleep = false;
}

static void set_window(int x0, int y0, int x1, int y1)
{
    const uint8_t xs[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1,
    };
    const uint8_t ys[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1,
    };
    tx_cmd(CMD_CASET, xs, 4);
    tx_cmd(CMD_RASET, ys, 4);
}

void lcd_gfx_flush(void)
{
    if (!s_ready) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_asleep) {
        panel_init();       /* sleep xoa trang thai hien thi, init lai */
    }

    /* Flush xoay 90 do: pixel panel (px, py) lay tu pixel framebuffer
     * (x = py, y = LCD_H - 1 - px). Day la phep quay CW cua anh portrait
     * thanh anh landscape 320x240. De dao huong quay (neu anh bi nguoc
     * dau), doi anh xa thanh (x = LCD_W - 1 - py, y = px) va doi MADCTL
     * o bang init (ILI9341 0x28 -> 0xE8, ST7789 0x20 -> 0xE0). */
    for (int py0 = 0; py0 < PANEL_H; py0 += BAND_ROWS) {
        const int rows = (PANEL_H - py0 < BAND_ROWS) ? (PANEL_H - py0) : BAND_ROWS;

        /* No palette 2 bpp -> RGB565 big-endian cho dai nay. */
        for (int r = 0; r < rows; r++) {
            const int py = py0 + r;
            uint8_t  *dst = &s_band[r * PANEL_W * 2];
            for (int px = 0; px < PANEL_W; px++) {
                const int fb_x = py;
                const int fb_y = LCD_H - 1 - px;
                const uint8_t *src = &s_fb[fb_y * FB_STRIDE];
                const uint8_t packed = src[fb_x >> 2];
                const int     idx    = (packed >> (6 - 2 * (fb_x & 3))) & 3;
                const uint16_t rgb  = s_palette[idx];
                dst[2 * px]     = (uint8_t)(rgb >> 8);
                dst[2 * px + 1] = (uint8_t)(rgb & 0xFF);
            }
        }

        set_window(0, py0, PANEL_W - 1, py0 + rows - 1);
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(
            s_io, CMD_RAMWR, s_band, (size_t)PANEL_W * rows * 2));
    }

    xSemaphoreGive(s_lock);
}

void lcd_gfx_sleep(void)
{
    if (!s_ready || s_asleep) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    tx_cmd(CMD_DISPOFF, NULL, 0);
    tx_cmd(CMD_SLPIN, NULL, 0);
    s_asleep = true;
    xSemaphoreGive(s_lock);
}

void lcd_gfx_backlight(bool on)
{
    gpio_set_level(TFT_PIN_BL, on ? 1 : 0);
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

    s_fb = heap_caps_malloc(FB_SIZE, MALLOC_CAP_8BIT);
    if (!s_fb) {
        ESP_LOGE(TAG, "Khong cap phat duoc framebuffer %d byte", FB_SIZE);
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb, 0x00, FB_SIZE);            /* palette 0 = PAPER */

    s_band = heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA);
    if (!s_band) {
        ESP_LOGE(TAG, "Khong cap phat duoc band buffer %d byte", BAND_BYTES);
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t out_cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_PIN_RST) | (1ULL << TFT_PIN_BL),
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    gpio_set_level(TFT_PIN_BL, 0);          /* den nen bat sau khi init xong */

    spi_bus_config_t buscfg = {
        .sclk_io_num     = TFT_PIN_SCK,
        .mosi_io_num     = TFT_PIN_SDI,
        .miso_io_num     = TFT_PIN_TOUCH_MISO,  // Phase 2: touch shares the bus
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = BAND_BYTES + 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = TFT_PIN_DC,
        .cs_gpio_num       = TFT_PIN_CS,
        .pclk_hz           = TFT_SPI_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_HOST,
                                             &io_cfg, &s_io));

    s_ready = true;
    panel_init();
    gpio_set_level(TFT_PIN_BL, 1);

    ESP_LOGI(TAG,
             "San sang: TFT %s %dx%d, fb 2bpp %d byte, SPI %d Hz, "
             "BL=%d RES=%d DC=%d CS=%d SCK=%d SDI=%d",
             PANEL_ST7789 ? "ST7789" : "ILI9341", LCD_W, LCD_H,
             FB_SIZE, TFT_SPI_HZ, TFT_PIN_BL, TFT_PIN_RST, TFT_PIN_DC,
             TFT_PIN_CS, TFT_PIN_SCK, TFT_PIN_SDI);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Ve vao framebuffer palette 2 bpp.                                   */
/* ------------------------------------------------------------------ */

static inline void put_px(int x, int y, uint8_t color)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) {
        return;
    }
    uint8_t      *b     = &s_fb[y * FB_STRIDE + (x >> 2)];
    const uint8_t shift = (uint8_t)(6 - 2 * (x & 3));
    const uint8_t mask  = (uint8_t)(3u << shift);
    *b = (uint8_t)((*b & ~mask) | ((uint8_t)(color & 3) << shift));
}

void lcd_gfx_clear(uint8_t color)
{
    if (!s_ready) {
        return;
    }
    const uint8_t c = (uint8_t)(color & 3);
    const uint8_t packed = (uint8_t)(c | (c << 2) | (c << 4) | (c << 6));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_fb, packed, FB_SIZE);
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
        s_dropped++;
        ESP_LOGW(TAG, "Bo glyph '%c' tai (%d,%d) scale %d: khong lot man",
                 c, x, y, scale);
        return;
    }

    const uint8_t *g = glyph_of(c);
    for (int row = 0; row < ch; row++) {
        const int gy = row / scale;
        const uint8_t bits = (gy < 7) ? g[gy] : 0;
        for (int col = 0; col < cw; col++) {
            const int gx = col / scale;
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

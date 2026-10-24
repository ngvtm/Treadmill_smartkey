/*
 * Test lop ve TFT: bang font, hinh hoc chu, cat bien, palette 2 bpp ->
 * RGB565, va chuoi init ILI9341/ST7789.
 */
#include "fake_panel.h"
#include "lcd_gfx.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "freertos/semphr.h"

/* Moi chuoi lcd_display.c thuc su ve ra man. */
static const char *const kUiStrings[] = {
    "SMARTKEY", "TRANG THAI", "MUC TOC DO",
    "DA KET NOI", "DANG TIM", "MAT KET NOI",
    "DANG CHAY", "DA DUNG", "DIEU KHIEN: BAT", "DIEU KHIEN: TAT",
    "AN TOAN", "CANH BAO 1", "GIAM TOC DO", "CANH BAO 2", "SAP DUNG MAY",
    "NGUY HIEM", "DUNG NGAY", "KHONG CO NGUOI", "TREN BAN CHAY",
    "BI TU CHOI", "KIEM TRA MAY", "KHONG RO",
    "KHOA AN TOAN", "DA BI BUT RA",
    "! YC 255  VUNG 255", "YC 10  VUNG 3", "--", "0", "10",
};
#define UI_COUNT (int)(sizeof(kUiStrings) / sizeof(kUiStrings[0]))

TEST_MAIN_BEGIN("lcd_gfx (TFT)")

    CHECK_EQ(lcd_gfx_init(), ESP_OK, "lcd_gfx_init");

    /* ---------------------------------------------------------------- */
    TEST("Chuoi init TFT dung: reset, RGB565, bat man");
    CHECK(fp_saw_swreset, "thieu SWRESET (0x01)");
    CHECK_EQ(fp_colmod, 0x55, "COLMOD phai la RGB565 (0x55)");
    CHECK_EQ(fp_madctl, 0x48, "MADCTL ILI9341 mac dinh (MX+BGR)");
    CHECK(fp_saw_slpout >= 1, "thieu SLPOUT (thoat sleep)");
    CHECK(fp_saw_dispon >= 1, "thieu DISPON (bat hien thi)");
    CHECK_EQ(fake_spi_bus_cfg.sclk_io_num, TFT_PIN_SCK, "SCK");
    CHECK_EQ(fake_spi_bus_cfg.mosi_io_num, TFT_PIN_SDI, "SDI -> MOSI");
    CHECK_EQ(fake_spi_bus_cfg.miso_io_num, -1, "khong doc nguoc tu panel");
    CHECK_EQ(fake_io_cfg.dc_gpio_num, TFT_PIN_DC, "D/C");
    CHECK_EQ(fake_io_cfg.cs_gpio_num, TFT_PIN_CS, "CS");
    CHECK_EQ(fake_io_cfg.lcd_cmd_bits, 8, "lenh 8 bit");
    CHECK_EQ(fake_io_cfg.lcd_param_bits, 8, "tham so 8 bit");
    CHECK_EQ(fake_io_cfg.pclk_hz, 40 * 1000 * 1000, "SPI TFT mac dinh 40 MHz");
    CHECK(fake_spi_bus_cfg.max_transfer_sz >= (size_t)(LCD_W * 16 * 2),
          "transfer size %d phai chua mot dai RGB565",
          (int)fake_spi_bus_cfg.max_transfer_sz);
    CHECK((fake_gpio_configured_mask & (1ULL << TFT_PIN_BL)) != 0,
          "phai cau hinh chan den nen BL");
    CHECK((fake_gpio_configured_mask & (1ULL << TFT_PIN_RST)) != 0,
          "phai cau hinh chan RES");
    CHECK_EQ(fake_gpio_level[TFT_PIN_BL], 1, "den nen phai bat sau init");

    /* ---------------------------------------------------------------- */
    TEST("Moi chan deu xuat duoc va khong trung nhau");
    {
        const struct { const char *name; int pin; } pins[] = {
            {"BL",  TFT_PIN_BL},  {"RES", TFT_PIN_RST},
            {"D/C", TFT_PIN_DC},  {"CS",  TFT_PIN_CS},
            {"SCK", TFT_PIN_SCK}, {"SDI", TFT_PIN_SDI},
        };
        const int n = (int)(sizeof(pins) / sizeof(pins[0]));
        const int btn[] = {21, 22, 23};

        for (int i = 0; i < n; i++) {
            const int pin = pins[i].pin;
            CHECK(pin >= 0 && pin <= 33,
                  "%s = GPIO%d vuot SOC_GPIO_OUT_RANGE_MAX", pins[i].name, pin);
            CHECK(!(pin >= 6 && pin <= 11),
                  "%s = GPIO%d thuoc vung flash SPI noi", pins[i].name, pin);
            CHECK(pin != 1 && pin != 3,
                  "%s = GPIO%d dung UART0, mat monitor", pins[i].name, pin);
            CHECK(pin != 0 && pin != 12,
                  "%s = GPIO%d la strapping pin nguy hiem luc boot",
                  pins[i].name, pin);
            for (int b = 0; b < 3; b++) {
                CHECK(pin != btn[b], "%s = GPIO%d trung nut bam",
                      pins[i].name, pin);
            }
            for (int j = i + 1; j < n; j++) {
                CHECK(pin != pins[j].pin, "%s va %s cung dung GPIO%d",
                      pins[i].name, pins[j].name, pin);
            }
        }
    }

    /* ---------------------------------------------------------------- */
    TEST("Flush day du mot khung RGB565, palette dung");
    {
        fp_reset();
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_flush();
        CHECK_EQ(fp_frames, 1, "flush phai gui dung mot khung");
        CHECK_EQ(fp_ramwr, LCD_H / 16, "khung chia thanh cac dai 16 dong");
        CHECK_EQ(fp_bytes, LCD_W * LCD_H * 2,
                 "kich thuoc anh RGB565 gui di");
        CHECK_EQ(fp_oob, 0, "ghi sai kich thuoc/cua so");
        CHECK(fp_uniform(0, 0, LCD_W, LCD_H, LCD_PAPER), "clear trang");

        lcd_gfx_clear(LCD_INK);
        lcd_gfx_flush();
        CHECK(fp_uniform(0, 0, LCD_W, LCD_H, LCD_INK), "clear den");

        lcd_gfx_clear(LCD_RED);
        lcd_gfx_flush();
        CHECK(fp_uniform(0, 0, LCD_W, LCD_H, LCD_RED), "clear do (palette)");

        lcd_gfx_clear(LCD_GREEN);
        lcd_gfx_flush();
        CHECK(fp_uniform(0, 0, LCD_W, LCD_H, LCD_GREEN), "clear xanh la (palette)");
    }

    /* ---------------------------------------------------------------- */
    TEST("Sleep tat man, flush danh thuc va init lai");
    {
        const int dispon_before = fp_saw_dispon;
        lcd_gfx_sleep();
        CHECK(fp_saw_dispoff >= 1, "sleep phai gui DISPOFF");
        CHECK(fp_saw_slpin >= 1, "sleep phai gui SLPIN");
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_flush();
        CHECK(fp_saw_dispon > dispon_before,
              "flush sau sleep phai init lai panel (DISPON lai)");
    }

    /* ---------------------------------------------------------------- */
    TEST("Bang font hop le");
    {
        int bad_rows = 0, blank_glyphs = 0;
        for (int c = 32; c <= 90; c++) {
            lcd_gfx_clear(LCD_PAPER);
            lcd_gfx_text(0, 0, (char[]){(char)c, 0}, 1, LCD_INK, LCD_PAPER);
            lcd_gfx_flush();
            const int ink = fp_ink(0, 0, LCD_GLYPH_W, LCD_GLYPH_H);
            if (ink > 5 * 7) {
                bad_rows++;
            }
            if (c != ' ' && ink == 0) {
                blank_glyphs++;
                printf("    glyph trong: '%c' (0x%02X)\n", c, c);
            }
        }
        CHECK_EQ(bad_rows, 0, "co glyph tran ra ngoai o 5x7");
        CHECK_EQ(blank_glyphs, 0, "co glyph khac space bi rong");
    }

    /* ---------------------------------------------------------------- */
    TEST("Moi ky tu trong chuoi UI deu co glyph");
    {
        int missing = 0;
        for (int s = 0; s < UI_COUNT; s++) {
            for (const char *p = kUiStrings[s]; *p; p++) {
                char c = *p;
                if (c >= 'a' && c <= 'z') {
                    c = (char)(c - 'a' + 'A');
                }
                if (c != ' ' && (c < 32 || c > 90)) {
                    missing++;
                    printf("    '%c' trong \"%s\" ngoai bang font\n",
                           *p, kUiStrings[s]);
                }
            }
        }
        CHECK_EQ(missing, 0, "chuoi UI dung ky tu khong ve duoc");
    }

    /* ---------------------------------------------------------------- */
    TEST("Chu thuong doi thanh in hoa");
    {
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text(0, 0, "a", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        uint8_t lower[LCD_GLYPH_H * 2][LCD_GLYPH_W * 2];
        for (int y = 0; y < LCD_GLYPH_H * 2; y++) {
            memcpy(lower[y], &fp_fb[y][0], LCD_GLYPH_W * 2);
        }

        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text(0, 0, "A", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        int diff = 0;
        for (int y = 0; y < LCD_GLYPH_H * 2; y++) {
            for (int x = 0; x < LCD_GLYPH_W * 2; x++) {
                if (lower[y][x] != fp_fb[y][x]) {
                    diff++;
                }
            }
        }
        CHECK_EQ(diff, 0, "'a' va 'A' phai ve giong nhau");
    }

    /* ---------------------------------------------------------------- */
    TEST("Ky tu ngoai bang ve thanh khoang trang");
    {
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text(0, 0, "\x01~{|}\x7F", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        CHECK_EQ(fp_ink(0, 0, LCD_GLYPH_W * 2 * 6, LCD_GLYPH_H * 2), 0,
                 "ky tu la phai ra o trong");
    }

    /* ---------------------------------------------------------------- */
    TEST("lcd_gfx_text_width va fit_scale");

    CHECK_EQ(lcd_gfx_text_width("", 3), 0, "chuoi rong");
    CHECK_EQ(lcd_gfx_text_width("A", 1), LCD_GLYPH_W - 1, "1 ky tu scale 1");
    CHECK_EQ(lcd_gfx_text_width("ABC", 2), 3 * LCD_GLYPH_W * 2 - 2,
             "3 ky tu scale 2");
    CHECK_EQ(lcd_gfx_fit_scale("AB", 5, 4), 0,
             "khong lot noi thi phai tra 0, khong tra 1");
    CHECK_EQ(lcd_gfx_fit_scale("A", 1000, 4), 4, "lot thoai mai thi lay max");
    {
        const int sc = lcd_gfx_fit_scale("DANG CHAY", LCD_W - 6, 3);
        CHECK(sc >= 1, "chuoi trang thai phai lot o be rong man");
        CHECK(lcd_gfx_text_width("DANG CHAY", sc) <= LCD_W - 6,
              "fit_scale tra ve co qua lon");
    }

    /* ---------------------------------------------------------------- */
    TEST("fill_rect cat bien, khong ghi ra ngoai framebuffer");
    {
        lcd_gfx_clear(LCD_PAPER);
        fp_reset();

        lcd_gfx_fill_rect(-20, -30, 60, 60, LCD_INK);
        lcd_gfx_fill_rect(LCD_W - 10, LCD_H - 10, 100, 100, LCD_INK);
        lcd_gfx_fill_rect(-100, -100, 50, 50, LCD_INK);   /* hoan toan ngoai */
        lcd_gfx_fill_rect(0, 0, 0, 10, LCD_INK);          /* rong bang 0 */
        lcd_gfx_flush();

        CHECK_EQ(fp_oob, 0, "co ghi ra ngoai khung");
        CHECK(fp_uniform(0, 0, 40, 30, LCD_INK), "phan trong khung phai to");
        CHECK(fp_uniform(LCD_W - 10, LCD_H - 10, 10, 10, LCD_INK),
              "goc phai duoi phai to");
    }

    /* ---------------------------------------------------------------- */
    TEST("Vien rect dung do day, rong o giua");
    {
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_rect(10, 10, 40, 30, 2, LCD_INK);
        lcd_gfx_flush();
        CHECK(fp_uniform(10, 10, 40, 2, LCD_INK), "canh tren");
        CHECK(fp_uniform(10, 38, 40, 2, LCD_INK), "canh duoi");
        CHECK(fp_uniform(10, 10, 2, 30, LCD_INK), "canh trai");
        CHECK(fp_uniform(48, 10, 2, 30, LCD_INK), "canh phai");
        CHECK(fp_uniform(12, 12, 36, 26, LCD_PAPER), "long phai rong");
    }

    /* ---------------------------------------------------------------- */
    TEST("Chu ve dac: gia tri cu bi xoa han");
    {
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text_box(0, 40, LCD_W, 20, "0", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        const int alone = fp_ink(0, 40, LCD_W, 20);
        CHECK(alone > 0, "chuoi ngan phai co muc");

        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text_box(0, 40, LCD_W, 20, "CANH BAO 1", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        CHECK(fp_ink(0, 40, LCD_W, 20) > alone, "chuoi dai phai nhieu muc hon");

        lcd_gfx_text_box(0, 40, LCD_W, 20, "0", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        CHECK_EQ(fp_ink(0, 40, LCD_W, 20), alone,
                 "con sot lai vet cua chuoi dai");
    }

    /* ---------------------------------------------------------------- */
    TEST("Chu dao mau: muc trang tren nen den");
    {
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text_box(0, 40, LCD_W, 20, "AN TOAN", 2, LCD_PAPER, LCD_INK);
        lcd_gfx_flush();
        const int ink   = fp_ink(0, 40, LCD_W, 20);
        const int paper = fp_count(0, 40, LCD_W, 20, LCD_PAPER);
        CHECK(ink > paper, "nen dao phai chu yeu la muc den");
        CHECK(paper > 0, "chu trang phai hien ra tren nen den");
    }

    /* ---------------------------------------------------------------- */
    TEST("Chu vuot bien bi bo va duoc dem lai");
    {
        lcd_gfx_reset_dropped();
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_text(LCD_W - LCD_GLYPH_W * 2, 0, "AB", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_flush();
        CHECK_EQ(lcd_gfx_dropped(), 1, "glyph thu hai phai bi bo va dem lai");
        CHECK_EQ(fp_oob, 0, "khong duoc ghi ra ngoai framebuffer");
    }

    /* ---------------------------------------------------------------- */
    TEST("Khoa mutex khong long nhau");
    {
        fake_mutex_reset();
        lcd_gfx_text_box(0, 60, LCD_W, 20, "AN TOAN", 2, LCD_INK, LCD_PAPER);
        lcd_gfx_rect(0, 0, 20, 20, 1, LCD_INK);
        lcd_gfx_flush();
        CHECK(fake_mutex_max_depth <= 1,
              "mutex bi lay long nhau (depth=%d) -> deadlock tren target",
              fake_mutex_max_depth);
        CHECK_EQ(fake_mutex_imbalance, 0, "so lan give nhieu hon take");
    }

TEST_MAIN_END()

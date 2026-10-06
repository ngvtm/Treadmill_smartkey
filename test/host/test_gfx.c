/*
 * Test lop ve e-paper: bang font, hinh hoc chu, cat bien, dong goi 1 bpp,
 * va chuoi init SSD1680.
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
    "! YC 255  VUNG 255", "YC 10  VUNG 3", "--", "0", "10",
};
#define UI_COUNT (int)(sizeof(kUiStrings) / sizeof(kUiStrings[0]))

TEST_MAIN_BEGIN("lcd_gfx (e-paper)")

    CHECK_EQ(lcd_gfx_init(), ESP_OK, "lcd_gfx_init");

    /* ---------------------------------------------------------------- */
    TEST("Chuoi init SSD1680 dung tham so theo kich thuoc tam");

    CHECK(fp_saw_swreset, "thieu SWRESET (0x12)");
    CHECK_EQ(fp_driver_mux, EPD_PANEL_H - 1,
             "so gate phai bang chieu cao tam - 1");
    CHECK_EQ(fp_data_entry, 0x03, "data entry phai la X tang / Y tang");
    CHECK_EQ(fp_ram_x_end, EPD_PANEL_W / 8 - 1,
             "gioi han X tinh theo byte, khong phai pixel");
    CHECK_EQ(fp_ram_y_end, EPD_PANEL_H - 1, "gioi han Y");

    /* ---------------------------------------------------------------- */
    TEST("Cau hinh SPI va chan khop DESPI-C02");

    CHECK_EQ(fake_spi_bus_cfg.sclk_io_num, EPD_PIN_SCK, "SCK");
    CHECK_EQ(fake_spi_bus_cfg.mosi_io_num, EPD_PIN_SDI, "SDI -> MOSI");
    CHECK_EQ(fake_spi_bus_cfg.miso_io_num, -1, "khong doc nguoc tu panel");
    CHECK_EQ(fake_io_cfg.dc_gpio_num, EPD_PIN_DC, "D/C");
    CHECK_EQ(fake_io_cfg.cs_gpio_num, EPD_PIN_CS, "CS");
    CHECK_EQ(fake_io_cfg.lcd_cmd_bits, 8, "lenh 8 bit");
    CHECK_EQ(fake_io_cfg.lcd_param_bits, 8, "tham so 8 bit");
    CHECK(fake_spi_bus_cfg.max_transfer_sz >= (size_t)(EPD_PANEL_W / 8) * EPD_PANEL_H,
          "max_transfer_sz phai chua het framebuffer trong mot lan");
    CHECK((fake_gpio_configured_mask & (1ULL << EPD_PIN_BUSY)) != 0,
          "chan BUSY phai duoc gpio_config");
    CHECK((fake_gpio_configured_mask & (1ULL << EPD_PIN_RST)) != 0,
          "chan RES phai duoc gpio_config");

    /* ---------------------------------------------------------------- */
    TEST("Moi chan deu xuat duoc va khong trung nhau");
    {
        const struct { const char *name; int pin; } pins[] = {
            {"BUSY", EPD_PIN_BUSY}, {"RES", EPD_PIN_RST},
            {"D/C",  EPD_PIN_DC},   {"CS",  EPD_PIN_CS},
            {"SCK",  EPD_PIN_SCK},  {"SDI", EPD_PIN_SDI},
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
    TEST("Dong goi 1 bpp: bit 1 la trang, bit 0 la den");
    {
        fp_reset();
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_flush();
        CHECK_EQ(fp_flushes, 1, "flush phai gui dung mot lan ghi RAM");
        CHECK_EQ(fp_bytes, (EPD_PANEL_W / 8) * EPD_PANEL_H,
                 "kich thuoc anh gui di");
        CHECK_EQ(fp_oob, 0, "ghi sai kich thuoc");
        CHECK(fp_uniform(0, 0, LCD_W, LCD_H, LCD_PAPER), "clear trang");

        lcd_gfx_clear(LCD_INK);
        lcd_gfx_flush();
        CHECK(fp_uniform(0, 0, LCD_W, LCD_H, LCD_INK), "clear den");
    }

    /* ---------------------------------------------------------------- */
    TEST("Kich hoat refresh sau moi lan flush");
    {
        const int before = fp_saw_master_act;
        lcd_gfx_clear(LCD_PAPER);
        lcd_gfx_flush();
        CHECK_EQ(fp_saw_master_act, before + 1,
                 "thieu MASTER_ACTIVATION -> anh nam trong RAM, man khong doi");
        CHECK_EQ(fp_update_seq, 0xF7, "chuoi refresh toan phan");
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

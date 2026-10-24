/*
 * Test tang hien thi TFT. Nap thang lcd_display.c de cham duoc vao
 * render() va cac hang so bo cuc (deu la static/#define trong file do).
 */
#include "fake_panel.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"

#include "lcd_display.c"

/* Ranh gioi bon dai de kiem chong lan va tran man. */
typedef struct {
    const char *name;
    int y, h;
} band_t;

static const band_t kBands[] = {
    {"header", 0,    HDR_H},
    {"state",  ST_Y, ST_H},
    {"speed",  SP_Y, SP_H},
    {"alert",  AL_Y, AL_H},
};
#define BAND_COUNT (int)(sizeof(kBands) / sizeof(kBands[0]))

static const treadmill_alert_t kAlerts[] = {
    TREADMILL_ALERT_NONE, TREADMILL_ALERT_WARNING_1,
    TREADMILL_ALERT_WARNING_2, TREADMILL_ALERT_DANGER,
    TREADMILL_ALERT_NO_PERSON, TREADMILL_ALERT_CMD_REJECTED,
    TREADMILL_ALERT_SAFETY_KEY_OUT, TREADMILL_ALERT_MOTOR_FAULT,
};

/* So pixel khong phai nen trang trong dai canh bao: dai nghiem trong
 * gio nen do chu trang nen dem "muc den" khong con dung nua. */
static int alert_filled(void)
{
    return INNER_W * AL_H - fp_count(PAD, AL_Y, INNER_W, AL_H, LCD_PAPER);
}
#define ALERT_COUNT (int)(sizeof(kAlerts) / sizeof(kAlerts[0]))

static unsigned band_hash(const band_t *b)
{
    unsigned h = 2166136261u;
    for (int y = b->y; y < b->y + b->h && y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            h = (h ^ fp_fb[y][x]) * 16777619u;
        }
    }
    return h;
}

TEST_MAIN_BEGIN("lcd_display (TFT)")

    CHECK_EQ(lcd_display_init(), ESP_OK, "lcd_display_init");
    CHECK(fake_task_fn != NULL, "phai tao task ve");

    /* ---------------------------------------------------------------- */
    TEST("Bon dai nam gon trong man va khong chong nhau");
    {
        for (int i = 0; i < BAND_COUNT; i++) {
            const band_t *b = &kBands[i];
            CHECK(b->y >= 0 && b->h > 0 && b->y + b->h <= LCD_H,
                  "dai '%s' tran man (y=%d h=%d, man cao %d)",
                  b->name, b->y, b->h, LCD_H);
        }
        for (int i = 0; i + 1 < BAND_COUNT; i++) {
            CHECK(kBands[i].y + kBands[i].h <= kBands[i + 1].y,
                  "dai '%s' chong len dai '%s'",
                  kBands[i].name, kBands[i + 1].name);
        }
        CHECK(INNER_W > 0, "be rong trong khung phai duong");
    }

    /* ---------------------------------------------------------------- */
    TEST("Khong glyph nao bi bo trong bat ky to hop trang thai nao");
    {
        lcd_gfx_reset_dropped();
        for (int a = 0; a < ALERT_COUNT; a++) {
            for (int sp = 0; sp <= LCD_SPEED_MAX; sp++) {
                lcd_status_t st = {
                    .running         = sp > 0,
                    .control_enabled = sp % 2,
                    .speed           = (uint8_t)sp,
                    .requested_speed = 255,
                    .zone            = 255,
                    .safety_limited  = sp % 3 == 0,
                    .alert           = kAlerts[a],
                };
                for (int link = 0; link <= 2; link++) {
                    render(&st, (lcd_link_state_t)link, 0, false);
                }
            }
        }
        CHECK_EQ(lcd_gfx_dropped(), 0,
                 "co chu bi bo - mot dai nao do hep hon chuoi cua no");
        CHECK_EQ(fp_oob, 0, "co ghi ra ngoai framebuffer");
    }

    /* ---------------------------------------------------------------- */
    TEST("Phase 2: canh bao tat tinh nang hien dai do, alert nang thang");
    {
        lcd_status_t ok = {.running = true, .speed = 3,
                           .alert = TREADMILL_ALERT_NONE,
                           .features_off_mask = 0};
        render(&ok, LCD_LINK_CONNECTED, 0, false);
        unsigned ok_filled = alert_filled();

        lcd_status_t warn = {.running = true, .speed = 3,
                             .alert = TREADMILL_ALERT_NONE,
                             .features_off_mask = 0x01};
        render(&warn, LCD_LINK_CONNECTED, 0, false);
        unsigned warn_hash = band_hash(&kBands[3]);
        unsigned warn_filled = alert_filled();
        CHECK(warn_filled > ok_filled * 4,
              "dai canh bao phai to nen do, khong phai chu tren nen trang");

        lcd_status_t severe = {.running = true, .speed = 3,
                               .alert = TREADMILL_ALERT_DANGER,
                               .features_off_mask = 0x01};
        render(&severe, LCD_LINK_CONNECTED, 0, false);
        CHECK(band_hash(&kBands[3]) != warn_hash,
              "alert nang phai thang dai canh bao tat tinh nang");
    }

    /* ---------------------------------------------------------------- */
    TEST("Moi lan render day dung mot khung len panel");
    {
        lcd_status_t st = {.running = true, .speed = 4};
        fp_reset();
        render(&st, LCD_LINK_CONNECTED, 0, false);
        CHECK_EQ(fp_frames, 1, "moi lan render phai gui tron mot khung");
        CHECK_EQ(fp_bytes, LCD_W * LCD_H * 2, "khung RGB565 day du");
    }

    /* ---------------------------------------------------------------- */
    TEST("Bon dai deu co noi dung, khong dai nao trong tron");
    {
        lcd_status_t st = {
            .running = true, .control_enabled = true, .speed = 5,
            .requested_speed = 5, .zone = 2,
            .alert = TREADMILL_ALERT_NONE,
        };
        render(&st, LCD_LINK_CONNECTED, 0, false);
        for (int i = 0; i < BAND_COUNT; i++) {
            CHECK(fp_ink(0, kBands[i].y, LCD_W, kBands[i].h) > 0,
                  "dai '%s' khong ve gi ca", kBands[i].name);
        }
    }

    /* ---------------------------------------------------------------- */
    TEST("Ba trang thai ket noi hien thi khac nhau");
    {
        lcd_status_t st = {0};
        unsigned sig[3];
        for (int i = 0; i < 3; i++) {
            render(&st, (lcd_link_state_t)i, 0, false);
            sig[i] = band_hash(&kBands[0]);
        }
        CHECK(sig[0] != sig[1] && sig[1] != sig[2] && sig[0] != sig[2],
              "ba trang thai BLE trong giong nhau");
    }

    /* ---------------------------------------------------------------- */
    TEST("Chay va dung hien thi khac nhau");
    {
        lcd_status_t run  = {.running = true,  .speed = 5};
        lcd_status_t stop = {.running = false, .speed = 5};

        render(&run, LCD_LINK_CONNECTED, 0, false);
        const unsigned h_run = band_hash(&kBands[1]);
        const int      speed_ink_run = fp_ink(0, SP_Y, LCD_W, SP_H);

        render(&stop, LCD_LINK_CONNECTED, 0, false);
        CHECK(band_hash(&kBands[1]) != h_run, "dai trang thai phai doi");
        CHECK(fp_ink(0, SP_Y, LCD_W, SP_H) != speed_ink_run,
              "dai toc do phai doi khi may dung");
    }

    /* ---------------------------------------------------------------- */
    TEST("Bar toc do dam dan theo muc");
    {
        /* Do thang draw_bar: neu do ca dai toc do thi chu so lon cung doi
         * theo muc va lam nhieu phep dem. */
        const int by = 10, bh = 8;
        int prev = -1;
        for (int level = 0; level <= LCD_SPEED_MAX; level++) {
            lcd_gfx_clear(LCD_PAPER);
            draw_bar(by, bh, (uint8_t)level);
            lcd_gfx_flush();
            const int ink = fp_ink(0, by, LCD_W, bh);
            if (prev >= 0) {
                CHECK(ink > prev,
                      "level %d khong dam hon level %d (%d so voi %d muc)",
                      level, level - 1, ink, prev);
            }
            prev = ink;
        }

        /* Vach tat van phai co vien, khong duoc bien mat hoan toan. */
        lcd_gfx_clear(LCD_PAPER);
        draw_bar(by, bh, 0);
        lcd_gfx_flush();
        CHECK(fp_ink(0, by, LCD_W, bh) > 0,
              "bar rong van phai thay duoc khung 10 vach");
    }

    /* ---------------------------------------------------------------- */
    TEST("Muc toc do vuot tran khong lam hong bo cuc");
    {
        lcd_status_t st = {.running = true, .speed = 200};
        lcd_gfx_reset_dropped();
        render(&st, LCD_LINK_CONNECTED, 0, false);
        CHECK_EQ(fp_oob, 0, "level vuot tran gay ghi ra ngoai khung");
        CHECK_EQ(lcd_gfx_dropped(), 0, "level vuot tran lam mat chu");
    }

    /* ---------------------------------------------------------------- */
    TEST("Moi muc canh bao ve ra noi dung khac nhau");
    {
        unsigned sig[ALERT_COUNT];
        for (int i = 0; i < ALERT_COUNT; i++) {
            lcd_status_t st = {.running = true, .speed = 3, .alert = kAlerts[i]};
            render(&st, LCD_LINK_CONNECTED, 0, false);
            sig[i] = band_hash(&kBands[3]);
            CHECK(alert_filled() > 0,
                  "alert %d ve ra o trong", (int)kAlerts[i]);
        }
        for (int i = 0; i < ALERT_COUNT; i++) {
            for (int j = i + 1; j < ALERT_COUNT; j++) {
                CHECK(sig[i] != sig[j],
                      "alert %d va %d hien thi giong het nhau",
                      (int)kAlerts[i], (int)kAlerts[j]);
            }
        }

        lcd_status_t bad = {.running = true, .alert = (treadmill_alert_t)99};
        render(&bad, LCD_LINK_CONNECTED, 0, false);
        CHECK(alert_filled() > 0,
              "alert la phai hien KHONG RO");
    }

    /* ---------------------------------------------------------------- */
    TEST("Muc nghiem trong ma hoa bang mau do + hinh khoi");
    {
        /* Canh bao nang nhat = nen do dac: qua nua dai la mau do,
         * trong khi AN TOAN khong co pixel do nao. */
        lcd_status_t safe = {.running = true, .alert = TREADMILL_ALERT_NONE};
        render(&safe, LCD_LINK_CONNECTED, 0, false);
        const int red_safe = fp_count(PAD, AL_Y, INNER_W, AL_H, LCD_RED);
        CHECK_EQ(red_safe, 0, "AN TOAN khong duoc co mau do");

        lcd_status_t danger = {.running = true, .alert = TREADMILL_ALERT_DANGER};
        render(&danger, LCD_LINK_CONNECTED, 0, false);
        const int red_danger = fp_count(PAD, AL_Y, INNER_W, AL_H, LCD_RED);
        CHECK(red_danger > INNER_W * AL_H / 2,
              "NGUY HIEM phai nen do, chiem qua nua o (%d do)", red_danger);
        const int paper_danger = fp_count(PAD, AL_Y, INNER_W, AL_H, LCD_PAPER);
        CHECK(paper_danger > 0, "chu trang phai noi tren nen do");
    }

    /* ---------------------------------------------------------------- */
    TEST("Co gioi han an toan lam doi dai toc do");
    {
        lcd_status_t plain = {
            .running = true, .speed = 5, .requested_speed = 8, .zone = 2,
            .safety_limited = false,
        };
        render(&plain, LCD_LINK_CONNECTED, 0, false);
        const unsigned h_plain = band_hash(&kBands[2]);

        plain.safety_limited = true;
        render(&plain, LCD_LINK_CONNECTED, 0, false);
        CHECK(band_hash(&kBands[2]) != h_plain,
              "co gioi han khong lam doi gi tren man");
    }

    /* ---------------------------------------------------------------- */
    TEST("set_status/set_link an toan va khong block");
    {
        fake_notify_count = 0;
        lcd_status_t st = {.running = true, .speed = 2};
        lcd_display_set_status(&st);
        lcd_display_set_link(LCD_LINK_CONNECTED);
        lcd_display_set_status(NULL);           /* con tro NULL */
        CHECK_EQ(fake_notify_count, 2, "moi cap nhat hop le phai danh thuc task");
        CHECK_EQ(fake_mutex_imbalance, 0, "mat can bang take/give");
    }

    /* ---------------------------------------------------------------- */
    TEST("changed() bat het moi truong cua ban tin status");
    {
        lcd_status_t a = {0};
        lcd_status_t b = a;
        CHECK(!changed(&a, &b), "hai ban giong nhau bi bao la khac");

        b = a; b.running         = 1; CHECK(changed(&a, &b), "running");
        b = a; b.control_enabled = 1; CHECK(changed(&a, &b), "control_enabled");
        b = a; b.speed           = 1; CHECK(changed(&a, &b), "speed");
        b = a; b.requested_speed = 1; CHECK(changed(&a, &b), "requested_speed");
        b = a; b.safety_limited  = 1; CHECK(changed(&a, &b), "safety_limited");
        b = a; b.zone            = 1; CHECK(changed(&a, &b), "zone");
        b = a; b.alert = TREADMILL_ALERT_DANGER; CHECK(changed(&a, &b), "alert");
    }

TEST_MAIN_END()

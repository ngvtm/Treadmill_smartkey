/*
 * Test may trang thai dieu khien: chan lenh khi chua san sang, xu ly ban
 * tin status, va viec day trang thai len man hinh.
 */
#include "test_util.h"
#include "treadmill_ctrl.h"
#include "lcd/lcd_display.h"

#include <stdbool.h>
#include <string.h>

/* ---- ble_client gia ---- */
static bool    s_ble_ready;
static int     s_write_rc;
static uint8_t s_sent[16];
static int     s_sent_n;

bool ble_client_is_ready(void) { return s_ble_ready; }

int ble_client_write_command(uint8_t cmd)
{
    if (!s_ble_ready) {
        return -1;
    }
    if (s_write_rc == 0 && s_sent_n < 16) {
        s_sent[s_sent_n++] = cmd;
    }
    return s_write_rc;
}

/* ---- lcd_display gia ---- */
static lcd_status_t s_lcd;
static int          s_lcd_updates;

void lcd_display_set_status(const lcd_status_t *st)
{
    s_lcd = *st;
    s_lcd_updates++;
}

void lcd_display_set_link(lcd_link_state_t link) { (void)link; }
esp_err_t lcd_display_init(void) { return ESP_OK; }
void lcd_display_backlight(bool on) { (void)on; }

/* ---- tien ich ---- */
static void reset_all(bool ready)
{
    s_ble_ready   = ready;
    s_write_rc    = 0;
    s_sent_n      = 0;
    s_lcd_updates = 0;
    memset(s_sent, 0, sizeof(s_sent));
    treadmill_ctrl_init();
    s_lcd_updates = 0;
}

static void send_status(uint8_t running, uint8_t speed, uint8_t req,
                        uint8_t limited, uint8_t zone, uint8_t alert)
{
    treadmill_status_msg_t m = {
        .type            = TREADMILL_MSG_STATUS,
        .running         = running,
        .speed           = speed,
        .requested_speed = req,
        .safety_limited  = limited,
        .zone            = zone,
        .alert           = alert,
    };
    treadmill_ctrl_handle_status((const uint8_t *)&m, sizeof(m));
}

TEST_MAIN_BEGIN("treadmill_ctrl")

    /* ---------------------------------------------------------------- */
    TEST("Ban tin status dung dinh dang khong bi doi kich thuoc");

    CHECK_EQ(sizeof(treadmill_status_msg_t), 7,
             "struct packed phai la 7 byte, neu khong hai ben lech giao thuc");

    /* ---------------------------------------------------------------- */
    TEST("Chua ket noi BLE -> khong gui lenh nao");
    {
        reset_all(false);
        treadmill_ctrl_toggle();
        treadmill_ctrl_speed_up();
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 0, "gui lenh khi BLE chua san sang");
        CHECK_EQ(s_lcd.control_enabled, 0, "dieu khien phai van tat");
    }

    /* ---------------------------------------------------------------- */
    TEST("Toggle bat/tat gui dung ma lenh");
    {
        reset_all(true);
        treadmill_ctrl_toggle();
        CHECK_EQ(s_sent_n, 1, "toggle lan 1 phai gui 1 lenh");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_ON, "lenh dau phai la ON");
        CHECK_EQ(s_lcd.control_enabled, 1, "man hinh phai hien DIEU KHIEN BAT");

        treadmill_ctrl_toggle();
        CHECK_EQ(s_sent[1], TREADMILL_CMD_OFF, "lenh sau phai la OFF");
        CHECK_EQ(s_lcd.control_enabled, 0, "man hinh phai hien DIEU KHIEN TAT");
    }

    /* ---------------------------------------------------------------- */
    TEST("Ghi BLE that bai -> khong doi trang thai");
    {
        reset_all(true);
        s_write_rc = -5;
        treadmill_ctrl_toggle();
        CHECK_EQ(s_lcd.control_enabled, 0,
                 "bat dieu khien du lenh khong gui duoc");

        s_write_rc = 0;
        treadmill_ctrl_toggle();
        CHECK_EQ(s_sent[0], TREADMILL_CMD_ON,
                 "sau khi loi, toggle tiep theo van phai la ON");
    }

    /* ---------------------------------------------------------------- */
    TEST("Tang/giam toc bi chan khi chua bat dieu khien");
    {
        reset_all(true);
        treadmill_ctrl_speed_up();
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 0, "gui lenh toc do khi dieu khien dang tat");

        treadmill_ctrl_toggle();          /* bat */
        treadmill_ctrl_speed_up();
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 3, "sau khi bat phai gui du 3 lenh");
        CHECK_EQ(s_sent[1], TREADMILL_CMD_SPEED_UP, "ma lenh tang toc");
        CHECK_EQ(s_sent[2], TREADMILL_CMD_SPEED_DOWN, "ma lenh giam toc");
    }

    /* ---------------------------------------------------------------- */
    TEST("Ban tin status hong bi bo qua");
    {
        reset_all(true);
        uint8_t junk[4] = {TREADMILL_MSG_STATUS, 1, 5, 5};

        treadmill_ctrl_handle_status(junk, sizeof(junk));       /* sai do dai */
        CHECK_EQ(s_lcd_updates, 0, "status sai do dai van duoc nhan");

        treadmill_status_msg_t m = {.type = 0x42, .running = 1, .speed = 9};
        treadmill_ctrl_handle_status((const uint8_t *)&m, sizeof(m));
        CHECK_EQ(s_lcd_updates, 0, "status sai ma loai van duoc nhan");
        CHECK_EQ(treadmill_ctrl_is_running(), 0, "trang thai bi thay doi");
    }

    /* ---------------------------------------------------------------- */
    TEST("Status hop le duoc day len man hinh day du");
    {
        reset_all(true);
        send_status(1, 6, 8, 1, 2, TREADMILL_ALERT_WARNING_1);

        CHECK_EQ(s_lcd_updates, 1, "moi status hop le phai cap nhat man hinh");
        CHECK_EQ(s_lcd.running, 1, "running");
        CHECK_EQ(s_lcd.speed, 6, "speed");
        CHECK_EQ(s_lcd.requested_speed, 8, "requested_speed");
        CHECK_EQ(s_lcd.safety_limited, 1, "safety_limited phai duoc chuyen tiep");
        CHECK_EQ(s_lcd.zone, 2, "zone");
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_WARNING_1, "alert");
        CHECK_EQ(treadmill_ctrl_is_running(), 1, "is_running");
    }

    /* ---------------------------------------------------------------- */
    TEST("May bao dung -> tu tat dieu khien phia smartkey");
    {
        reset_all(true);
        treadmill_ctrl_toggle();
        CHECK_EQ(s_lcd.control_enabled, 1, "dang bat");

        send_status(0, 0, 0, 0, 0, TREADMILL_ALERT_NONE);
        CHECK_EQ(s_lcd.control_enabled, 0, "may dung nhung smartkey van bat");

        s_sent_n = 0;
        treadmill_ctrl_speed_up();
        CHECK_EQ(s_sent_n, 0, "van gui duoc lenh toc do sau khi may dung");
    }

    /* ---------------------------------------------------------------- */
    TEST("Lenh bi tu choi -> tu tat dieu khien");
    {
        reset_all(true);
        treadmill_ctrl_toggle();
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_CMD_REJECTED);
        CHECK_EQ(s_lcd.control_enabled, 0,
                 "CMD_REJECTED phai buoc tat dieu khien");
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_CMD_REJECTED, "alert phai giu");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.1: lenh toc do bi chan khi alert thuoc nhom dung cung");
    {
        const uint8_t hard_alerts[] = {
            TREADMILL_ALERT_DANGER, TREADMILL_ALERT_NO_PERSON,
            TREADMILL_ALERT_SAFETY_KEY_OUT, TREADMILL_ALERT_CMD_REJECTED,
        };
        for (int i = 0; i < 4; i++) {
            reset_all(true);
            treadmill_ctrl_toggle();          /* bat dieu khien */
            send_status(1, 3, 3, 0, 1, hard_alerts[i]);
            s_sent_n = 0;
            treadmill_ctrl_speed_up();
            treadmill_ctrl_speed_down();
            CHECK_EQ(s_sent_n, 0, "van gui lenh toc do duoi alert dung cung");
        }
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.1: WARNING_1/WARNING_2 khong chan lenh toc do o smartkey");
    {
        reset_all(true);
        treadmill_ctrl_toggle();
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_WARNING_2);
        s_sent_n = 0;
        treadmill_ctrl_speed_up();
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 2, "WARNING khong duoc chan giam toc bang tay");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.1: ON bi chan khi khoa an toan dang bi but ra");
    {
        reset_all(true);
        send_status(0, 0, 0, 0, 0, TREADMILL_ALERT_SAFETY_KEY_OUT);
        treadmill_ctrl_toggle();              /* dinh bat */
        CHECK_EQ(s_sent_n, 0, "van gui ON du khoa an toan dang mo");
        CHECK_EQ(s_lcd.control_enabled, 0, "dieu khien van bat du ON bi chan");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.1: OFF khong bao gio bi chan, ke ca khi dang DANGER");
    {
        reset_all(true);
        treadmill_ctrl_toggle();              /* bat */
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_DANGER);
        s_sent_n = 0;
        treadmill_ctrl_toggle();              /* tat */
        CHECK_EQ(s_sent_n, 1, "OFF phai luon gui duoc");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_OFF, "lenh phai la OFF");
        CHECK_EQ(s_lcd.control_enabled, 0, "dieu khien phai tat sau OFF");
    }

    /* ---------------------------------------------------------------- */
    TEST("Mat ket noi -> xoa het so lieu cu tren man hinh");
    {
        reset_all(true);
        treadmill_ctrl_toggle();
        send_status(1, 9, 9, 1, 3, TREADMILL_ALERT_DANGER);
        CHECK_EQ(s_lcd.speed, 9, "da co so lieu");

        /* main.c goi treadmill_ctrl_init() trong BLE_GAP_EVENT_DISCONNECT */
        s_lcd_updates = 0;
        treadmill_ctrl_init();

        CHECK_EQ(s_lcd_updates, 1, "init phai cap nhat man hinh");
        CHECK_EQ(s_lcd.speed, 0, "toc do cu con sot lai");
        CHECK_EQ(s_lcd.running, 0, "trang thai chay cu con sot lai");
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_NONE, "canh bao cu con sot lai");
        CHECK_EQ(s_lcd.zone, 0, "vung cu con sot lai");
        CHECK_EQ(s_lcd.safety_limited, 0, "co gioi han cu con sot lai");
        CHECK_EQ(s_lcd.control_enabled, 0, "dieu khien phai tat");
        CHECK_EQ(treadmill_ctrl_is_running(), 0, "is_running phai la false");
    }

TEST_MAIN_END()

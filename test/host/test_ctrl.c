/*
 * Test may trang thai dieu khien: chan lenh khi chua san sang, xu ly ban
 * tin status, va viec day trang thai len man hinh.
 */
#include "test_util.h"
#include "treadmill_ctrl.h"
#include "lcd/lcd_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

/* ---- RSSI gia (S1.4) ---- */
static int s_rssi_dbm = -50;
static int s_rssi_rc = 0;

int ble_client_get_rssi(int8_t *out_rssi)
{
    if (s_rssi_rc != 0) {
        return s_rssi_rc;
    }
    *out_rssi = (int8_t)s_rssi_dbm;
    return 0;
}

/* ---- FreeRTOS gia: dong ho dieu khien duoc bang tay ---- */
static uint32_t s_fake_tick_ms;

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(s_fake_tick_ms * configTICK_RATE_HZ / 1000);
}

int xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                void *arg, uint32_t prio, TaskHandle_t *out)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)out;
    return 1; /* task tick khong chay trong test; test goi tick truc tiep */
}

void vTaskDelay(TickType_t ticks) { (void)ticks; }

static void advance_ms(uint32_t ms)
{
    s_fake_tick_ms += ms;
    treadmill_ctrl_tick();
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
    s_fake_tick_ms = 0;
    s_rssi_dbm    = -50;
    s_rssi_rc     = 0;
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

/* Bat dieu khien theo dung nghia S1.2: gui ON roi nhan status xac nhan. */
static void confirm_on(void)
{
    treadmill_ctrl_request_on();
    send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NONE);
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
        treadmill_ctrl_request_on();
        treadmill_ctrl_speed_up();
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 0, "gui lenh khi BLE chua san sang");
        CHECK_EQ(s_lcd.control_enabled, 0, "dieu khien phai van tat");
    }

    /* ---------------------------------------------------------------- */
    TEST("Toggle bat/tat gui dung ma lenh (S1.2: ON cho xac nhan)");
    {
        reset_all(true);
        treadmill_ctrl_request_on();
        CHECK_EQ(s_sent_n, 1, "toggle lan 1 phai gui 1 lenh");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_ON, "lenh dau phai la ON");
        CHECK_EQ(s_lcd.control_enabled, 0, "ON chua xac nhan thi chua bat");

        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NONE);
        CHECK_EQ(s_lcd.control_enabled, 1, "status chay phai xac nhan ON");

        treadmill_ctrl_request_off();
        CHECK_EQ(s_sent[1], TREADMILL_CMD_OFF, "lenh sau phai la OFF");
        CHECK_EQ(s_lcd.control_enabled, 0, "man hinh phai hien DIEU KHIEN TAT");
    }

    /* ---------------------------------------------------------------- */
    TEST("Ghi BLE that bai -> khong doi trang thai");
    {
        reset_all(true);
        s_write_rc = -5;
        treadmill_ctrl_request_on();
        CHECK_EQ(s_lcd.control_enabled, 0,
                 "bat dieu khien du lenh khong gui duoc");

        s_write_rc = 0;
        treadmill_ctrl_request_on();
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

        confirm_on();                     /* bat + xac nhan */
        treadmill_ctrl_speed_up();
        advance_ms(300);
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
        confirm_on();
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
        confirm_on();
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
            confirm_on();                     /* bat + xac nhan */
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
        treadmill_ctrl_request_on();
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_WARNING_2);
        s_sent_n = 0;
        treadmill_ctrl_speed_up();
        advance_ms(300);
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 2, "WARNING khong duoc chan giam toc bang tay");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.1: ON bi chan khi khoa an toan dang bi but ra");
    {
        reset_all(true);
        send_status(0, 0, 0, 0, 0, TREADMILL_ALERT_SAFETY_KEY_OUT);
        treadmill_ctrl_request_on();          /* dinh bat */
        CHECK_EQ(s_sent_n, 0, "van gui ON du khoa an toan dang mo");
        CHECK_EQ(s_lcd.control_enabled, 0, "dieu khien van bat du ON bi chan");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.1: OFF khong bao gio bi chan, ke ca khi dang DANGER");
    {
        reset_all(true);
        treadmill_ctrl_request_on();          /* bat */
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_DANGER);
        s_sent_n = 0;
        treadmill_ctrl_request_off();         /* tat */
        CHECK_EQ(s_sent_n, 1, "OFF phai luon gui duoc");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_OFF, "lenh phai la OFF");
        CHECK_EQ(s_lcd.control_enabled, 0, "dieu khien phai tat sau OFF");
    }

    /* ---------------------------------------------------------------- */
    TEST("Mat ket noi -> xoa het so lieu cu tren man hinh");
    {
        reset_all(true);
        confirm_on();
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

    /* ---------------------------------------------------------------- */
    TEST("S1.2: ON qua timeout khong xac nhan thi khong bat, khong tu gui lai");
    {
        reset_all(true);
        treadmill_ctrl_request_on();      /* gui ON, cho xac nhan */
        CHECK_EQ(s_sent_n, 1, "phai gui ON");
        CHECK_EQ(s_lcd.control_enabled, 0, "chua xac nhan thi chua bat");

        advance_ms(2000);                 /* het cua so xac nhan */
        CHECK_EQ(s_lcd.control_enabled, 0, "qua timeout van bat");
        CHECK_EQ(s_sent_n, 1, "khong duoc tu gui lai ON");

        treadmill_ctrl_request_on();      /* bam lai: la lan ON moi */
        CHECK_EQ(s_sent_n, 2, "bam lai phai gui ON moi");
        CHECK_EQ(s_sent[1], TREADMILL_CMD_ON, "lenh moi phai la ON");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.2: status chay den muon sau timeout khong xac nhan nguoc");
    {
        reset_all(true);
        treadmill_ctrl_request_on();
        advance_ms(2500);
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NONE);
        CHECK_EQ(s_lcd.control_enabled, 0, "status muon van bat dieu khien");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.2: OFF huy ON dang cho xac nhan");
    {
        reset_all(true);
        treadmill_ctrl_request_on();      /* ON dang cho */
        treadmill_ctrl_request_off();     /* bam nua = OFF/huy */
        CHECK_EQ(s_sent_n, 2, "phai gui OFF huy");
        CHECK_EQ(s_sent[1], TREADMILL_CMD_OFF, "lenh huy phai la OFF");
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NONE);
        CHECK_EQ(s_lcd.control_enabled, 0, "ON da huy thi status khong bat lai");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.2: OFF ghi BLE loi thi thu lai 1 lan, that bai thi giu nguyen");
    {
        reset_all(true);
        confirm_on();
        s_write_rc = -5;
        s_sent_n = 0;
        treadmill_ctrl_request_off();     /* OFF loi 2 lan */
        CHECK_EQ(s_lcd.control_enabled, 1, "OFF loi thi dieu khien van bat");
        s_write_rc = 0;
        treadmill_ctrl_request_off();
        CHECK_EQ(s_sent_n, 1, "sau khi het loi phai gui duoc OFF");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_OFF, "lenh phai la OFF");
        CHECK_EQ(s_lcd.control_enabled, 0, "OFF thanh cong thi tat");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.2: mat status qua 2 s -> MAT TIN HIEU, khoa dieu khien, co status moi thi mo");
    {
        reset_all(true);
        confirm_on();
        CHECK_EQ(s_lcd.control_enabled, 1, "dang bat");

        advance_ms(2000);                 /* vua dung 2000 ms: chua mat */
        CHECK_EQ(s_lcd.signal_lost, 0, "chua qua nguong thi chua mat tin hieu");
        advance_ms(10);                   /* qua nguong */
        CHECK_EQ(s_lcd.signal_lost, 1, "phai hien MAT TIN HIEU");
        CHECK_EQ(s_lcd.control_enabled, 0, "mat tin hieu thi khoa dieu khien");

        s_sent_n = 0;
        treadmill_ctrl_speed_up();
        CHECK_EQ(s_sent_n, 0, "mat tin hieu van gui lenh toc do");
        treadmill_ctrl_request_on();      /* dinh bat lai khi mat tin hieu */
        CHECK_EQ(s_sent_n, 0, "mat tin hieu van gui ON");

        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NONE);
        CHECK_EQ(s_lcd.signal_lost, 0, "co status moi thi het MAT TIN HIEU");
        treadmill_ctrl_request_on();
        CHECK_EQ(s_sent_n, 1, "co tin hieu lai thi bat duoc");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_ON, "lenh phai la ON");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.3: toc do gioi han 1 nac moi 300 ms, chung cho ca 2 chieu");
    {
        reset_all(true);
        confirm_on();
        s_sent_n = 0;
        treadmill_ctrl_speed_up();
        CHECK_EQ(s_sent_n, 1, "nac dau phai gui");
        treadmill_ctrl_speed_up();
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 1, "bam don trong 300 ms khong duoc gui them");
        advance_ms(299);
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 1, "299 ms van chua du");
        advance_ms(1);
        treadmill_ctrl_speed_down();
        CHECK_EQ(s_sent_n, 2, "du 300 ms thi gui duoc nac tiep");
        CHECK_EQ(s_sent[1], TREADMILL_CMD_SPEED_DOWN, "nac tiep phai la DOWN");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.4: ON bi chan khi RSSI yeu hon -75 dBm, dung nguong thi duoc");
    {
        reset_all(true);
        s_rssi_dbm = -80;
        treadmill_ctrl_request_on();
        CHECK_EQ(s_sent_n, 0, "RSSI -80 van gui ON");

        s_rssi_dbm = -75;
        treadmill_ctrl_request_on();
        CHECK_EQ(s_sent_n, 1, "RSSI dung -75 phai gui duoc ON");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_ON, "lenh phai la ON");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.4: RSSI doc loi thi fail open, OFF va toc do khong bi cong");
    {
        reset_all(true);
        s_rssi_rc = -1;
        treadmill_ctrl_request_on();
        CHECK_EQ(s_sent_n, 1, "RSSI loi thi ON van phai gui (fail open)");

        confirm_on();                     /* RSSI loi nhung van xac nhan */
        s_rssi_dbm = -90;                 /* yeu, nhung dang chay roi */
        s_rssi_rc = 0;
        s_sent_n = 0;
        treadmill_ctrl_speed_up();
        CHECK_EQ(s_sent_n, 1, "RSSI yeu giua luc chay van tang toc duoc");
        treadmill_ctrl_request_off();
        CHECK_EQ(s_sent_n, 2, "RSSI yeu van OFF duoc");
        CHECK_EQ(s_sent[1], TREADMILL_CMD_OFF, "lenh phai la OFF");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.6: alert nghiem trong giu toi thieu 3 s tren man hinh");
    {
        reset_all(true);
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_DANGER);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_DANGER, "DANGER phai hien ngay");

        advance_ms(1000);
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NONE);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_DANGER, "1 s sau van phai giu DANGER");

        advance_ms(1999);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_DANGER, "2999 ms van giu");
        advance_ms(1);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_NONE, "du 3 s thi alert moi thang");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.6: alert nghiem trong moi lam moi cua so giu, alert thuong thi khong");
    {
        reset_all(true);
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_DANGER);
        advance_ms(2000);
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NO_PERSON);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_NO_PERSON, "alert moi phai hien ngay");
        advance_ms(2000);
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_WARNING_1);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_NO_PERSON, "WARNING khong duoc chen ngang");
        advance_ms(1000);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_WARNING_1, "het cua so moi thi WARNING thang");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.6: CMD_REJECTED khong thuoc nhom giu");
    {
        reset_all(true);
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_CMD_REJECTED);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_CMD_REJECTED, "CMD_REJECTED hien ngay");
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_NONE);
        CHECK_EQ(s_lcd.alert, TREADMILL_ALERT_NONE, "CMD_REJECTED khong bi giu");
    }

    /* ---------------------------------------------------------------- */
    TEST("GĐ2: MOTOR_FAULT chan ON va toc do, OFF van duoc");
    {
        reset_all(true);
        // ON gate: status chay nhung alert MOTOR_FAULT, chua enabled.
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_MOTOR_FAULT);
        treadmill_ctrl_request_on();
        CHECK_EQ(s_sent_n, 0, "MOTOR_FAULT van gui ON");

        // Speed gate: enabled roi, alert chuyen sang MOTOR_FAULT.
        confirm_on();
        s_sent_n = 0;
        send_status(1, 3, 3, 0, 1, TREADMILL_ALERT_MOTOR_FAULT);
        treadmill_ctrl_speed_up();
        CHECK_EQ(s_sent_n, 0, "MOTOR_FAULT van tang toc");
        treadmill_ctrl_request_off();
        CHECK_EQ(s_sent_n, 1, "MOTOR_FAULT van OFF duoc");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_OFF, "lenh phai la OFF");
    }

    /* ---------------------------------------------------------------- */
    TEST("S1.3: OFF khong bi gioi han toc do bam");
    {
        reset_all(true);
        confirm_on();
        s_sent_n = 0;
        treadmill_ctrl_request_off();
        CHECK_EQ(s_sent_n, 1, "OFF ngay sau lenh toc do van phai gui");
        CHECK_EQ(s_sent[0], TREADMILL_CMD_OFF, "lenh phai la OFF");
    }

TEST_MAIN_END()

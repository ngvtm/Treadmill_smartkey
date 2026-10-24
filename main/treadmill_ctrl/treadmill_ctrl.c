#include "treadmill_ctrl.h"
#include "ble_client/ble_client.h"
#include "lcd/lcd_display.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "TREADMILL_CTRL"

/* Phase 1, S1.2 timeouts (menuconfig "Smartkey Control", defaults = plan). */
#ifndef CONFIG_TREADMILL_CTRL_ON_CONFIRM_TIMEOUT_MS
#define CONFIG_TREADMILL_CTRL_ON_CONFIRM_TIMEOUT_MS 2000
#endif
#ifndef CONFIG_TREADMILL_CTRL_STATUS_STALE_MS
#define CONFIG_TREADMILL_CTRL_STATUS_STALE_MS 2000
#endif
/* Phase 1, S1.3: at most one speed step per this interval, across both
 * directions (a burst of presses must not jump the speed). */
#ifndef CONFIG_TREADMILL_CTRL_SPEED_RATE_LIMIT_MS
#define CONFIG_TREADMILL_CTRL_SPEED_RATE_LIMIT_MS 300
#endif
/* Phase 1, S1.4: RSSI start gate (dBm). ON is refused when the link is
 * weaker than this. Default -75 is deliberately loose to avoid false
 * refusals; the final value must be calibrated on real hardware
 * (RSSI beside the belt vs. RSSI from the next room) and the gate
 * dropped entirely if calibration shows RSSI cannot tell them apart. */
#ifndef CONFIG_TREADMILL_CTRL_RSSI_MIN_DBM
#define CONFIG_TREADMILL_CTRL_RSSI_MIN_DBM (-75)
#endif

#define CTRL_TICK_MS 100

static bool s_enabled = false;
static bool s_server_running = false;
static uint8_t s_server_speed = 0;
static uint8_t s_server_requested_speed = 0;
static uint8_t s_server_zone = 0;
static uint8_t s_server_safety_limited = 0;
static uint8_t s_server_alert = TREADMILL_ALERT_NONE;
/* S1.2: ON is no longer optimistic. Sending ON starts a pending window;
 * only a status with running=1 inside the window confirms control ON.
 * No auto-retry: re-sending ON after a refusal could restart the belt
 * right after a fault. */
static bool s_on_pending = false;
static uint32_t s_on_deadline_ms = 0;
/* S1.2: staleness — a connected server that stops sending status is
 * treated as signal lost: control locks until a fresh status arrives. */
static bool s_have_status = false;
static uint32_t s_last_status_ms = 0;
static bool s_signal_lost = false;
/* S1.3 speed rate limit. */
static uint32_t s_last_speed_ms = 0;
static bool s_have_speed = false;
/* S1.6: severe-alert hold. The LCD shows s_display_alert, which lags
 * s_server_alert while a severe alert is inside its 3 s hold window:
 * a warning that flashes for one status and vanishes before the runner
 * can read it is worse than a warning that lingers 3 s. */
#ifndef CONFIG_TREADMILL_CTRL_ALERT_HOLD_MS
#define CONFIG_TREADMILL_CTRL_ALERT_HOLD_MS 3000
#endif
static uint8_t s_display_alert = TREADMILL_ALERT_NONE;
static uint32_t s_alert_hold_until_ms = 0;
static uint8_t s_features_off_mask = 0;

static bool alert_is_severe(uint8_t alert)
{
    return alert == TREADMILL_ALERT_DANGER ||
           alert == TREADMILL_ALERT_NO_PERSON ||
           alert == TREADMILL_ALERT_SAFETY_KEY_OUT ||
           alert == TREADMILL_ALERT_MOTOR_FAULT;
}

static uint32_t now_ms(void)
{
    return (uint32_t)((uint64_t)xTaskGetTickCount() * 1000u / configTICK_RATE_HZ);
}

/* Đẩy trạng thái hiện tại lên màn hình. No-op nếu LCD chưa init được. */
static void push_to_lcd(void)
{
    lcd_status_t st = {
        .control_enabled = s_enabled,
        .running         = s_server_running,
        .speed           = s_server_speed,
        .requested_speed = s_server_requested_speed,
        .safety_limited  = s_server_safety_limited != 0,
        .zone            = s_server_zone,
        .alert           = (treadmill_alert_t)s_display_alert,
        .signal_lost     = s_signal_lost,
        .features_off_mask = s_features_off_mask,
    };
    lcd_display_set_status(&st);
}

/* S1.2: evaluate the ON-confirm timeout and the status-staleness gate
 * against the current clock. Called from every command entry point and
 * from the periodic tick, so an expiry surfaces even with no user input. */
static void evaluate_timeouts(uint32_t now)
{
    if (s_on_pending && now >= s_on_deadline_ms) {
        // Unconfirmed ON: control simply never turns on (the display
        // already shows control OFF — there is no message line on the
        // LCD to print "LENH KHONG DUOC XAC NHAN" on, and inventing one
        // would mean redesigning the screen for a transient state).
        s_on_pending = false;
        ESP_LOGW(TAG, "ON not confirmed within %d ms, control stays OFF",
                 CONFIG_TREADMILL_CTRL_ON_CONFIRM_TIMEOUT_MS);
        push_to_lcd();
    }
    if (s_have_status && !s_signal_lost &&
        now - s_last_status_ms > CONFIG_TREADMILL_CTRL_STATUS_STALE_MS) {
        s_signal_lost = true;
        s_enabled = false;
        s_on_pending = false;
        ESP_LOGW(TAG, "MAT TIN HIEU: no status for >%d ms, control locked",
                 CONFIG_TREADMILL_CTRL_STATUS_STALE_MS);
        push_to_lcd();
    }
    // S1.6: when the hold window expires, the latest alert wins.
    if (s_display_alert != s_server_alert &&
        now >= s_alert_hold_until_ms) {
        s_display_alert = s_server_alert;
        push_to_lcd();
    }
}

void treadmill_ctrl_tick(void)
{
    evaluate_timeouts(now_ms());
}

static void ctrl_tick_task(void *arg)
{
    (void)arg;
    // Phase 1, S1.5: watchdog-supervised, like the LCD and button tasks.
    esp_task_wdt_add(NULL);
    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(CTRL_TICK_MS));
        treadmill_ctrl_tick();
    }
}

static const char *alert_to_str(uint8_t alert)
{
    switch (alert) {
    case TREADMILL_ALERT_NONE:         return "NONE";
    case TREADMILL_ALERT_WARNING_1:    return "WARNING_1";
    case TREADMILL_ALERT_WARNING_2:    return "WARNING_2";
    case TREADMILL_ALERT_DANGER:       return "DANGER";
    case TREADMILL_ALERT_NO_PERSON:    return "NO_PERSON";
    case TREADMILL_ALERT_CMD_REJECTED: return "CMD_REJECTED";
    case TREADMILL_ALERT_SAFETY_KEY_OUT: return "SAFETY_KEY_OUT";
    case TREADMILL_ALERT_MOTOR_FAULT:  return "MOTOR_FAULT";
    default:                           return "UNKNOWN";
    }
}

void treadmill_ctrl_init(void)
{
    /* Cũng được gọi lại khi mất kết nối, nên xoá luôn trạng thái cũ của
     * server để màn hình không giữ số liệu đã hết hiệu lực. */
    s_enabled = false;
    s_server_running = false;
    s_server_speed = 0;
    s_server_requested_speed = 0;
    s_server_zone = 0;
    s_server_safety_limited = 0;
    s_server_alert = TREADMILL_ALERT_NONE;
    s_display_alert = TREADMILL_ALERT_NONE;
    s_alert_hold_until_ms = 0;
    s_features_off_mask = 0;
    s_on_pending = false;
    s_have_status = false;
    s_signal_lost = false;
    s_have_speed = false;
    push_to_lcd();
    ESP_LOGI(TAG, "initialized (control=OFF)");

    // Periodic tick for the S1.2 timeouts. Created once: init is also
    // called on disconnect, and re-creating the task would leak tasks.
    static bool tick_task_created = false;
    if (!tick_task_created) {
        tick_task_created = true;
        xTaskCreate(ctrl_tick_task, "ctrl_tick", 2048, NULL, 9, NULL);
    }
}

/* Phase 1, S1.1 — control gating mirror. The server is the real safety
 * layer and would refuse these commands anyway; refusing them here too
 * means a dangerous command is never even put on the air, and the user
 * gets the reason immediately instead of after a round trip.
 * OFF is NEVER gated: stopping must always be the easiest action. */
static bool alert_blocks_speed(uint8_t alert)
{
    return alert == TREADMILL_ALERT_DANGER ||
           alert == TREADMILL_ALERT_NO_PERSON ||
           alert == TREADMILL_ALERT_SAFETY_KEY_OUT ||
           alert == TREADMILL_ALERT_MOTOR_FAULT ||
           alert == TREADMILL_ALERT_CMD_REJECTED;
}

static bool alert_blocks_on(uint8_t alert)
{
    // MOTOR_FAULT mirrors the server's latched fault: the server
    // refuses ON until the cooldown expires, and the key must not
    // offer a start it knows will be refused.
    return alert == TREADMILL_ALERT_SAFETY_KEY_OUT ||
           alert == TREADMILL_ALERT_MOTOR_FAULT;
}

/* Phase 1, S1.3: the old single toggle() is split. A quick press can
 * only ever turn the machine OFF; turning it ON requires the HOLD event
 * from the button layer, so a child tapping the key once cannot start
 * the belt. */
void treadmill_ctrl_request_off(void)
{
    if (!ble_client_is_ready()) {
        ESP_LOGW(TAG, "BLE not ready, ignore OFF");
        return;
    }

    evaluate_timeouts(now_ms());

    // OFF is never gated; a pending ON is cancelled by it. On BLE write
    // failure the OFF is retried once, and control keeps its old state
    // until a write actually goes out (the display keeps showing the
    // server's running state until a status confirms the stop).
    int rc = ble_client_write_command(TREADMILL_CMD_OFF);
    if (rc != 0) {
        rc = ble_client_write_command(TREADMILL_CMD_OFF);
    }
    if (rc == 0) {
        s_enabled = false;
        s_on_pending = false;
        push_to_lcd();
        ESP_LOGI(TAG, "Treadmill control: OFF");
    } else {
        ESP_LOGE(TAG, "OFF failed twice, keep control=%s",
                 s_enabled ? "ON" : "OFF");
    }
}

void treadmill_ctrl_request_on(void)
{
    if (!ble_client_is_ready()) {
        ESP_LOGW(TAG, "BLE not ready, ignore ON");
        return;
    }

    uint32_t now = now_ms();
    evaluate_timeouts(now);

    // ON path: S1.1 gate + S1.2 staleness gate + S1.4 RSSI gate, then
    // pending-confirm. The RSSI gate applies ONLY to ON: never to OFF,
    // never to reconnect, and never re-checked mid-run (posture changes
    // would drop a running session on a fading signal).
    if (s_signal_lost) {
        ESP_LOGW(TAG, "Refuse ON: signal lost (no fresh status)");
        return;
    }
    if (alert_blocks_on(s_server_alert)) {
        ESP_LOGW(TAG, "Refuse ON: latest alert=%s (safety key out)",
                 alert_to_str(s_server_alert));
        return;
    }
    int8_t rssi = 0;
    int rssi_rc = ble_client_get_rssi(&rssi);
    if (rssi_rc == 0 && rssi < CONFIG_TREADMILL_CTRL_RSSI_MIN_DBM) {
        // No message line on the LCD to print "KEY QUA XA" on (same
        // limitation as the S1.2 unconfirmed-ON case); the refusal is
        // visible as control staying OFF, and logged here.
        ESP_LOGW(TAG, "Refuse ON: KEY QUA XA (rssi=%d dBm < %d dBm)",
                 (int)rssi, CONFIG_TREADMILL_CTRL_RSSI_MIN_DBM);
        return;
    }
    if (rssi_rc != 0) {
        // Fail open, loudly: a broken RSSI read must not brick ON
        // forever. The gate is provisional until calibrated anyway.
        ESP_LOGW(TAG, "RSSI unreadable, ON gate skipped (fail open)");
    }

    int rc = ble_client_write_command(TREADMILL_CMD_ON);
    if (rc == 0) {
        s_on_pending = true;
        s_on_deadline_ms = now + CONFIG_TREADMILL_CTRL_ON_CONFIRM_TIMEOUT_MS;
        push_to_lcd();
        ESP_LOGI(TAG, "ON sent, awaiting confirmation within %d ms",
                 CONFIG_TREADMILL_CTRL_ON_CONFIRM_TIMEOUT_MS);
    } else {
        ESP_LOGE(TAG, "ON write failed, control stays OFF");
    }
}

bool treadmill_ctrl_is_control_active(void)
{
    return s_enabled || s_on_pending;
}

/* S1.3 rate limit, shared by both directions. Returns false when the
 * caller must refuse the step. */
static bool speed_rate_ok(uint32_t now)
{
    if (s_have_speed &&
        now - s_last_speed_ms < CONFIG_TREADMILL_CTRL_SPEED_RATE_LIMIT_MS) {
        return false;
    }
    s_have_speed = true;
    s_last_speed_ms = now;
    return true;
}

void treadmill_ctrl_speed_up(void)
{
    uint32_t now = now_ms();
    evaluate_timeouts(now);
    if (!s_enabled || !ble_client_is_ready()) {
        ESP_LOGW(TAG, "Ignore SPEED_UP: control disabled or BLE not ready");
        return;
    }
    if (!speed_rate_ok(now)) {
        ESP_LOGW(TAG, "Refuse SPEED_UP: rate limited to 1 step per %d ms",
                 CONFIG_TREADMILL_CTRL_SPEED_RATE_LIMIT_MS);
        return;
    }

    if (alert_blocks_speed(s_server_alert)) {
        ESP_LOGW(TAG, "Refuse SPEED_UP: latest alert=%s",
                 alert_to_str(s_server_alert));
        return;
    }

    int rc = ble_client_write_command(TREADMILL_CMD_SPEED_UP);
    if (rc == 0) {
        ESP_LOGI(TAG, "Command: SPEED_UP");
    }
}

void treadmill_ctrl_speed_down(void)
{
    uint32_t now = now_ms();
    evaluate_timeouts(now);
    if (!s_enabled || !ble_client_is_ready()) {
        ESP_LOGW(TAG, "Ignore SPEED_DOWN: control disabled or BLE not ready");
        return;
    }
    if (!speed_rate_ok(now)) {
        ESP_LOGW(TAG, "Refuse SPEED_DOWN: rate limited to 1 step per %d ms",
                 CONFIG_TREADMILL_CTRL_SPEED_RATE_LIMIT_MS);
        return;
    }

    if (alert_blocks_speed(s_server_alert)) {
        ESP_LOGW(TAG, "Refuse SPEED_DOWN: latest alert=%s",
                 alert_to_str(s_server_alert));
        return;
    }

    int rc = ble_client_write_command(TREADMILL_CMD_SPEED_DOWN);
    if (rc == 0) {
        ESP_LOGI(TAG, "Command: SPEED_DOWN");
    }
}

bool treadmill_ctrl_is_running(void)
{
    return s_server_running;
}

uint8_t treadmill_ctrl_get_features_off_mask(void)
{
    return s_features_off_mask;
}

void treadmill_ctrl_handle_status(const uint8_t *data, uint16_t len)
{
    // Phase 2: accept both the old 7-byte status (mask reads 0) and
    // the new 8-byte status. The mask byte is read from the raw
    // buffer, not through the struct, so a 7-byte packet is never
    // read past its end.
    if (len != 7 && len != sizeof(treadmill_status_msg_t)) {
        ESP_LOGW(TAG, "Ignore invalid status len=%u", len);
        return;
    }

    const treadmill_status_msg_t *msg = (const treadmill_status_msg_t *)data;
    if (msg->type != TREADMILL_MSG_STATUS) {
        ESP_LOGW(TAG, "Ignore unknown msg type=0x%02X", msg->type);
        return;
    }
    s_features_off_mask = (len >= 8) ? data[7] : 0;

    // Fresh status clears the staleness lock and (re)starts its clock.
    uint32_t now = now_ms();
    s_have_status = true;
    s_last_status_ms = now;
    s_signal_lost = false;

    s_server_running = msg->running != 0;
    s_server_speed = msg->speed;
    s_server_requested_speed = msg->requested_speed;
    s_server_safety_limited = msg->safety_limited;
    s_server_zone = msg->zone;
    s_server_alert = msg->alert;

    // S1.6 display hold: a severe alert takes the screen immediately
    // and keeps it for 3 s; a newer severe alert refreshes the window;
    // anything else waits out the window instead of flashing past.
    if (alert_is_severe(s_server_alert)) {
        s_display_alert = s_server_alert;
        s_alert_hold_until_ms = now + CONFIG_TREADMILL_CTRL_ALERT_HOLD_MS;
    } else if (now < s_alert_hold_until_ms &&
               alert_is_severe(s_display_alert)) {
        // Hold: display keeps the severe alert, latest waits.
    } else {
        s_display_alert = s_server_alert;
    }

    if (!s_server_running) {
        s_enabled = false;
        s_on_pending = false;
    }

    if (s_server_alert == TREADMILL_ALERT_CMD_REJECTED ||
        s_server_alert == TREADMILL_ALERT_SAFETY_KEY_OUT) {
        s_enabled = false;
        s_on_pending = false;
    }

    // S1.2 confirmation: a running status inside the pending window
    // turns control ON. A running status arriving after the window
    // expired does NOT retro-confirm (the deadline is checked here, not
    // just in the tick, so a late status can never confirm a dead ON).
    if (s_on_pending && s_server_running) {
        if (now < s_on_deadline_ms) {
            s_on_pending = false;
            s_enabled = true;
            ESP_LOGI(TAG, "ON confirmed by server status");
        } else {
            s_on_pending = false;
            ESP_LOGW(TAG, "Running status arrived after ON window, ignored");
        }
    }

    push_to_lcd();

    ESP_LOGI(TAG,
             "Status: running=%d speed=%d requested=%d limited=%d zone=%d alert=%s",
             s_server_running,
             s_server_speed,
             s_server_requested_speed,
             msg->safety_limited,
             s_server_zone,
             alert_to_str(s_server_alert));
}

#include "treadmill_ctrl.h"
#include "ble_client/ble_client.h"
#include "esp_log.h"

#define TAG "TREADMILL_CTRL"

static bool s_enabled = false;
static bool s_server_running = false;
static uint8_t s_server_speed = 0;
static uint8_t s_server_requested_speed = 0;
static uint8_t s_server_zone = 0;
static uint8_t s_server_alert = TREADMILL_ALERT_NONE;

static const char *alert_to_str(uint8_t alert)
{
    switch (alert) {
    case TREADMILL_ALERT_NONE:         return "NONE";
    case TREADMILL_ALERT_WARNING_1:    return "WARNING_1";
    case TREADMILL_ALERT_WARNING_2:    return "WARNING_2";
    case TREADMILL_ALERT_DANGER:       return "DANGER";
    case TREADMILL_ALERT_NO_PERSON:    return "NO_PERSON";
    case TREADMILL_ALERT_CMD_REJECTED: return "CMD_REJECTED";
    default:                           return "UNKNOWN";
    }
}

void treadmill_ctrl_init(void)
{
    s_enabled = false;
    ESP_LOGI(TAG, "initialized (control=OFF)");
}

void treadmill_ctrl_toggle(void)
{
    if (!ble_client_is_ready()) {
        ESP_LOGW(TAG, "BLE not ready, ignore toggle");
        return;
    }

    bool new_state = !s_enabled;
    uint8_t cmd = new_state ? TREADMILL_CMD_ON : TREADMILL_CMD_OFF;

    int rc = ble_client_write_command(cmd);
    if (rc == 0) {
        s_enabled = new_state;
        ESP_LOGI(TAG, "Treadmill control: %s", s_enabled ? "ON" : "OFF");
    } else {
        ESP_LOGE(TAG, "Toggle failed, keep control=%s",
                 s_enabled ? "ON" : "OFF");
    }
}

void treadmill_ctrl_speed_up(void)
{
    if (!s_enabled || !ble_client_is_ready()) {
        ESP_LOGW(TAG, "Ignore SPEED_UP: control disabled or BLE not ready");
        return;
    }

    int rc = ble_client_write_command(TREADMILL_CMD_SPEED_UP);
    if (rc == 0) {
        ESP_LOGI(TAG, "Command: SPEED_UP");
    }
}

void treadmill_ctrl_speed_down(void)
{
    if (!s_enabled || !ble_client_is_ready()) {
        ESP_LOGW(TAG, "Ignore SPEED_DOWN: control disabled or BLE not ready");
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

void treadmill_ctrl_handle_status(const uint8_t *data, uint16_t len)
{
    if (len != sizeof(treadmill_status_msg_t)) {
        ESP_LOGW(TAG, "Ignore invalid status len=%u", len);
        return;
    }

    const treadmill_status_msg_t *msg = (const treadmill_status_msg_t *)data;
    if (msg->type != TREADMILL_MSG_STATUS) {
        ESP_LOGW(TAG, "Ignore unknown msg type=0x%02X", msg->type);
        return;
    }

    s_server_running = msg->running != 0;
    s_server_speed = msg->speed;
    s_server_requested_speed = msg->requested_speed;
    s_server_zone = msg->zone;
    s_server_alert = msg->alert;

    if (!s_server_running) {
        s_enabled = false;
    }

    if (s_server_alert == TREADMILL_ALERT_CMD_REJECTED) {
        s_enabled = false;
    }

    ESP_LOGI(TAG,
             "Status: running=%d speed=%d requested=%d limited=%d zone=%d alert=%s",
             s_server_running,
             s_server_speed,
             s_server_requested_speed,
             msg->safety_limited,
             s_server_zone,
             alert_to_str(s_server_alert));
}

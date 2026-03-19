#include "treadmill_ctrl.h"
#include "ble_client/ble_client.h"
#include "esp_log.h"

#define TAG "TREADMILL_CTRL"

static bool s_running = false;

void treadmill_ctrl_init(void)
{
    s_running = false;
    ESP_LOGI(TAG, "initialized (state=OFF)");
}

void treadmill_ctrl_toggle(void)
{
    bool new_state = !s_running;
    uint8_t cmd = new_state ? TREADMILL_CMD_ON : TREADMILL_CMD_OFF;

    int rc = ble_client_write_command(cmd);
    if (rc == 0) {
        s_running = new_state;
        ESP_LOGI(TAG, "Treadmill: %s", s_running ? ">>> BẬT" : ">>> TẮT");
    } else {
        /* Giữ nguyên state nếu gửi thất bại */
        ESP_LOGE(TAG, "Toggle thất bại, giữ nguyên state: %s",
                 s_running ? "ON" : "OFF");
    }
}

void treadmill_ctrl_speed_up(void)
{
    int rc = ble_client_write_command(TREADMILL_CMD_SPEED_UP);
    if (rc == 0) {
        ESP_LOGI(TAG, "Lệnh: TĂNG TỐC");
    }
}

void treadmill_ctrl_speed_down(void)
{
    int rc = ble_client_write_command(TREADMILL_CMD_SPEED_DOWN);
    if (rc == 0) {
        ESP_LOGI(TAG, "Lệnh: GIẢM TỐC");
    }
}

bool treadmill_ctrl_is_running(void)
{
    return s_running;
}

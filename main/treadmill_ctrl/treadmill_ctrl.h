#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Module điều khiển treadmill phía Central (Smartkey).
 * Định nghĩa protocol command và cung cấp API toggle/speed_up/speed_down.
 * Quản lý state nội bộ và gửi lệnh qua ble_client.
 */

/* ---- Protocol command bytes — dùng chung cả 2 phía ---- */
#define TREADMILL_CMD_OFF        0x00
#define TREADMILL_CMD_ON         0x01
#define TREADMILL_CMD_SPEED_UP   0x02
#define TREADMILL_CMD_SPEED_DOWN 0x03

/** Khởi tạo / reset state về mặc định (tắt) */
void treadmill_ctrl_init(void);

/** Toggle bật/tắt — tự quản lý trạng thái running nội bộ */
void treadmill_ctrl_toggle(void);

void treadmill_ctrl_speed_up(void);
void treadmill_ctrl_speed_down(void);

bool treadmill_ctrl_is_running(void);

#ifdef __cplusplus
}
#endif

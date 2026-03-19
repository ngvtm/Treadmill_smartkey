#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Module quản lý nút bấm vật lý.
 * Hoàn toàn độc lập với BLE — chỉ phát sự kiện qua callback.
 * Tích hợp debounce 50ms.
 *
 * Sơ đồ nối: Nút nối GPIO <-> GND, dùng pull-up nội.
 * GPIO15 = Toggle, GPIO2 = Speed Up, GPIO4 = Speed Down.
 */

typedef enum {
    BUTTON_ID_TOGGLE     = 0,   /* D15 — Bật/Tắt treadmill */
    BUTTON_ID_SPEED_UP   = 1,   /* D2  — Tăng tốc          */
    BUTTON_ID_SPEED_DOWN = 2,   /* D4  — Giảm tốc          */
    BUTTON_ID_MAX
} button_id_t;

/**
 * Callback được gọi từ FreeRTOS task (không phải ISR) sau khi debounce.
 * @param id  ID của nút vừa được nhấn
 */
typedef void (*button_callback_t)(button_id_t id);

/**
 * Khởi tạo GPIO, ISR và task xử lý nút bấm.
 * @param cb  Hàm callback sẽ được gọi mỗi khi có nút nhấn hợp lệ
 */
void button_init(button_callback_t cb);

#ifdef __cplusplus
}
#endif

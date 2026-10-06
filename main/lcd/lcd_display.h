#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "treadmill_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Module hiển thị của smartkey.
 *
 * Sở hữu một task riêng: các module khác chỉ đẩy trạng thái vào bằng
 * lcd_display_set_*(), task sẽ vẽ lại đúng những trường đã đổi. Các hàm
 * set_* an toàn khi gọi từ task NimBLE hoặc task nút bấm và không block
 * chờ SPI.
 */

/* Mức tốc độ lớn nhất, dùng để vẽ thanh bar. */
#ifndef LCD_SPEED_MAX
#define LCD_SPEED_MAX 10
#endif

typedef enum {
    LCD_LINK_DISCONNECTED = 0, /* chưa/mất kết nối tới bàn chạy */
    LCD_LINK_SCANNING,         /* đang quét tìm bàn chạy */
    LCD_LINK_CONNECTED,        /* đã kết nối và sẵn sàng gửi lệnh */
} lcd_link_state_t;

typedef struct {
    bool              control_enabled; /* smartkey đang bật điều khiển */
    bool              running;         /* bàn chạy báo đang chạy */
    uint8_t           speed;           /* mức tốc độ hiện tại */
    uint8_t           requested_speed; /* mức tốc độ đã yêu cầu */
    bool              safety_limited;  /* bàn chạy đang giới hạn vì an toàn */
    uint8_t           zone;            /* vùng an toàn báo về */
    treadmill_alert_t alert;           /* cảnh báo hiện tại */
} lcd_status_t;

/** Khởi tạo panel và task vẽ. Trả về lỗi nếu không init được màn hình;
 *  khi đó phần còn lại của smartkey vẫn chạy bình thường. */
esp_err_t lcd_display_init(void);

/** Cập nhật trạng thái kết nối BLE trên thanh tiêu đề. */
void lcd_display_set_link(lcd_link_state_t link);

/** Cập nhật toàn bộ trạng thái bàn chạy. */
void lcd_display_set_status(const lcd_status_t *st);

void lcd_display_backlight(bool on);

#ifdef __cplusplus
}
#endif

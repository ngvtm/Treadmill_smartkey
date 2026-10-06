#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lớp vẽ mức thấp cho e-paper Good Display qua bo chuyển DESPI-C02.
 *
 * Khác TFT ở ba điểm cốt lõi:
 *   - 1 bit/pixel, không phải RGB565. Framebuffer đủ nhỏ để giữ nguyên
 *     trong RAM (~4,7 KB cho tấm 128x296) nên vẽ vào RAM rồi đẩy một lần,
 *     thay vì vẽ trực tiếp từng dải như trước.
 *   - Có chân BUSY: panel kéo lên khi đang refresh, mọi lệnh phải chờ.
 *   - Refresh tốn 1-3 giây, nên không vẽ lại từng ô. Đổi gì cũng dựng lại
 *     cả khung rồi refresh một lần.
 *
 * ---- ĐẤU NỐI (DESPI-C02, theo thứ tự in trên bo) ----
 *   BUSY  -> GPIO4    ngõ VÀO, panel báo đang bận
 *   RES   -> GPIO25
 *   D/C   -> GPIO26
 *   CS    -> GPIO27
 *   SCK   -> GPIO32
 *   SDI   -> GPIO33
 *   GND   -> GND
 *   3.3V  -> 3V3      DESPI-C02 chạy 3,3 V
 *
 * Nút bấm giữ GPIO21/22/23. Không chân nào ở đây là strapping, không đụng
 * flash 6-11, UART0, hay vùng chỉ-vào 34-39.
 *
 * Nhớ kiểm tra công tắc DIP trên DESPI-C02: 2,2 Ohm cho tấm nhỏ
 * (1,54"-2,9"), 0,47 Ohm cho tấm lớn. Sai thì panel không refresh nổi.
 */
#ifndef EPD_PIN_BUSY
#define EPD_PIN_BUSY  4
#endif
#ifndef EPD_PIN_RST
#define EPD_PIN_RST  25
#endif
#ifndef EPD_PIN_DC
#define EPD_PIN_DC   26
#endif
#ifndef EPD_PIN_CS
#define EPD_PIN_CS   27
#endif
#ifndef EPD_PIN_SCK
#define EPD_PIN_SCK  32
#endif
#ifndef EPD_PIN_SDI
#define EPD_PIN_SDI  33
#endif

#ifndef EPD_SPI_HZ
#define EPD_SPI_HZ (4 * 1000 * 1000)   /* SSD168x chịu tới ~20 MHz */
#endif

/*
 * ---- CHỌN TẤM ----
 * DESPI-C02 chỉ là bo chuyển; tấm e-paper mới quyết định các số dưới đây.
 * Mặc định đang đặt cho GDEY029T94 (2,9", chip SSD1680, native 128x296).
 *
 * Đổi tấm thì sửa đúng ba dòng:
 *   1,54" GDEY0154D67  ->  200 x 200
 *   2,13" GDEY0213B74  ->  122 x 250   (bề rộng làm tròn lên 128)
 *   2,9"  GDEY029T94   ->  128 x 296
 *   4,2"  GDEY042T81   ->  400 x 300
 *
 * EPD_PANEL_W phải là bội của 8 (RAM của SSD168x đóng gói 8 pixel/byte).
 */
#ifndef EPD_PANEL_W
#define EPD_PANEL_W 128
#endif
#ifndef EPD_PANEL_H
#define EPD_PANEL_H 296
#endif

/* 0 = dọc (cáp ở dưới), 1 = xoay ngang. */
#ifndef EPD_ROTATION
#define EPD_ROTATION 0
#endif

#if EPD_ROTATION
#define LCD_W EPD_PANEL_H
#define LCD_H EPD_PANEL_W
#else
#define LCD_W EPD_PANEL_W
#define LCD_H EPD_PANEL_H
#endif

/* Chỉ còn hai "màu". Mọi thứ trước đây mã hoá bằng màu giờ phải mã hoá
 * bằng hình khối: khung, nền đảo, độ đậm. */
#define LCD_PAPER 0   /* nền trắng */
#define LCD_INK   1   /* mực đen  */

/* Font 5x7 trong ô 6x8 (1 cột + 1 hàng cách chữ). */
#define LCD_GLYPH_W 6
#define LCD_GLYPH_H 8

/** Bật SPI, cấp phát framebuffer, reset và init panel. Gọi một lần. */
esp_err_t lcd_gfx_init(void);

/** Tô toàn bộ framebuffer. Chưa đẩy lên panel. */
void lcd_gfx_clear(uint8_t color);

/** Tô đặc hình chữ nhật vào framebuffer (tự cắt theo biên). */
void lcd_gfx_fill_rect(int x, int y, int w, int h, uint8_t color);

/** Vẽ viền hình chữ nhật dày `t` pixel. */
void lcd_gfx_rect(int x, int y, int w, int h, int t, uint8_t color);

/** Bề rộng pixel của chuỗi ở tỉ lệ `scale`. */
int lcd_gfx_text_width(const char *s, int scale);

/** Tỉ lệ lớn nhất (1..max) mà chuỗi còn lọt bề rộng `w`. Trả 0 nếu không
 *  lọt nổi ngay cả ở tỉ lệ 1 — dùng để bố cục tự co theo kích thước tấm. */
int lcd_gfx_fit_scale(const char *s, int w, int max_scale);

/** Vẽ chuỗi tại (x, y). Chỉ ASCII 32..90; chữ thường tự đổi thành in hoa. */
void lcd_gfx_text(int x, int y, const char *s, int scale,
                  uint8_t fg, uint8_t bg);

/** Vẽ chuỗi canh giữa dải ngang [x0, x0 + w). */
void lcd_gfx_text_center(int x0, int w, int y, const char *s, int scale,
                         uint8_t fg, uint8_t bg);

/** Xóa ô rồi vẽ chuỗi canh giữa cả hai chiều. */
void lcd_gfx_text_box(int x, int y, int w, int h, const char *s, int scale,
                      uint8_t fg, uint8_t bg);

/** Đẩy framebuffer lên panel và chờ refresh xong. Tốn 1-3 giây. */
void lcd_gfx_flush(void);

/** Đưa panel về chế độ ngủ sâu. Gọi khi không cập nhật nữa một thời gian
 *  dài; lần vẽ kế tiếp lcd_gfx_flush() sẽ tự đánh thức. */
void lcd_gfx_sleep(void);

/** Số glyph bị bỏ vì không lọt màn. Khác 0 nghĩa là có chuỗi dài hơn ô
 *  chứa nó — chữ mất âm thầm chứ không tràn ra ngoài. */
uint32_t lcd_gfx_dropped(void);
void     lcd_gfx_reset_dropped(void);

#ifdef __cplusplus
}
#endif

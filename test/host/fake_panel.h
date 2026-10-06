#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lcd_gfx.h"

/* Kich thuoc logic, tu dong theo tam va goc xoay dang cau hinh. */
#define FP_W LCD_W
#define FP_H LCD_H

/*
 * Panel e-paper gia: giai ma lenh SSD1680 va dung lai anh 1 bpp thanh
 * mang 1 byte/pixel (LCD_INK / LCD_PAPER) de test assert tren pixel that.
 */
extern uint8_t fp_fb[FP_H][FP_W];

extern int fp_flushes;   /* so lan ghi RAM (0x24) ke tu fp_reset() */
extern int fp_bytes;     /* tong byte anh da gui */
extern int fp_oob;       /* so lan ghi sai kich thuoc hoac ra ngoai khung */

/* Trang thai panel dung lai tu chuoi lenh init. */
extern int fp_saw_swreset;
extern int fp_saw_master_act;  /* moi lan kich hoat refresh */
extern int fp_driver_mux;      /* tham so 0x01, phai la EPD_PANEL_H - 1 */
extern int fp_data_entry;      /* tham so 0x11 */
extern int fp_ram_x_end;       /* tham so 0x44, phai la stride - 1 */
extern int fp_ram_y_end;       /* tham so 0x45 */
extern int fp_update_seq;      /* tham so 0x22 */
extern int fp_deep_sleep;      /* tham so 0x10 */

void fp_reset(void);
void fp_clear(uint8_t color);

/* Dem pixel mang gia tri `c` trong vung cho truoc. */
int  fp_count(int x, int y, int w, int h, uint8_t c);
/* 1 neu ca vung chi mot gia tri duy nhat. */
int  fp_uniform(int x, int y, int w, int h, uint8_t c);
/* So pixel mang muc den trong vung. */
int  fp_ink(int x, int y, int w, int h);

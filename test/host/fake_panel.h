#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lcd_gfx.h"

/* Kich thuoc panel that (landscape sau khi xoay 90 do, xem lcd_gfx.c). */
#define FP_W PANEL_W
#define FP_H PANEL_H

/*
 * Panel TFT gia: giai ma lenh ILI9341/ST7789 (CASET/RASET/RAMWR),
 * dung lai anh RGB565 thanh mang palette (LCD_PAPER/INK/RED/GREEN)
 * de test assert tren pixel that.
 */
extern uint8_t fp_fb[FP_H][FP_W];

extern int fp_frames;      /* so khung day du (dai RAMWR bat dau tu y=0) */
extern int fp_ramwr;       /* so lan ghi RAMWR (so dai) */
extern int fp_bytes;       /* tong byte mau da gui */
extern int fp_oob;         /* so lan ghi sai cua so / ra ngoai khung / mau la */

/* Trang thai panel dung lai tu chuoi lenh. */
extern int fp_saw_swreset;
extern int fp_saw_slpout;
extern int fp_saw_dispon;
extern int fp_saw_dispoff;
extern int fp_saw_slpin;
extern int fp_saw_invon;
extern int fp_colmod;      /* tham so 0x3A, phai la 0x55 (RGB565) */
extern int fp_madctl;      /* tham so 0x36 */
extern int fp_win_x0, fp_win_x1, fp_win_y0, fp_win_y1; /* cua so gan nhat */

void fp_reset(void);
void fp_clear(uint8_t color);

/* Dem pixel mang gia tri `c` trong vung cho truoc (toa do panel). */
int  fp_count(int x, int y, int w, int h, uint8_t c);
/* Ban dem theo toa do framebuffer app (portrait): anh xa vung fb sang
 * vung panel xoay 90 do (fb (x,y) -> panel (LCD_H-1-y, x)). */
int  fp_count_fb(int x, int y, int w, int h, uint8_t c);
int  fp_uniform_fb(int x, int y, int w, int h, uint8_t c);
int  fp_ink_fb(int x, int y, int w, int h);
/* 1 neu ca vung chi mot gia tri duy nhat. */
int  fp_uniform(int x, int y, int w, int h, uint8_t c);
/* So pixel mang muc den trong vung. */
int  fp_ink(int x, int y, int w, int h);

#pragma once

// Pure 3-point touch calibration for the XPT2046 (host-testable).
// A 3-point affine map handles scale, offset, axis swap and flips in
// one shot, so mounting orientation does not matter: the user taps
// three on-screen targets and the math absorbs whatever the raw axes
// do. Screen coordinates are the TFT's 240x320 space.
//
//   screen_x = (a * raw_x + b * raw_y + c) / div
//   screen_y = (d * raw_x + e * raw_y + f) / div
//
// Solved exactly with Cramer's rule in int64 (raw values are 12-bit,
// so no overflow: 4095 * 320 * 4095 stays far below int64).

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t raw_x, raw_y;
} touch_raw_t;

typedef struct {
    int32_t x, y;
} touch_screen_t;

typedef struct {
    int64_t a, b, c;
    int64_t d, e, f;
    int64_t div;
    bool valid;
} touch_cal_t;

// Compute calibration from 3 (raw, screen) pairs. Returns false when
// the three raw points are collinear (degenerate — the user tapped a
// line, not a triangle); the caller must redo the calibration.
static inline bool touch_cal_compute(touch_cal_t *cal,
                                     const touch_raw_t raw[3],
                                     const touch_screen_t scr[3])
{
    int64_t x0 = raw[0].raw_x, y0 = raw[0].raw_y;
    int64_t x1 = raw[1].raw_x, y1 = raw[1].raw_y;
    int64_t x2 = raw[2].raw_x, y2 = raw[2].raw_y;

    // Determinant of the raw-point triangle (2x signed area).
    int64_t div = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (div == 0) {
        cal->valid = false;
        cal->div = 0;
        return false;
    }
    int64_t sx0 = scr[0].x, sx1 = scr[1].x, sx2 = scr[2].x;
    int64_t sy0 = scr[0].y, sy1 = scr[1].y, sy2 = scr[2].y;

    cal->a = (sx1 - sx0) * (y2 - y0) - (sx2 - sx0) * (y1 - y0);
    cal->b = (x1 - x0) * (sx2 - sx0) - (x2 - x0) * (sx1 - sx0);
    cal->c = sx0 * div - cal->a * x0 - cal->b * y0;
    cal->d = (sy1 - sy0) * (y2 - y0) - (sy2 - sy0) * (y1 - y0);
    cal->e = (x1 - x0) * (sy2 - sy0) - (x2 - x0) * (sy1 - sy0);
    cal->f = sy0 * div - cal->d * x0 - cal->e * y0;
    cal->div = div;
    cal->valid = true;
    return true;
}

// Map one raw point through a calibration. Invalid calibration maps
// to (-1, -1) so the caller can ignore the touch.
static inline touch_screen_t touch_cal_map(const touch_cal_t *cal,
                                           touch_raw_t raw)
{
    touch_screen_t out = { -1, -1 };
    if (!cal->valid || cal->div == 0) {
        return out;
    }
    out.x = (int32_t)((cal->a * raw.raw_x + cal->b * raw.raw_y +
                       cal->c) / cal->div);
    out.y = (int32_t)((cal->d * raw.raw_x + cal->e * raw.raw_y +
                       cal->f) / cal->div);
    return out;
}

// Fallback when no calibration is stored yet: raw 0..4095 maps
// linearly onto the screen, no swap, no flip. Only good enough to
// run the calibration screen itself (whose targets are big).
static inline touch_cal_t touch_cal_default(int32_t screen_w,
                                            int32_t screen_h)
{
    touch_cal_t cal;
    cal.a = screen_w;
    cal.b = 0;
    cal.c = 0;
    cal.d = 0;
    cal.e = screen_h;
    cal.f = 0;
    cal.div = 4095;
    cal.valid = true;
    return cal;
}

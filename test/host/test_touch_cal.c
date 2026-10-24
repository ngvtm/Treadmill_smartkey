// Host-based unit tests for touch_calibration.h (Phase 2).
// Build: gcc -std=gnu17 -Wall -I../../main -o /tmp/test_touch_cal test_touch_cal.c

#include <stdio.h>
#include "touch/touch_calibration.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                             \
    g_checks++;                                                      \
    if (!(cond)) {                                                   \
        g_failures++;                                                \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__);           \
    }                                                                \
} while (0)

int main(void)
{
    printf("touch calibration:\n");

    // 1. Identity-ish mapping: raw corners map to screen corners.
    {
        touch_raw_t raw[3] = { {0, 0}, {4095, 0}, {0, 4095} };
        touch_screen_t scr[3] = { {0, 0}, {240, 0}, {0, 320} };
        touch_cal_t cal;
        CHECK(touch_cal_compute(&cal, raw, scr));
        touch_screen_t m = touch_cal_map(&cal, (touch_raw_t){2048, 2048});
        CHECK(m.x >= 118 && m.x <= 122);
        CHECK(m.y >= 158 && m.y <= 162);
        m = touch_cal_map(&cal, (touch_raw_t){0, 0});
        CHECK(m.x == 0 && m.y == 0);
        m = touch_cal_map(&cal, (touch_raw_t){4095, 4095});
        CHECK(m.x >= 238 && m.x <= 242);
        CHECK(m.y >= 318 && m.y <= 322);
    }

    // 2. Swapped + flipped axes (touch mounted rotated 90 deg): the
    //    affine map must still reproduce the calibration points.
    {
        touch_raw_t raw[3] = { {400, 3800}, {3700, 3800}, {400, 300} };
        touch_screen_t scr[3] = { {30, 30}, {210, 30}, {30, 290} };
        touch_cal_t cal;
        CHECK(touch_cal_compute(&cal, raw, scr));
        for (int i = 0; i < 3; i++) {
            touch_screen_t m = touch_cal_map(&cal, raw[i]);
            CHECK(m.x >= scr[i].x - 2 && m.x <= scr[i].x + 2);
            CHECK(m.y >= scr[i].y - 2 && m.y <= scr[i].y + 2);
        }
    }

    // 3. Collinear raw points are rejected.
    {
        touch_raw_t raw[3] = { {0, 0}, {1000, 1000}, {2000, 2000} };
        touch_screen_t scr[3] = { {0, 0}, {120, 160}, {240, 320} };
        touch_cal_t cal;
        CHECK(!touch_cal_compute(&cal, raw, scr));
        touch_screen_t m = touch_cal_map(&cal, (touch_raw_t){500, 500});
        CHECK(m.x == -1 && m.y == -1);
    }

    // 4. Default mapping spans the screen.
    {
        touch_cal_t cal = touch_cal_default(240, 320);
        touch_screen_t m = touch_cal_map(&cal, (touch_raw_t){4095, 4095});
        CHECK(m.x == 240 && m.y == 320);
        m = touch_cal_map(&cal, (touch_raw_t){0, 0});
        CHECK(m.x == 0 && m.y == 0);
    }

    printf("  %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("ALL PASS: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    return 1;
}

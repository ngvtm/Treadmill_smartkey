// Host-based unit tests for touch/touch_controls_logic.h (touch controls).
// Build: gcc -std=gnu17 -Wall -I../../main -o /tmp/test_touch_controls test_touch_controls.c

#include <stdio.h>
#include "touch/touch_controls_logic.h"

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
    printf("touch controls:\n");

    // 1. Zone geometry: centres, edges, gaps, outside areas.
    {
        CHECK(touch_zone_at(40, 280) == TOUCH_ZONE_DOWN);
        CHECK(touch_zone_at(120, 280) == TOUCH_ZONE_POWER);
        CHECK(touch_zone_at(200, 280) == TOUCH_ZONE_UP);
        CHECK(touch_zone_at(40, 243) == TOUCH_ZONE_NONE);   // above bar
        CHECK(touch_zone_at(40, 244) == TOUCH_ZONE_DOWN);   // top row
        CHECK(touch_zone_at(40, 316) == TOUCH_ZONE_DOWN);   // bottom row
        CHECK(touch_zone_at(40, 317) == TOUCH_ZONE_NONE);   // below bar
        CHECK(touch_zone_at(2, 280) == TOUCH_ZONE_NONE);    // left margin
        CHECK(touch_zone_at(3, 280) == TOUCH_ZONE_DOWN);
        CHECK(touch_zone_at(78, 280) == TOUCH_ZONE_DOWN);
        CHECK(touch_zone_at(79, 280) == TOUCH_ZONE_NONE);   // gap
        CHECK(touch_zone_at(81, 280) == TOUCH_ZONE_NONE);   // gap
        CHECK(touch_zone_at(82, 280) == TOUCH_ZONE_POWER);
        CHECK(touch_zone_at(157, 280) == TOUCH_ZONE_POWER);
        CHECK(touch_zone_at(158, 280) == TOUCH_ZONE_NONE);  // gap
        CHECK(touch_zone_at(160, 280) == TOUCH_ZONE_NONE);  // gap
        CHECK(touch_zone_at(161, 280) == TOUCH_ZONE_UP);
        CHECK(touch_zone_at(236, 280) == TOUCH_ZONE_UP);
        CHECK(touch_zone_at(237, 280) == TOUCH_ZONE_NONE);  // right margin
        CHECK(touch_zone_at(120, 10) == TOUCH_ZONE_NONE);   // header area
        CHECK(touch_zone_at(120, 150) == TOUCH_ZONE_NONE);  // middle
    }

    // 2. Stopped POWER hold: nothing before 1500 ms, ON once, no refire.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 1000, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 1499, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 1500, false) == TOUCH_CTRL_ON);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 3000, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, false, 3100, false) == TOUCH_CTRL_NONE);
        // Second hold fires again.
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 4000, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 5500, false) == TOUCH_CTRL_ON);
    }

    // 3. Stopped POWER early release: never ON.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 1000, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, false, 1100, false) == TOUCH_CTRL_NONE);
    }

    // 4. Running POWER tap: OFF immediately, no refire while held.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0, true) == TOUCH_CTRL_OFF);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 500, true) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, false, 600, true) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 700, true) == TOUCH_CTRL_OFF);
    }

    // 5. Hold through the stopped->running transition: no spurious OFF.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 1500, false) == TOUCH_CTRL_ON);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 2000, true) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, false, 2100, true) == TOUCH_CTRL_NONE);
    }

    // 6. Speed zones: edge fires once, no repeat while held, refires after release.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_DOWN, true, 0, true) == TOUCH_CTRL_SPEED_DOWN);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_DOWN, true, 100, true) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, false, 200, true) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_DOWN, true, 300, true) == TOUCH_CTRL_SPEED_DOWN);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_UP, true, 400, true) == TOUCH_CTRL_NONE); // slide DOWN->UP without release: cancel, no action
    }

    // 7. Slide-out cancels and requires release before any new action.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_UP, true, 500, false) == TOUCH_CTRL_NONE); // slide into UP: cancel
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_UP, true, 2000, false) == TOUCH_CTRL_NONE); // held in UP: still nothing
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, false, 2100, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_UP, true, 2200, false) == TOUCH_CTRL_SPEED_UP);
    }

    // 8. Touches outside the bar never act.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, true, 0, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_NONE, true, 5000, false) == TOUCH_CTRL_NONE);
    }

    // 9. Hold progress fraction for the display fill.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_hold_frac(&st, 0, false) == 0);
        touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0, false);
        CHECK(touch_ctrl_hold_frac(&st, 750, false) == 50);
        CHECK(touch_ctrl_hold_frac(&st, 1499, false) == 99);
        touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 1500, false); // ON fires
        CHECK(touch_ctrl_hold_frac(&st, 1600, false) == 0); // fired: no fill
        touch_ctrl_init(&st);
        touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0, true);
        CHECK(touch_ctrl_hold_frac(&st, 750, true) == 0); // running: no fill
    }

    // 10. Timestamp wrap: hold across the uint32 boundary still fires.
    {
        touch_ctrl_state_t st;
        touch_ctrl_init(&st);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0xFFFFFF00u, false) == TOUCH_CTRL_NONE);
        CHECK(touch_ctrl_update(&st, TOUCH_ZONE_POWER, true, 0xFFFFFF00u + 1500u, false) == TOUCH_CTRL_ON);
    }

    printf("  %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

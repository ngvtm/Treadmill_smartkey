/* Host-based unit tests for the pure button state machine in
 * button_logic.h (Phase 1, S1.3). No ESP-IDF needed.
 * Build: gcc -std=gnu17 -Wall -I../../main -o /tmp/test_button_sm test_button_sm.c
 */

#include <stdio.h>
#include "button/button_logic.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                             \
    g_checks++;                                                      \
    if (!(cond)) {                                                   \
        g_failures++;                                                \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__);          \
    }                                                                \
} while (0)

static const button_sm_cfg_t CFG = {
    .debounce_ms = 50, .hold_ms = 1500, .stuck_ms = 5000, .min_gap_ms = 250,
};

static button_event_t at(button_sm_t *sm, int pressed, unsigned t)
{
    return button_sm_update(sm, pressed != 0, t, &CFG);
}

int main(void)
{
    printf("button state machine:\n");

    /* 1. Quick press: debounce 50 ms, then exactly one PRESS. */
    {
        button_sm_t sm; button_sm_init(&sm, false);
        CHECK(at(&sm, 0, 0) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 20) == BTN_EV_NONE);   /* debounce starts */
        CHECK(at(&sm, 1, 40) == BTN_EV_NONE);   /* 20 ms < 50 */
        CHECK(at(&sm, 1, 70) == BTN_EV_PRESS);  /* 50 ms reached */
        CHECK(at(&sm, 1, 100) == BTN_EV_NONE);  /* held: no repeat */
        CHECK(at(&sm, 0, 120) == BTN_EV_NONE);  /* release */
    }

    /* 2. Bounce: low for less than debounce, then high -> no PRESS. */
    {
        button_sm_t sm; button_sm_init(&sm, false);
        CHECK(at(&sm, 1, 20) == BTN_EV_NONE);
        CHECK(at(&sm, 0, 40) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 60) == BTN_EV_NONE);   /* new press starts over */
        CHECK(at(&sm, 1, 90) == BTN_EV_NONE);   /* 30 ms < 50 */
        CHECK(at(&sm, 1, 110) == BTN_EV_PRESS); /* 50 ms from 60 */
    }

    /* 3. Hold: PRESS, then exactly one HOLD at 1.5 s, then silence. */
    {
        button_sm_t sm; button_sm_init(&sm, false);
        CHECK(at(&sm, 1, 20) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 70) == BTN_EV_PRESS);
        CHECK(at(&sm, 1, 1000) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 1519) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 1520) == BTN_EV_HOLD);
        CHECK(at(&sm, 1, 3000) == BTN_EV_NONE); /* held: no repeat HOLD */
        CHECK(at(&sm, 0, 3100) == BTN_EV_NONE);
    }

    /* 4. Stuck: held past 5 s -> nothing until release; then normal. */
    {
        button_sm_t sm; button_sm_init(&sm, false);
        CHECK(at(&sm, 1, 20) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 70) == BTN_EV_PRESS);
        CHECK(at(&sm, 1, 1520) == BTN_EV_HOLD);
        CHECK(at(&sm, 1, 5019) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 5020) == BTN_EV_NONE); /* -> STUCK, silent */
        CHECK(at(&sm, 1, 9000) == BTN_EV_NONE); /* still stuck: silent */
        CHECK(at(&sm, 0, 9100) == BTN_EV_NONE); /* release clears */
        CHECK(at(&sm, 1, 9120) == BTN_EV_NONE); /* debounce again */
        CHECK(at(&sm, 1, 9170) == BTN_EV_PRESS);/* works again */
    }

    /* 5. Pressed at boot -> STUCK until release, never fires on its own. */
    {
        button_sm_t sm; button_sm_init(&sm, true);
        CHECK(at(&sm, 1, 20) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 2000) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 9000) == BTN_EV_NONE);
        CHECK(at(&sm, 0, 9100) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 9120) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 9170) == BTN_EV_PRESS);
    }

    /* 6. Min gap: a second press within 250 ms of the last PRESS is
     * chatter — suppressed, and the next press after the gap fires. */
    {
        button_sm_t sm; button_sm_init(&sm, false);
        CHECK(at(&sm, 1, 20) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 70) == BTN_EV_PRESS);   /* last_press = 70 */
        CHECK(at(&sm, 0, 100) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 120) == BTN_EV_NONE);   /* debounce */
        CHECK(at(&sm, 1, 170) == BTN_EV_NONE);   /* 100 ms < 250: suppressed */
        CHECK(at(&sm, 0, 200) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 220) == BTN_EV_NONE);
        CHECK(at(&sm, 1, 320) == BTN_EV_PRESS);  /* 250 ms from 70: fires */
    }

    printf("  %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("ALL PASS: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    return 1;
}

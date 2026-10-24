#pragma once

// Pure, hardware-independent touch-control logic (Phase 1 touch controls).
// No ESP-IDF / FreeRTOS dependencies so it can be unit-tested on host.
// The firmware layer (touch_menu.c task) maps raw samples to screen
// coordinates and feeds them in; all zone and gesture decisions live here.
//
// Geometry: the main screen's control bar, shared with lcd_display.c,
// which draws exactly these rects. Half-open intervals: a point is in
// [lo, hi). The bar sits at the bottom of the 240x320 screen, below the
// alert band; the header (menu-entry) area is far above it and never
// overlaps.
//
// Gestures (deliberately asymmetric: starting is hard, stopping is easy):
//   POWER zone, stopped: hold >= 1500 ms -> ON (fires once per hold).
//   POWER zone, running: press -> OFF immediately (never gated here;
//                          treadmill_ctrl gates nothing on OFF either).
//   SPEED zones: press edge -> SPEED_UP / SPEED_DOWN once (no repeat
//                while held; treadmill_ctrl rate-limits to 1 step/300 ms).
//   Sliding out of the pressed zone cancels the press, and no new press
//   counts until the finger is released (a slide across the bar must
//   never fire a neighbouring zone's action).

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Control bar geometry (screen pixels, 240x320 portrait).
#define TOUCH_CTL_Y0 244
#define TOUCH_CTL_Y1 317  // exclusive: bar rows 244..316
#define TOUCH_CTL_DOWN_X0 3
#define TOUCH_CTL_DOWN_X1 79
#define TOUCH_CTL_POWER_X0 82
#define TOUCH_CTL_POWER_X1 158
#define TOUCH_CTL_UP_X0 161
#define TOUCH_CTL_UP_X1 237

#define TOUCH_CTRL_HOLD_ON_MS 1500

typedef enum {
    TOUCH_ZONE_NONE = 0,
    TOUCH_ZONE_DOWN,
    TOUCH_ZONE_POWER,
    TOUCH_ZONE_UP,
} touch_zone_t;

typedef enum {
    TOUCH_CTRL_NONE = 0,
    TOUCH_CTRL_ON,
    TOUCH_CTRL_OFF,
    TOUCH_CTRL_SPEED_UP,
    TOUCH_CTRL_SPEED_DOWN,
} touch_ctrl_action_t;

typedef struct {
    touch_zone_t pressed_zone;
    uint32_t pressed_since_ms;
    bool fired;          // action for this press already emitted
    bool needs_release;  // set by a slide-out cancel; cleared on release
} touch_ctrl_state_t;

static inline void touch_ctrl_init(touch_ctrl_state_t *st)
{
    st->pressed_zone = TOUCH_ZONE_NONE;
    st->pressed_since_ms = 0;
    st->fired = false;
    st->needs_release = false;
}

static inline touch_zone_t touch_zone_at(int x, int y)
{
    if (y < TOUCH_CTL_Y0 || y >= TOUCH_CTL_Y1) {
        return TOUCH_ZONE_NONE;
    }
    if (x >= TOUCH_CTL_DOWN_X0 && x < TOUCH_CTL_DOWN_X1) {
        return TOUCH_ZONE_DOWN;
    }
    if (x >= TOUCH_CTL_POWER_X0 && x < TOUCH_CTL_POWER_X1) {
        return TOUCH_ZONE_POWER;
    }
    if (x >= TOUCH_CTL_UP_X0 && x < TOUCH_CTL_UP_X1) {
        return TOUCH_ZONE_UP;
    }
    return TOUCH_ZONE_NONE;
}

static inline touch_ctrl_action_t touch_ctrl_update(touch_ctrl_state_t *st,
                                                    touch_zone_t zone,
                                                    bool touched,
                                                    uint32_t now_ms,
                                                    bool running)
{
    if (!touched) {
        st->pressed_zone = TOUCH_ZONE_NONE;
        st->fired = false;
        st->needs_release = false;
        return TOUCH_CTRL_NONE;
    }
    if (st->needs_release) {
        return TOUCH_CTRL_NONE;
    }
    if (st->pressed_zone == TOUCH_ZONE_NONE) {
        if (zone == TOUCH_ZONE_NONE) {
            return TOUCH_CTRL_NONE;
        }
        st->pressed_zone = zone;
        st->pressed_since_ms = now_ms;
        st->fired = false;
        // Press-edge actions: OFF when running, speed steps always.
        // ON is hold-gated below, never on press.
        if (zone == TOUCH_ZONE_POWER && running) {
            st->fired = true;
            return TOUCH_CTRL_OFF;
        }
        if (zone == TOUCH_ZONE_DOWN) {
            st->fired = true;
            return TOUCH_CTRL_SPEED_DOWN;
        }
        if (zone == TOUCH_ZONE_UP) {
            st->fired = true;
            return TOUCH_CTRL_SPEED_UP;
        }
        return TOUCH_CTRL_NONE;
    }
    // Still touching the same zone.
    if (zone != st->pressed_zone) {
        // Slide-out (including into a gap): cancel, require release.
        st->pressed_zone = TOUCH_ZONE_NONE;
        st->fired = false;
        st->needs_release = true;
        return TOUCH_CTRL_NONE;
    }
    if (st->pressed_zone == TOUCH_ZONE_POWER && !running && !st->fired &&
        (now_ms - st->pressed_since_ms) >= TOUCH_CTRL_HOLD_ON_MS) {
        st->fired = true;
        return TOUCH_CTRL_ON;
    }
    return TOUCH_CTRL_NONE;
}

// Hold progress for the display's POWER-zone fill (0..100). Only while a
// stopped-machine POWER hold is in progress; 0 otherwise.
static inline uint8_t touch_ctrl_hold_frac(const touch_ctrl_state_t *st,
                                           uint32_t now_ms, bool running)
{
    if (st->pressed_zone != TOUCH_ZONE_POWER || running || st->fired) {
        return 0;
    }
    uint32_t held = now_ms - st->pressed_since_ms;
    uint32_t frac = held * 100u / TOUCH_CTRL_HOLD_ON_MS;
    return frac > 100u ? 100u : (uint8_t)frac;
}

#ifdef __cplusplus
}
#endif

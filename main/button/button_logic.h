#pragma once

// Pure, hardware-independent button state machine (Phase 1, S1.3).
// No ESP-IDF / FreeRTOS dependencies so it can be unit-tested on host.
// The firmware layer (button.c) polls the GPIOs and feeds levels in;
// all press/hold/stuck decisions live here.
//
// Events:
//   PRESS — button validated pressed (debounced). One per press.
//   HOLD  — button still held at hold_ms. Fired once per press; the
//           main layer uses it for hold-to-start on the toggle button
//           and ignores it for the speed buttons.
//   (nothing) — while STUCK, or while debouncing, no events fire.
// A button already pressed at boot starts STUCK: a key wedged down
// before power-on must never fire a PRESS or HOLD on its own.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_EV_NONE = 0,
    BTN_EV_PRESS,
    BTN_EV_HOLD,
} button_event_t;

typedef struct {
    uint32_t debounce_ms;  // stable-low time before a press counts
    uint32_t hold_ms;      // held time before HOLD fires
    uint32_t stuck_ms;     // held time after which the button is stuck
    uint32_t min_gap_ms;   // minimum time between two PRESS events
} button_sm_cfg_t;

typedef enum {
    BTN_ST_IDLE = 0,
    BTN_ST_DEBOUNCE,
    BTN_ST_PRESSED,
    BTN_ST_HELD,
    BTN_ST_STUCK,
} button_sm_state_t;

typedef struct {
    button_sm_state_t state;
    uint32_t pressed_since_ms;  // first low sample of the current press
    uint32_t last_press_ms;     // last fired PRESS (for min-gap)
    bool     has_last_press;
} button_sm_t;

static inline void button_sm_init(button_sm_t *sm, bool initially_pressed)
{
    sm->state = initially_pressed ? BTN_ST_STUCK : BTN_ST_IDLE;
    sm->pressed_since_ms = 0;
    sm->last_press_ms = 0;
    sm->has_last_press = false;
}

// Feed one polled sample. `pressed` is true when the pin reads low
// (button to GND with pull-up). Returns the event for this sample.
static inline button_event_t button_sm_update(button_sm_t *sm,
                                              bool pressed,
                                              uint32_t now_ms,
                                              const button_sm_cfg_t *cfg)
{
    switch (sm->state) {
    case BTN_ST_IDLE:
        if (pressed) {
            sm->state = BTN_ST_DEBOUNCE;
            sm->pressed_since_ms = now_ms;
        }
        return BTN_EV_NONE;

    case BTN_ST_DEBOUNCE:
        if (!pressed) {
            sm->state = BTN_ST_IDLE;
            return BTN_EV_NONE;
        }
        if (now_ms - sm->pressed_since_ms >= cfg->debounce_ms) {
            sm->state = BTN_ST_PRESSED;
            // Min-gap: a press too soon after the previous one is
            // chatter — track the state normally, but fire nothing.
            if (!sm->has_last_press ||
                now_ms - sm->last_press_ms >= cfg->min_gap_ms) {
                sm->last_press_ms = now_ms;
                sm->has_last_press = true;
                return BTN_EV_PRESS;
            }
        }
        return BTN_EV_NONE;

    case BTN_ST_PRESSED:
        if (!pressed) {
            sm->state = BTN_ST_IDLE;
            return BTN_EV_NONE;
        }
        if (now_ms - sm->pressed_since_ms >= cfg->hold_ms) {
            sm->state = BTN_ST_HELD;
            return BTN_EV_HOLD;
        }
        return BTN_EV_NONE;

    case BTN_ST_HELD:
        if (!pressed) {
            sm->state = BTN_ST_IDLE;
            return BTN_EV_NONE;
        }
        if (now_ms - sm->pressed_since_ms >= cfg->stuck_ms) {
            sm->state = BTN_ST_STUCK;
        }
        return BTN_EV_NONE;

    case BTN_ST_STUCK:
    default:
        // No events at all until the button is released.
        if (!pressed) {
            sm->state = BTN_ST_IDLE;
        }
        return BTN_EV_NONE;
    }
}

#ifdef __cplusplus
}
#endif

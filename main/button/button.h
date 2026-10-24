#pragma once

#include <stdint.h>

#include "button_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Physical button module.
 * Wiring: button connects GPIO to GND, using internal pull-up.
 *
 * GPIO21 = Toggle
 * GPIO22 = Speed Up
 * GPIO23 = Speed Down
 *
 * Phase 1, S1.3: the module polls (20 ms) instead of firing on the
 * interrupt edge, so one task can track all three buttons independently
 * — a stuck button no longer blocks the others — and so press, hold
 * and stuck are distinguishable events:
 *   PRESS — validated press (debounced 50 ms).
 *   HOLD  — still held at 1.5 s (hold-to-start for the toggle button).
 * A button held past 5 s, or already pressed at boot, is STUCK and
 * fires nothing until released.
 */

typedef enum {
    BUTTON_ID_TOGGLE     = 0,   /* GPIO21 - Toggle treadmill */
    BUTTON_ID_SPEED_UP   = 1,   /* GPIO22 - Speed up */
    BUTTON_ID_SPEED_DOWN = 2,   /* GPIO23 - Speed down */
    BUTTON_ID_MAX
} button_id_t;

typedef void (*button_callback_t)(button_id_t id, button_event_t event);

void button_init(button_callback_t cb);

#ifdef __cplusplus
}
#endif

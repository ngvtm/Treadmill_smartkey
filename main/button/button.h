#pragma once

#include <stdint.h>

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
 */

typedef enum {
    BUTTON_ID_TOGGLE     = 0,   /* GPIO21 - Toggle treadmill */
    BUTTON_ID_SPEED_UP   = 1,   /* GPIO22 - Speed up */
    BUTTON_ID_SPEED_DOWN = 2,   /* GPIO23 - Speed down */
    BUTTON_ID_MAX
} button_id_t;

typedef void (*button_callback_t)(button_id_t id);

void button_init(button_callback_t cb);

#ifdef __cplusplus
}
#endif

#pragma once

// XPT2046 resistive touch driver (Phase 2) on the shared display SPI
// bus (SCK 32 / MOSI 33, SPI2_HOST). The touch chip gets its own SPI
// device at 2 MHz, mode 0 — never the display's 40 MHz config.
// Polling only (T_IRQ level): the menu task polls every 20 ms, which
// is plenty for finger taps and avoids interrupt-context SPI.
//
// Wiring (menuconfig "Smartkey Touch"): T_CLK = SCK, T_DIN = MOSI,
// T_DO = MISO GPIO, T_CS = its own GPIO, T_IRQ = touch-detect level
// (low = touched). Most 2.4" TFT modules expose these on the same
// ribbon as the display pins.

#include <stdint.h>
#include <stdbool.h>
#include "touch_calibration.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init the SPI device + IRQ pin. Must run after lcd_display_init
// (which creates the shared SPI bus). Returns false if the bus or
// the device cannot be added; touch then stays unavailable and the
// menu never opens (buttons keep working).
bool touch_xpt2046_init(void);

// True while the panel reports a touch (T_IRQ low).
bool touch_xpt2046_is_touched(void);

// One debounced raw sample: median of 5 reads per axis. Returns false
// when the panel is not touched or the SPI transfer fails.
bool touch_xpt2046_read_raw(touch_raw_t *out);

#ifdef __cplusplus
}
#endif

#pragma once

// Touch settings menu (Phase 2): the only screen that uses touch.
// Entry: hold the header area (top 35 px) for 2 s while the belt is
// stopped and the key is connected. Exit: the THOAT row, the belt
// starting, BLE disconnect, or 30 s without a touch. While open, the
// normal display task is paused and this module draws via lcd_gfx.
//
// Rows: IR zone protection toggle, reed safety key toggle, touch
// re-calibration, exit. Tapping a toggle sends SET_FEATURE; the row
// repaints from the server's features_off_mask when the status
// notify comes back, so a refused change visibly reverts. Toggles
// are painted only from that mask — never optimistically.
//
// First run (no calibration in NVS) opens the calibration flow
// instead of the menu: tap the three on-screen targets, then the
// menu opens with the fresh calibration.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Must run after lcd_display_init (shared SPI bus + display task).
// If touch init fails, the menu stays disabled and buttons keep
// working; the failure is logged, not fatal.
void touch_menu_init(void);

#ifdef __cplusplus
}
#endif

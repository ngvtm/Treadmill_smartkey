#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol command bytes shared by smartkey and treadmill. */
#define TREADMILL_CMD_OFF        0x00
#define TREADMILL_CMD_ON         0x01
#define TREADMILL_CMD_SPEED_UP   0x02
#define TREADMILL_CMD_SPEED_DOWN 0x03

#define TREADMILL_MSG_STATUS     0x80

typedef enum {
    TREADMILL_ALERT_NONE = 0,
    TREADMILL_ALERT_WARNING_1,
    TREADMILL_ALERT_WARNING_2,
    TREADMILL_ALERT_DANGER,
    TREADMILL_ALERT_NO_PERSON,
    TREADMILL_ALERT_CMD_REJECTED,
    TREADMILL_ALERT_SAFETY_KEY_OUT,  /* = 6: reed breakaway key pulled / not docked */
    TREADMILL_ALERT_MOTOR_FAULT,     /* = 7: stall / overcurrent / no-motion (GĐ2) */
} treadmill_alert_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t running;
    uint8_t speed;
    uint8_t requested_speed;
    uint8_t safety_limited;
    uint8_t zone;
    uint8_t alert;
} treadmill_status_msg_t;

void treadmill_ctrl_init(void);
/* Phase 1, S1.3: quick press = OFF only; ON requires a HOLD event.
 * (The old single toggle() is gone: its two halves had different gates
 * and different confirmation rules, and one function hid that.) */
void treadmill_ctrl_request_on(void);
void treadmill_ctrl_request_off(void);
/* True while control is ON or an ON is awaiting confirmation — used by
 * the button layer to decide whether a quick press means OFF. */
bool treadmill_ctrl_is_control_active(void);
/* Periodic timeout evaluation (ON-confirm window, status staleness).
 * Called every 100 ms by the ctrl tick task; tests call it directly
 * after advancing the fake clock. */
void treadmill_ctrl_tick(void);
void treadmill_ctrl_speed_up(void);
void treadmill_ctrl_speed_down(void);
void treadmill_ctrl_handle_status(const uint8_t *data, uint16_t len);
bool treadmill_ctrl_is_running(void);

#ifdef __cplusplus
}
#endif

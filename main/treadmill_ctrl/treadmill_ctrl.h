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
void treadmill_ctrl_toggle(void);
void treadmill_ctrl_speed_up(void);
void treadmill_ctrl_speed_down(void);
void treadmill_ctrl_handle_status(const uint8_t *data, uint16_t len);
bool treadmill_ctrl_is_running(void);

#ifdef __cplusplus
}
#endif

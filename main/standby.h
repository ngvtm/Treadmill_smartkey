#pragma once

// Power standby state, owned by main.c (the button layer).
// The touch task reads it to ignore input while the key sleeps;
// the GAP handler reads it to suppress auto-rescan while asleep.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool smartkey_standby_is_active(void);

#ifdef __cplusplus
}
#endif

#!/bin/sh
# Host-based unit tests for Treadmill_smartkey (no ESP-IDF needed).
# Used by CI (.github/workflows/host-tests.yml) and by hand.
# Exits non-zero on the first failing suite.
set -e
cd "$(dirname "$0")"

INC="-I. -Istubs -I../../main -I../../main/lcd -I../../main/treadmill_ctrl"

echo "=== test_ctrl ==="
gcc -std=gnu17 -Wall -Wextra -Wno-unused-parameter -g -O1 $INC \
    test_ctrl.c ../../main/treadmill_ctrl/treadmill_ctrl.c \
    -o /tmp/test_ctrl
/tmp/test_ctrl | tail -1

echo "=== test_display ==="
gcc -std=gnu17 -Wall -Wextra -Wno-unused-parameter -g -O1 $INC \
    test_display.c ../../main/lcd/lcd_gfx.c fake_panel.c \
    -o /tmp/test_display
/tmp/test_display | tail -1

echo "=== test_gfx ==="
gcc -std=gnu17 -Wall -Wextra -Wno-unused-parameter -g -O1 $INC \
    test_gfx.c ../../main/lcd/lcd_gfx.c fake_panel.c \
    -o /tmp/test_gfx
/tmp/test_gfx | tail -1

echo "=== test_button_sm ==="
gcc -std=gnu17 -Wall -Wextra -g -O1 -I../../main \
    test_button_sm.c -o /tmp/test_button_sm
/tmp/test_button_sm | tail -1

echo "=== test_touch_cal ==="
gcc -std=gnu17 -Wall -Wextra -g -O1 -I../../main \
    test_touch_cal.c -o /tmp/test_touch_cal
/tmp/test_touch_cal | tail -1

echo "ALL SMARTKEY HOST SUITES PASSED"

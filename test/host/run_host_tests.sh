#!/bin/sh
# Host-based unit tests for Treadmill_smartkey (no ESP-IDF needed).
# Used by CI (.github/workflows/host-tests.yml) and by hand.
# Exits non-zero on the first failing suite.
# LUU Y: khong pipe truc tiep qua `tail` (exit code cua pipeline la
# cua tail, suite loi van in ALL PASSED va CI xanh oan). Chay ra file
# log, giu lai exit code that, roi moi in dong cuoi.
set -e
cd "$(dirname "$0")"

INC="-I. -Istubs -I../../main -I../../main/lcd -I../../main/treadmill_ctrl"

echo "=== test_ctrl ==="
gcc -std=gnu17 -Wall -Wextra -Wno-unused-parameter -g -O1 $INC \
    test_ctrl.c ../../main/treadmill_ctrl/treadmill_ctrl.c \
    -o /tmp/test_ctrl
rc=0; /tmp/test_ctrl > /tmp/test_ctrl.log || rc=$?; tail -1 /tmp/test_ctrl.log; [ "$rc" -eq 0 ] || exit "$rc"

echo "=== test_display ==="
gcc -std=gnu17 -Wall -Wextra -Wno-unused-parameter -g -O1 $INC \
    test_display.c ../../main/lcd/lcd_gfx.c fake_panel.c \
    -o /tmp/test_display
rc=0; /tmp/test_display > /tmp/test_display.log || rc=$?; tail -1 /tmp/test_display.log; [ "$rc" -eq 0 ] || exit "$rc"

echo "=== test_gfx ==="
gcc -std=gnu17 -Wall -Wextra -Wno-unused-parameter -g -O1 $INC \
    test_gfx.c ../../main/lcd/lcd_gfx.c fake_panel.c \
    -o /tmp/test_gfx
rc=0; /tmp/test_gfx > /tmp/test_gfx.log || rc=$?; tail -1 /tmp/test_gfx.log; [ "$rc" -eq 0 ] || exit "$rc"

echo "=== test_button_sm ==="
gcc -std=gnu17 -Wall -Wextra -g -O1 -I../../main \
    test_button_sm.c -o /tmp/test_button_sm
rc=0; /tmp/test_button_sm > /tmp/test_button_sm.log || rc=$?; tail -1 /tmp/test_button_sm.log; [ "$rc" -eq 0 ] || exit "$rc"

echo "=== test_touch_cal ==="
gcc -std=gnu17 -Wall -Wextra -g -O1 -I../../main \
    test_touch_cal.c -o /tmp/test_touch_cal
rc=0; /tmp/test_touch_cal > /tmp/test_touch_cal.log || rc=$?; tail -1 /tmp/test_touch_cal.log; [ "$rc" -eq 0 ] || exit "$rc"

echo "=== test_touch_controls ==="
gcc -std=gnu17 -Wall -Wextra -g -O1 -I../../main \
    test_touch_controls.c -o /tmp/test_touch_controls
rc=0; /tmp/test_touch_controls > /tmp/test_touch_controls.log || rc=$?; tail -1 /tmp/test_touch_controls.log; [ "$rc" -eq 0 ] || exit "$rc"

echo "ALL SMARTKEY HOST SUITES PASSED"

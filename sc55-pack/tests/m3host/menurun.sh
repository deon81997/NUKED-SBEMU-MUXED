#!/bin/sh
# MENU (sc55menu.c + menu3d.h + tui.h) under AddressSanitizer + UBSan on Linux. sh tests/m3host/menurun.sh [quick]
set -e
d=$(dirname "$0")
gcc -m32 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
    -DMENU_VER='"1.0.0-beta.0-test"' -I"$d/inc" -I"$d/../../tools" -w -o /tmp/menuasan "$d/menuasan.c" -lm
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 /tmp/menuasan "$@"

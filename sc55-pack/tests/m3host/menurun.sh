#!/bin/sh
# MENU (sc55menu.c + menu3d.h + tui.h) under AddressSanitizer + UBSan on Linux. Needs gcc-multilib.
#   sh tests/m3host/menurun.sh [quick|fit|ed|shots|seed N]   whole MENU (menuasan.c: text + 3D, entries, editor, random)
#   then, unless only fit/ed/shots/seed were asked: the 3D renderer in every mode/style/look/theme, overlays,
#   keep-mode, refused modes, Windows path, clicks, OOM (m3menu.c, same as run.sh menu [quick])
set -e
d=$(dirname "$0")
gcc -m32 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
    -DMENU_VER='"1.0.0-beta.0-test"' -I"$d/inc" -I"$d/../../tools" -w -o /tmp/menuasan "$d/menuasan.c" -lm
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 /tmp/menuasan "$@"
case "$1" in fit|ed|shots|seed) exit 0 ;; esac
echo "6: 3D renderer (m3menu.c)"
sh "$d/run.sh" menu ${1:+"$1"}

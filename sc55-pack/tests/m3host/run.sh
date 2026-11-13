#!/bin/sh
# The 3D views under AddressSanitizer + UBSan on Linux (32 bit, like DOS). Needs gcc-multilib.
#   sh tests/m3host/run.sh [quick|crash]        MIDPLAY (m3asan.c)
#   sh tests/m3host/run.sh menu [quick|crash]   SC55MENU's 3D menu (m3menu.c)
set -e
d=$(dirname "$0")
t=m3asan
if [ "$1" = menu ]; then t=m3menu; shift; fi
gcc -m32 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
    -I"$d/inc" -I"$d/../../tools" -w -o /tmp/$t "$d/$t.c" -lm
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 /tmp/$t "$@"

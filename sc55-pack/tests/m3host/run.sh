#!/bin/sh
# MIDPLAY 3D under AddressSanitizer + UBSan on Linux (32 bit, like DOS). sh tests/m3host/run.sh [quick]
set -e
d=$(dirname "$0")
gcc -m32 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all \
    -I"$d/inc" -I"$d/../../tools" -w -o /tmp/m3asan "$d/m3asan.c" -lm
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 /tmp/m3asan "$@"

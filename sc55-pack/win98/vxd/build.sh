#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# build SC55W.VXD.  DEV=1 ./build.sh -> unique device name per build (SWnnnnn)
set -e
cd "$(dirname "$0")"
export WATCOM=/home/claude/ow PATH=/home/claude/ow/binl64:$PATH INCLUDE=/home/claude/ow/h
J=/home/claude/jwasm-src/build/GccUnixR/jwasm
$J -q -c -W2 -Fo sc55w.o sc55w.asm
BID=$(( $(cat .devbuild 2>/dev/null || echo 0) + 1 ))
wcc386 -q -zl -s -ox -mf -bt=windows -ecc -wx -dBUILD_ID=$BID -fo=sbcore.obj sbcore.c
wlink @sc55w.lnk
if [ -n "$DEV" ]; then
    n=$(( $(cat .devbuild 2>/dev/null || echo 0) + 1 )); echo $n > .devbuild
    python3 ../tools/lefix.py SC55W.VXD "$(printf 'SW%05d' $n)"
    printf 'SW%05d' $n > .devname
else
    python3 ../tools/lefix.py SC55W.VXD
fi

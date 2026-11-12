#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# build NUKEDJOY.VXD (VJoyD minidriver) with JWasm + Open Watcom, like vxd/build.sh
set -e
cd "$(dirname "$0")"
export WATCOM=/home/claude/ow PATH=/home/claude/ow/binl64:$PATH INCLUDE=/home/claude/ow/h
J=/home/claude/jwasm-src/build/GccUnixR/jwasm
$J -q -c -W2 -Fo nukedjoy.o nukedjoy.asm
wcc386 -q -zl -s -ox -mf -bt=windows -ecc -wx -dNJ_BUILD=$(date +%s) ${NJ_IFACE:+-dNJ_IFACE=$NJ_IFACE} -fo=nukedjoy.obj nukedjoy.c
wlink @nukedjoy.lnk
python3 ../tools/lefix.py NUKEDJOY.VXD

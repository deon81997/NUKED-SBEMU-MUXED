#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# joyseq.sh : press A, B, stick left, Y, X on the game port joystick (JOYSTATE file)
J=${JOYSTATE:-/home/claude/86vm/w98/joy.st}
s() { echo "$1" > $J; sleep ${2:-0.8}; }
s "0 0 1 0 0 0"; s "0 0 0 0 0 0"
s "0 0 0 1 0 0"; s "0 0 0 0 0 0"
s "-32768 0 0 0 0 0"; s "0 0 0 0 0 0"
s "0 0 0 0 0 1"; s "0 0 0 0 0 0"
s "0 0 0 0 1 0"; s "0 0 0 0 0 0"

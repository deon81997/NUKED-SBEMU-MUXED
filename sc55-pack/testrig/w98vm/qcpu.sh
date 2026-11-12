#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# QEMU host CPU seconds used over $1 seconds (guest idle = low)
P=$(pgrep -f qemu-system-i386 | head -1)
a=$(awk '{print $14+$15}' /proc/$P/stat); sleep ${1:-30}; b=$(awk '{print $14+$15}' /proc/$P/stat)
echo "qemu cpu: $(( (b-a) )) ticks in ${1:-30}s ($(( (b-a)*100/(100*${1:-30}) ))% of a host core)"

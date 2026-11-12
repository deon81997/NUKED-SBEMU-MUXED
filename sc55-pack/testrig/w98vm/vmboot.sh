#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# boot the Win98 test VM (IMG, D), wait for the desktop, plug the fake pad
cd /home/claude/w98vm
pkill -f qemu-system-i386; sleep 2; rm -f /tmp/w98mon.sock
(IMG=${IMG:-t1.img} D=${D:-d.img} ./run.sh &)
sleep ${WAIT:-140}
[ -n "$NOPAD" ] || /tmp/padplug.sh

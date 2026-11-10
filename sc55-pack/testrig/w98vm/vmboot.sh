#!/bin/sh
# boot the Win98 test VM (IMG, D), wait for the desktop, plug the fake pad
cd /home/claude/w98vm
pkill -f qemu-system-i386; sleep 2; rm -f /tmp/w98mon.sock
(IMG=${IMG:-t1.img} D=${D:-d.img} ./run.sh &)
sleep ${WAIT:-140}
[ -n "$NOPAD" ] || /tmp/padplug.sh

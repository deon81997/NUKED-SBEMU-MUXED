#!/bin/sh
# shut Windows down cleanly (Start > Shut Down > OK) and wait for QEMU to exit
timeout 30 python3 /tmp/qk.py /tmp/w98mon.sock k:ctrl-esc s:1 k:u s:2 k:alt-s s:1 k:ret
for i in $(seq 1 60); do pgrep -f qemu-system-i386 >/dev/null || exit 0; sleep 2; done
echo "VM did not power off"; timeout 10 python3 /tmp/qk.py /tmp/w98mon.sock shot:/tmp/off.png; exit 1

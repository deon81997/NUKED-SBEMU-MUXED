#!/bin/sh
# Win98 SE test VM (QuickInstall): QEMU pc, P3+SSE2, 128 MB, SB16, PIIX3 UHCI. EXTRA = more args
cd /home/claude/w98vm
exec timeout ${T:-7000} /home/claude/src/qemu/build/qemu-system-i386 -M pc -cpu pentium3,+sse2 -m 128 \
  -drive file=${IMG:-qi.img},format=raw,if=ide,index=0 ${D:+-drive file=$D,format=raw,if=ide,index=1} \
  -rtc base=localtime -display none -monitor unix:/tmp/w98mon.sock,server,nowait \
  -device sb16 -usb ${CD:+-drive file=$CD,media=cdrom,if=ide,index=2,readonly=on} $EXTRA > /tmp/w98q.log 2>&1

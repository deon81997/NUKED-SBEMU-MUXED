#!/bin/bash
# run.sh: boot c.img with OHCI + usb-kbd + usb-mouse, HMP monitor on mon.sock (background)
rm -f mon.sock
exec qemu-system-i386 -machine pc -cpu ${CPU:-pentium2} -m 64 \
  -drive file=c.img,format=raw,if=ide,index=0 -boot c -audiodev none,id=snd -device AC97,audiodev=snd \
  -display none -vga std ${USBDEV:--device pci-ohci,id=ohci -device usb-kbd,bus=ohci.0 -device usb-mouse,bus=ohci.0} \
  -monitor unix:mon.sock,server,nowait -serial file:serial.log ${QTRACE} > qemu.log 2>&1

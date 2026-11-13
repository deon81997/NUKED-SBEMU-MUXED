#!/bin/bash
# runq.sh VMDIR IMG [MEM_MB] : QEMU (pc, pentium3, std-vga, AC97, PS/2 mouse) headless, HMP monitor at VMDIR/m.sock.
# The image is copied to VMDIR/hd.img (the original stays clean). Then: python3 soak.py q VMDIR/m.sock SECS OUTDIR
V=$1; mkdir -p $V; cp --sparse=always $2 $V/hd.img; pkill -f "qemu-system-i386.*$V/hd.img"; sleep 0.5; rm -f $V/m.sock
nohup qemu-system-i386 -M pc -cpu pentium3 -m ${3:-64} -display none -vga std \
  -drive file=$V/hd.img,format=raw,if=ide -device AC97,audiodev=a0 -audiodev none,id=a0 \
  -monitor unix:$V/m.sock,server,nowait -rtc base=localtime > $V/qemu.log 2>&1 &
for i in $(seq 50); do [ -S $V/m.sock ] && break; sleep 0.2; done; ls $V/m.sock

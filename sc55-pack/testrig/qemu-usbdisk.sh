#!/bin/bash
# qemu-usbdisk.sh NAME [extra qemu args] (USB disks, session 7). QEMU 8.2.2 built from github qemu/qemu: configure
# --target-list=i386-softmmu --disable-docs --disable-werror --disable-tools --disable-gtk --disable-sdl --disable-vnc
# --disable-slirp --disable-capstone; gitlab is blocked, so subprojects dtc / keycodemapdb / berkeley-*float-3 come
# from their github mirrors (+ packagefiles copied in), and distlib is a 3-file shim over pip's vendored copy
# (PYTHONPATH). /home/claude/vm/c = the boot disk folder as in 86box/msdtest.sh.
# : QEMU PC with ICH9 EHCI + 3 UHCI companions, USB stick (+ keyboard) on the EHCI; USBTEST log -> NAME.txt
cd /home/claude/vm; R=/home/claude/nuked-sbemu-muxed/sc55-pack; Q=/home/claude/qemu
python3 $R/testrig/mkdisk.py build c q.img 100 >/dev/null
N=$1; shift
timeout ${T:-70} $Q/b3/qemu-system-i386 -L $Q/src/pc-bios -m 64 -display none -no-reboot \
  -drive file=q.img,format=raw,if=ide,index=0 \
  -device ich9-usb-ehci1,id=ehci,addr=1d.7,multifunction=on \
  -device ich9-usb-uhci1,masterbus=ehci.0,firstport=0,addr=1d.0,multifunction=on \
  -device ich9-usb-uhci2,masterbus=ehci.0,firstport=2,addr=1d.1,multifunction=on \
  -device ich9-usb-uhci3,masterbus=ehci.0,firstport=4,addr=1d.2,multifunction=on \
  -drive if=none,id=stick,file=qmsd.img,format=raw -device usb-storage,bus=ehci.0,port=1,drive=stick,id=st \
  -device usb-kbd,bus=ehci.0,port=2,usb_version=1 -device usb-mouse,bus=ehci.0,port=3,usb_version=1,id=ms -monitor unix:/home/claude/vm/qmon.sock,server,nowait "$@" > qemu.log 2>&1
python3 $R/testrig/mkdisk.py get q.img UT.TXT $N.txt && grep -v '^$' $N.txt | grep -v '^ *[│┌└]' | head -${L:-40}

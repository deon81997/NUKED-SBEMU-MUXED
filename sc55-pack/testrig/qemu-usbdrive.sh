#!/bin/bash
# qemu-usbdrive.sh NAME "DOS cmd" ... (USB disks session 4: DOS drive letters). QEMU 8.2.2 (Ubuntu's qemu-system-x86
# works too). /home/claude/vm/c = boot folder: FreeDOS KERNEL.SYS + COMMAND.COM (testrig/rufus/res/freedos),
# JEMMEX.EXE + HDPMI32I.EXE (release zip), SBEMU.EXE, DELAY.COM (10 s wait, INT 1Ah), CONFIG.SYS
# "DEVICE=C:\JEMMEX.EXE NOEMS / DOS=HIGH,UMB / LASTDRIVE=Z" (SBEMU needs XMS).
# C=c71 MKDISK_BOOT=msdos: MS-DOS 7.1 folder instead (IO.SYS, MSDOS.SYS, COMMAND.COM from the user's CD, BOOT/DOS71INS.IMG).
# Sticks are hot-plugged at P s (default 6) while DELAY runs: plugged at boot, SeaBIOS maps them as INT 13h disks and
# FreeDOS gives them letters itself (writable, not ours). D="id:port ..." (default "s1:1 s2:4"), images vm/sa.img
# (MBR FAT16 + logical FAT32), sg.img (GPT FAT16), sf.img (superfloppy FAT12); B=uh.0 QX="-device piix3-usb-uhci,id=uh,addr=5"
# for UHCI. EV="28:device_del+us1 ..." runs monitor commands at given seconds (+ = space). Each DOS cmd's output goes to C:\UT.TXT -> NAME-UT.TXT; screen at the end -> NAME.ppm.
N=$1; shift; cd /home/claude/vm; R=/home/claude/nuked-sbemu-muxed/sc55-pack
M() { echo "$1" | socat - UNIX-CONNECT:/home/claude/vm/qmon.sock | tr -d "\033" | tail -c 300 >> /home/claude/vm/$N-mon.log; }
{ printf '@ECHO OFF\r\nHDPMI32I -r -x\r\nDELAY\r\nSBEMU /JOY:1 /USBDISK:1 > C:\\SB.TXT\r\nVER > C:\\UT.TXT\r\n'
  for c in "$@"; do printf '%s >> C:\\UT.TXT\r\n' "$c"; done; printf 'ECHO DONE >> C:\\UT.TXT\r\n'; } > ${C:-c}/AUTOEXEC.BAT
python3 $R/testrig/mkdisk.py build ${C:-c} q.img 100 >/dev/null
timeout ${T:-60} qemu-system-i386 -m 64 -display none -no-reboot -audiodev none,id=a -device AC97,audiodev=a \
  -drive file=q.img,format=raw,if=ide,index=0 -device ich9-usb-ehci1,id=ehci,addr=1d.7,multifunction=on \
  -device ich9-usb-uhci1,masterbus=ehci.0,firstport=0,addr=1d.0,multifunction=on \
  -device ich9-usb-uhci2,masterbus=ehci.0,firstport=2,addr=1d.1,multifunction=on \
  -device ich9-usb-uhci3,masterbus=ehci.0,firstport=4,addr=1d.2,multifunction=on \
  -drive if=none,id=s1,file=sa.img,format=raw -drive if=none,id=s2,file=sg.img,format=raw -drive if=none,id=s3,file=sf.img,format=raw \
  -monitor unix:/home/claude/vm/qmon.sock,server,nowait $QX > qemu.log 2>&1 &
for e in $EV; do (sleep ${e%%:*}; M "$(echo ${e#*:} | tr + " ")") & done   # EV="28:device_del+us1": monitor command (+ = space) at 28 s
sleep ${P:-6}; for d in ${D:-s1:1 s2:4}; do M "device_add usb-storage,bus=${B:-ehci.0},port=${d#*:},drive=${d%:*},id=u${d%:*}"; done
sleep $(( ${T:-60} - ${P:-6} - 4 )); M "screendump /home/claude/vm/$N.ppm"; wait
python3 $R/testrig/mkdisk.py get q.img UT.TXT $N-UT.TXT >/dev/null && tr -d '\r' < $N-UT.TXT | grep -v '^$'

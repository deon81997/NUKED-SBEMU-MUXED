#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# drvtest.sh NAME [ohci] "DOS cmd" ... : USB disk drive letters (session 4) in the devkit 86Box (P2B-LS, ES1371, usb_msd).
# /home/claude/vm86: c/ (boot folder as for testrig/qemu-usbdrive.sh; C= another one, MKDISK_BOOT=msdos for MS-DOS),
# vm/86box.cfg = p2bls-uhci.cfg + "usb_msd = usb_msd" in [Ports] + hdd_02 msd.img (63, 16, 260, 0, scsi; location 0:00).
# ohci: two fake pads take the UHCI ports, the disk is plugged at 25 s (AUTOEXEC waits with DELAY) -> OPTi OHCI.
# Output of each DOS cmd -> NAME-UT.TXT; text screen at the end -> NAME-scr.txt.
N=$1; shift; O=; [ "$1" = ohci ] && { O=1; shift; }
cd /home/claude/vm86; R=/home/claude/nuked-sbemu-muxed/sc55-pack; A=$R/testrig/86box/a.py; C=${C:-c}
{ printf '@ECHO OFF\r\nHDPMI32I -r -x\r\n'; [ $O ] && printf 'DELAY\r\nDELAY\r\nDELAY\r\n'
  printf 'SBEMU /JOY:1 /USBDISK:1 > C:\\SB.TXT\r\nVER > C:\\UT.TXT\r\n'
  for c in "$@"; do printf '%s >> C:\\UT.TXT\r\n' "$c"; done; printf 'ECHO DONE >> C:\\UT.TXT\r\n'; } > $C/AUTOEXEC.BAT
python3 $R/testrig/mkdisk.py build $C vm/hd.img 100 >/dev/null; rm -f vm/a.sock
grep -v usb_msd_connected vm/86box.cfg > t && mv t vm/86box.cfg; unset XPAD
if [ $O ]; then sed -i 's/^usb_msd = usb_msd/usb_msd = usb_msd\nusb_msd_connected = 0/' vm/86box.cfg; export XPAD=2 XPAD_STATE=$PWD/x1.st XPAD_STATE2=$PWD/x2.st; echo buttons=0 > x1.st; echo buttons=0 > x2.st; fi
(timeout ${T:-90} /home/claude/devkit/sc55-devkit/86box/run86box.sh $PWD/vm > run.log 2>&1 &)
[ $O ] && { sleep 25; python3 $A vm/a.sock "usb plug usb_msd" >/dev/null; }
sleep $(( ${T:-90} - ${O:+25} - 5 )); python3 $A vm/a.sock @screen > $N-scr.txt 2>&1; sleep 5
python3 $R/testrig/mkdisk.py get vm/hd.img UT.TXT $N-UT.TXT >/dev/null && tr -d '\r' < $N-UT.TXT | grep -v '^$'

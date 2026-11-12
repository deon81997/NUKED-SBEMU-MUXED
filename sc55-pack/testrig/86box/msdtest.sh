#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# msdtest.sh NAME [ohci] : USB disk test in the devkit 86Box (usb_msd). Needs /home/claude/vm with c/ (FreeDOS
# KERNEL.SYS + COMMAND.COM from testrig/rufus/res/freedos, JEMMEX.EXE + CONFIG.SYS, HDPMI32I.EXE, USBTEST.EXE, AUTOEXEC.BAT
# "HDPMI32I -r -x" / "USBTEST C:\UT.TXT /T:15") and vm/86box.cfg = p2bls-uhci.cfg + "usb_msd = usb_msd" in [Ports] and
# hdd_02 (msd.img, scsi, location 0:00). ohci: two pads take the UHCI ports, the disk is plugged later -> OPTi OHCI.
# USBTEST log to NAME.txt; compare its CRC-32 with the first MB of msd.img.
cd /home/claude/vm; R=/home/claude/nuked-sbemu-muxed/sc55-pack; A=$R/testrig/86box/a.py
python3 $R/testrig/mkdisk.py build c vm/hd.img 100 >/dev/null; rm -f vm/a.sock
grep -v usb_msd_connected vm/86box.cfg > t && mv t vm/86box.cfg
if [ "$2" = ohci ]; then sed -i 's/^usb_msd = usb_msd/usb_msd = usb_msd\nusb_msd_connected = 0/' vm/86box.cfg; export XPAD=2 XPAD_STATE=$PWD/x1.st XPAD_STATE2=$PWD/x2.st; echo buttons=0 > x1.st; echo buttons=0 > x2.st; fi
(timeout ${T:-80} /home/claude/devkit/sc55-devkit/86box/run86box.sh $PWD/vm > run.log 2>&1 &)
if [ "$2" = ohci ]; then sleep 30; python3 $A vm/a.sock "usb plug usb_msd"; fi
sleep $(( ${T:-80} - ${2:+30} - 3 )); python3 $R/testrig/mkdisk.py get vm/hd.img UT.TXT $1.txt; grep -v '^$' $1.txt | grep -v '^ *[│┌└]' | head -${N:-40}

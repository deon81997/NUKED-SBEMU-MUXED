#!/bin/bash
# mk.sh SBEMU.EXE -> c.img
set -e
R=/home/claude/rel/NUKEDSB/FILES; W=${T3D:-/home/claude/t3d}/c; rm -rf $W; mkdir -p $W/SC55 $W/MID $W/MOUSE
cp /home/claude/fd/KERNEL.SYS /home/claude/fd/COMMAND.COM $W/
cp $R/JEMM/JEMMEX.EXE $R/JEMM/JLOAD.EXE $R/JEMM/QPIEMU.DLL $R/HDPMI/HDPMI32I.EXE $R/SC55/*.EXE $W/SC55/
cp $R/MOUSE/CTMOUSE.EXE $W/MOUSE/
cp "${1:-$R/SC55/SBEMU.EXE}" $W/SC55/SBEMU.EXE
[ -n "$2" ] && cp $2 $W/SC55/MIDPLAY.EXE
[ -n "$3" ] && cp $3 $W/SC55/SC55MENU.EXE
python3 /home/claude/nuked-sbemu-muxed/sc55-pack/testrig/midplay/genmid.py $W/MID/T.MID 120 > /dev/null
printf 'DEVICE=C:\\SC55\\JEMMEX.EXE NOEMS\r\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n' > $W/CONFIG.SYS
A="@ECHO OFF\r\nC:\\SC55\\JLOAD C:\\SC55\\QPIEMU.DLL\r\nC:\\SC55\\HDPMI32I -r\r\nCD \\SC55\r\nSBEMU ${SBOPT:-/JOY:1 /USBKB:1 /USBMS:2}\r\nC:\\MOUSE\\CTMOUSE\r\n"; [ -z "$NOLOOP" ] && A="$A:L\r\nMIDPLAY /V:2 /L C:\\MID\\T.MID\r\nECHO M >> C:\\ALIVE.TXT\r\nSC55MENU\r\nECHO S >> C:\\ALIVE.TXT\r\nGOTO L\r\n"; printf "$A" > $W/AUTOEXEC.BAT
python3 /home/claude/nuked-sbemu-muxed/sc55-pack/testrig/mkdisk.py build $W ${T3D:-/home/claude/t3d}/c.img 120 > /dev/null

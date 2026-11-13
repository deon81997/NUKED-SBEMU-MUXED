#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# MENU matrix part 2: run.sh DOS MODE HC PLUG [WORK]
#   DOS  fd = FreeDOS (testrig/rufus/res/freedos), 71 = MS-DOS 7.1 (IO.SYS/MSDOS.SYS/COMMAND.COM/HIMEM.SYS from the
#        Windows 98 QuickInstall v1.1.0 FULL.866, testrig/padkeys/mercy.py; folder $Q71, default /tmp/dl/w98qi/x)
#   MODE j32 = JEMMEX + HDPMI32I + SBEMU; x32 = HIMEM.SYS only (no EMM; FreeDOS: HimemX, $HX: github.com/
#        Baron-von-Riedesel/HimemX built with JWasm, `jwasm -mz HimemX.asm`) + HDPMI32I + SBEMU;
#        j16 = JEMMEX + QPIEMU + HDPMI16I + HDPMI32I + SBEMU + SBEMU16 (SC55J16.BAT order)
#   HC   uhci (PIIX3), ohci (pci-ohci), ehci (ICH9 EHCI, high speed), ehcic (ICH9 EHCI + UHCI companions, sticks
#        behind a full-speed hub -> companion), xhci (qemu-xhci, USB 2 ports)
#   PLUG before = sticks hot-plugged before SBEMU starts, after = after SBEMU is resident (spare letters)
# Sticks from mkstick.sh in $ST (default /home/claude/vm/st): s1 FAT16+FAT32, s2 exFAT+NTFS; s1 is then swapped for
# s3 (FAT16) on the same port. Per ISO (dos/ONE.BAT): UCDD mount from the stick, install (COPY to C:), play
# (MPUTEST through SBEMU's MPU-401, TimGM6mb SoundFont /VSF), unmount. DOS signals the host through COM1.
# Prints the per-case result lines; WORK/UT.TXT, SB.TXT, screen.ppm kept. Needs qemu-system-i386, socat, nasm.
set -u; D=$1 MODE=$2 HC=$3 PLUG=$4; W=${5:-/home/claude/vm/mm-$D-$MODE-$HC-$PLUG}
T=$(cd "$(dirname "$0")" && pwd); R=$(cd $T/../.. && pwd); ST=${ST:-/home/claude/vm/st}; Q71=${Q71:-/tmp/dl/w98qi/x}
SF=${SF:-/tmp/dl/timgm6mb/x/usr/share/sounds/sf2/TimGM6mb.sf2}; REL=${REL:-/tmp/rel/NUKEDSB/FILES}
rm -rf $W; mkdir -p $W/c/T $W/c/SC55/SF2 $W/c/CDSET $W/c/HDPMI $W/c/JEMM; C=$W/c
cp $REL/CDSET/* $C/CDSET/; cp $REL/HDPMI/*.EXE $C/HDPMI/; cp $REL/JEMM/* $C/JEMM/; cp $SF $C/SC55/SF2/T.SF2
for f in SBEMU USBDRV MPUTEST SBDIAG; do cp $R/output/$(echo $f | tr A-Z a-z).exe $C/SC55/$f.EXE; done
cp $REL/SC55/SBEMU16.COM $C/SC55/; for f in UCDD.EXE SHSUCDX.COM; do [ -f $R/output/cdset/$f ] && cp $R/output/cdset/$f $C/CDSET/; done; [ -f $R/output/sbemu16.com ] && cp $R/output/sbemu16.com $C/SC55/SBEMU16.COM
cp $T/dos/*.BAT $C/T/; nasm -f bin -o $C/DELAY.COM $R/testrig/delay.asm; nasm -f bin -o $C/T/HASISO.COM $T/dos/HASISO.ASM
if [ $D = 71 ]; then cp $Q71/IO.SYS $Q71/COMMAND.COM $C/; cp $Q71/WINDOWS/HIMEM.SYS $C/
  tr -d '\r' < $Q71/MSDOS.SYS | sed 's/^BootGUI=1/BootGUI=0/;s/^Logo=1/Logo=0/;s/^AutoScan=1/AutoScan=0/' | sed 's/$/\r/' > $C/MSDOS.SYS
  export MKDISK_BOOT=msdos
else cp $R/testrig/rufus/res/freedos/KERNEL.SYS $R/testrig/rufus/res/freedos/COMMAND.COM $C/; cp ${HX:-/tmp/dl/HIMEMX.EXE} $C/HIMEM.SYS; unset MKDISK_BOOT; fi   # MS HIMEM hangs the FreeDOS kernel: HimemX
{ if [ $MODE = x32 ]; then printf 'DEVICE=C:\\HIMEM.SYS\r\nDOS=HIGH\r\n'; else printf 'DEVICE=C:\\JEMM\\JEMMEX.EXE NOEMS\r\nDOS=HIGH,UMB\r\n'; fi
  printf 'FILES=40\r\nLASTDRIVE=Z\r\n'; } > $C/CONFIG.SYS
SB="C:\SC55\SBEMU.EXE /JOY:1 /USBDISK:1 /VSF:C:\SC55\SF2\T.SF2 ${SBX:-}"
{ printf '@ECHO OFF\r\nSET PATH=C:\\;C:\\SC55\r\nSET SBEMULOG=C:\\SB.TXT\r\nVER > C:\\UT.TXT\r\nMEM >> C:\\UT.TXT\r\n'
  [ $MODE = j16 ] && printf 'C:\\JEMM\\JLOAD.EXE C:\\JEMM\\QPIEMU.DLL >> C:\\UT.TXT\r\nC:\\HDPMI\\HDPMI16I.EXE -r >> C:\\UT.TXT\r\n'
  printf 'C:\\HDPMI\\HDPMI32I.EXE -r -x >> C:\\UT.TXT\r\nECHO PLUG1> COM1\r\nDELAY\r\n%s > C:\\SB.TXT\r\n' "$SB"
  [ $MODE = j16 ] && printf 'C:\\SC55\\SBEMU16.COM >> C:\\UT.TXT\r\n'
  printf 'ECHO PLUG2> COM1\r\nDELAY\r\nC:\\SC55\\SBDIAG.EXE START > NUL\r\nC:\\SC55\\MPUTEST.EXE /P:330 > NUL\r\nCALL C:\\T\\ALL.BAT A\r\nECHO SWAP> COM1\r\nDELAY\r\nCALL C:\\T\\ALL.BAT B\r\nC:\\SC55\\SBDIAG.EXE > NUL\r\nECHO DONE>> C:\\UT.TXT\r\nECHO DONE> COM1\r\n'
} > $C/AUTOEXEC.BAT
python3 $R/testrig/mkdisk.py build $C $W/q.img 100 >/dev/null || { echo "MKDISK FAILED"; exit 1; }
case $HC in
  uhci) QX="-device piix3-usb-uhci,id=hc,addr=5"; BUS=hc.0;;
  ohci) QX="-device pci-ohci,id=hc,addr=5"; BUS=hc.0;;
  xhci) QX="-device qemu-xhci,id=hc,addr=5,p2=8,p3=0"; BUS=hc.0;;
  ehci|ehcic) QX="-device ich9-usb-ehci1,id=ehci,addr=1d.7,multifunction=on -device ich9-usb-uhci1,masterbus=ehci.0,firstport=0,addr=1d.0,multifunction=on -device ich9-usb-uhci2,masterbus=ehci.0,firstport=2,addr=1d.1,multifunction=on -device ich9-usb-uhci3,masterbus=ehci.0,firstport=4,addr=1d.2,multifunction=on"; BUS=ehci.0;;
esac
cp $ST/s1.img $ST/s2.img $ST/s3.img $W/
MON=$W/mon.sock; M() { echo "$1" | socat - UNIX-CONNECT:$MON >/dev/null 2>&1; }
timeout ${TMO:-420} qemu-system-i386 -m 64 -display none -no-reboot -audiodev wav,id=a,path=$W/out.wav${AUDX-,timer-period=2000} -device AC97,audiodev=a \
  -drive file=$W/q.img,format=raw,if=ide,index=0 $QX -serial file:$W/com1.txt \
  -drive if=none,id=s1,file=$W/s1.img,format=raw,readonly=on -drive if=none,id=s2,file=$W/s2.img,format=raw,readonly=on \
  -drive if=none,id=s3,file=$W/s3.img,format=raw,readonly=on -monitor unix:$MON,server,nowait > $W/qemu.log 2>&1 & QP=$!
plug() { [ -n "${NOPLUG:-}" ] && return; if [ $HC = ehcic ]; then M "device_add usb-hub,bus=$BUS,port=1,id=hub"; sleep 0.3
           M "device_add usb-storage,bus=$BUS,port=1.1,drive=s1,id=us1"; M "device_add usb-storage,bus=$BUS,port=1.2,drive=s2,id=us2"
         else M "device_add usb-storage,bus=$BUS,port=1,drive=s1,id=us1"; M "device_add usb-storage,bus=$BUS,port=2,drive=s2,id=us2"; fi; }
want=PLUG1; [ $PLUG = after ] && want=PLUG2; st=0
while kill -0 $QP 2>/dev/null; do sleep 0.5; s=$(tr -d '\r' < $W/com1.txt 2>/dev/null)
  if [ $st = 0 ] && echo "$s" | grep -q "^$want"; then sleep 2; plug; st=1; fi
  if [ $st = 1 ] && echo "$s" | grep -q '^SWAP'; then sleep 1; M "device_del us1"; sleep 2
    P=1; [ $HC = ehcic ] && P=1.1; M "device_add usb-storage,bus=$BUS,port=$P,drive=s3,id=us3"; st=2; fi
  if echo "$s" | grep -q '^DONE'; then sleep 1; M "screendump $W/screen.ppm"; sleep 1; M quit; break; fi
done
[ $st = 2 ] || M "screendump $W/screen.ppm"; wait $QP 2>/dev/null
python3 $R/testrig/mkdisk.py get $W/q.img UT.TXT $W/UT.TXT >/dev/null 2>&1; python3 $R/testrig/mkdisk.py get $W/q.img SB.TXT $W/SB.TXT >/dev/null 2>&1; python3 $R/testrig/mkdisk.py get $W/q.img SBDETAIL.TXT $W/SBDETAIL.TXT >/dev/null 2>&1; python3 $R/testrig/mkdisk.py get $W/q.img SC55\\SBDIAG.TXT $W/SBDIAG.TXT >/dev/null 2>&1; python3 $R/testrig/mkdisk.py get $W/q.img SBDIAG.TXT $W/SBDIAG.TXT >/dev/null 2>&1
grep -q '^DONE' <(tr -d '\r' < $W/com1.txt 2>/dev/null) || echo "TIMEOUT (stage $st)"
# byte check of each install against the stick's DATA.BIN
for d in $(tr -d '\r' < $W/UT.TXT 2>/dev/null | awk '/^CASE/{l=$2; p=$4} /^INSTALL OK/{print p l}'); do
  python3 $R/testrig/mkdisk.py get $W/q.img "$d\\DATA.BIN" $W/d.bin >/dev/null 2>&1
  cmp -s $W/d.bin $ST/data.bin && echo "BYTES $d OK" >> $W/UT.TXT || echo "BYTES $d BAD" >> $W/UT.TXT; done
python3 -I $T/wavrms.py $W/out.wav >> $W/UT.TXT 2>/dev/null
tr -d '\r' < $W/UT.TXT 2>/dev/null | grep -E '^(CASE|DISC|MOUNT|INSTALL|PLAY|UNMOUNT|BYTES|DONE|WAV)' | tr '\n' ' '; echo

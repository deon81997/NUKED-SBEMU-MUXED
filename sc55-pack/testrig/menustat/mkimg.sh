#!/bin/sh
# mkimg.sh ZIP OUTDIR [EXE...] - two MS-DOS 7.1 test disks with the pack's NUKEDSB folder in C:\ (store-dos71):
#   OUTDIR/pm.img  CONFIG.SYS as stored (HIMEM only, like the P5PE-VM)   OUTDIR/rm.img  JEMMEX X2MAX=8192 NOEMS
# EXE...: rebuilt EXEs copied over NUKEDSB/FILES/SC55 (e.g. output/sbemu.exe output/sc55menu.exe).
set -e
ZIP=$1 OUT=$2; shift 2
IMG=${DOS71:-/home/claude/vms/dos71/msdos71.img}
T=$(mktemp -d); mkdir -p "$OUT"
unzip -q "$ZIP" -d "$T"
for e in "$@"; do cp "$e" "$T/NUKEDSB/FILES/SC55/$(basename "$e" | tr a-z A-Z)"; done
for m in pm rm; do
  cp --sparse=always "$IMG" "$OUT/$m.img"
  mcopy -o -s -i "$OUT/$m.img@@32256" "$T/NUKEDSB" ::/
  mcopy -o -i "$OUT/$m.img@@32256" "$(dirname "$0")/MSTAT.BAT" ::/
done
cp "$T/NUKEDSB/FILES/JEMM/JEMMEX.EXE" "$T/JEMMEX.EXE"
mcopy -o -i "$OUT/rm.img@@32256" "$T/JEMMEX.EXE" ::/
printf 'DEVICE=C:\\JEMMEX.EXE X2MAX=8192 NOEMS\r\nFILES=40\r\nBUFFERS=20\r\n' > "$T/CONFIG.SYS"
mcopy -o -i "$OUT/rm.img@@32256" "$T/CONFIG.SYS" ::/
rm -rf "$T"

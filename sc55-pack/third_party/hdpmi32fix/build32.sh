#!/bin/bash
# build32.sh HXDIR OUTDIR [extra jwasm -D options...]: HDPMI32I.EXE (IOPL0, port trap) with jwasm + OW wlink
set -e
HX=$1; O=$2; shift 2
J=/home/claude/jwasm-src/build/GccUnixR/jwasm; L=/home/claude/ow/binl64/wlink
U=$(mktemp -d); mkdir -p $U/S $U/I "$O"
for f in "$HX"/Src/HDPMI/*; do b=$(basename "$f"); ln -sf "$f" "$U/S/$(echo $b | tr a-z A-Z)"; ln -sf "$f" "$U/S/$(echo $b | tr A-Z a-z)" 2>/dev/null || true; done
for f in "$HX"/Include/*; do ln -sf "$f" "$U/I/$(basename "$f" | tr a-z A-Z)"; ln -sf "$f" "$U/I/$(basename "$f" | tr A-Z a-z)" 2>/dev/null || true; done
cd $U/S
mods="HDPMI A20GATE CLIENTS EXCEPT HEAP HELPERS I2FHDPMI I31DEB I31DOS I31FPU I31INT I31MEM I31SEL I31SWT INIT INT13API INT21API INT2FAPI INT2XAPI INT31API INT33API INT41API INTXXAPI MOVEHIGH PAGEMGR PUTCHR PUTCHRR SWITCH VXD"
files=""
for m in $mods; do
  "$J" -nologo -c -Cp -Sg -D?32BIT=1 -D?PMIOPL=0 -D?VIODIROUT=${VIO:-1} "$@" -I$U/I -I$U/S -Fo"$O/$m.OBJ" $m.ASM > $O/$m.log || { cat $O/$m.log; exit 1; }
  files="$files file $m.OBJ"
done
cd "$O" && WATCOM=/home/claude/ow "$L" format DOS $files name HDPMI32I.EXE op q, map=HDPMI32I.MAP, stack=0
ls -l "$O/HDPMI32I.EXE"

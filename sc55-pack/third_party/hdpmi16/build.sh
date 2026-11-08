#!/bin/bash
# Builds HDPMI16I.EXE (16-bit HDPMI with SBEMU's port-trap API) for SBEMU16.COM.
#   build.sh HXDIR JWASM JWLINK OUTDIR
# HXDIR  = a checkout of https://github.com/crazii/HX (tested at 43182b4) with hdpmi16.diff applied
#          (cd HXDIR && patch -p1 < hdpmi16.diff)
# JWASM / JWLINK = JWasm and JWlink binaries (https://github.com/Baron-von-Riedesel/JWasm, .../JWlink)
# The sources mix upper/lower case file names in INCLUDEs: build on a case-insensitive file system, or give
# every file in Src/HDPMI and Include an upper-case name (e.g. a folder of upper-case symlinks).
set -e
HX=$1; J=$2; L=$3; O=$4
mkdir -p "$O"
cd "$HX/Src/HDPMI"
mods="HDPMI A20GATE CLIENTS EXCEPT HEAP HELPERS I2FHDPMI I31DEB I31DOS I31FPU I31INT I31MEM I31SEL I31SWT INIT INT13API INT21API INT2FAPI INT2XAPI INT31API INT33API INT41API INTXXAPI MOVEHIGH PAGEMGR PUTCHR PUTCHRR SWITCH VXD"
files=""
for m in $mods; do
  "$J" -nologo -c -Cp -Sg -D?32BIT=0 -D?PMIOPL=0 -D?VIODIROUT=1 -I"$HX/Include" -Fo"$O/$m.OBJ" $m.ASM
  files="$files file $m.OBJ"
done
cd "$O" && "$L" format DOS $files name HDPMI16I.EXE op q, map=HDPMI16I.MAP, stack=0

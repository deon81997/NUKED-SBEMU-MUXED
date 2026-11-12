#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# Cross-build SC55W.EXE for Windows 98 (Pentium M / SSE2) with MinGW-w64.
set -e
cd "$(dirname "$0")"
N=/home/claude/nsc55j; M=/home/claude/munt/mt32emu; S=/home/claude/sbemu-src/sbemu; O=/home/claude/nopl3
ARCH=${ARCH:-"-march=pentium-m -mfpmath=sse -O2"}   # ARCH="-march=pentium2 -O2" OBJ=build98p2 LIB=b98p2: test build for 86Box (no SSE2)
OBJ=${OBJ:-build98}; LIB=${LIB:-b98}
OD=${OD:-$OD}; EXE=${EXE:-SC55W.EXE}   # buildall.sh: SC55WM.EXE (SSE2) + SC55W2.EXE (Pentium II) + the launcher
BID=$(( $(date +%s) % 100000 + 100000 ))
CXX="i686-w64-mingw32-g++ -std=c++23 $ARCH -DBUILD_ID=$BID -D_USE_MATH_DEFINES -I$N/build98/backend -I$N/src -I$N/src/backend -I$M/b98 -I$M/src -I$S -I$O"
mkdir -p $OD
for f in opts romscan synths engine w98main install98 pad joyout gameport; do
  $CXX -c helper/$f.cpp -o $OD/$f.o &
done
i686-w64-mingw32-g++ -std=c++17 $ARCH -c $S/dbopl.cpp -o $OD/dbopl.o &
i686-w64-mingw32-gcc $ARCH -c $O/opl3.c -o $OD/opl3.o &
i686-w64-mingw32-gcc -O2 -c compat/compat98.c -o $OD/compat98.o &
i686-w64-mingw32-gcc $ARCH -DPADCONV_LIB -c ../tools/padmap/padconv.c -o $OD/padconv.o &
wait
i686-w64-mingw32-windres -I helper helper/sc55w.rc -O coff -o $OD/sc55w.res
i686-w64-mingw32-g++ -static -mwindows -s -o out/$EXE $OD/*.o $OD/sc55w.res \
  $(ls $N/$OBJ/*.o) $M/$LIB/libmt32emu.a -lwinmm -lcomdlg32 -lshell32 -lcomctl32
python3 tools/chkimp.py out/$EXE || true
ls -l out/$EXE
sh mididrv/build.sh && cp mididrv/SC55MIDI.DRV out/
sh vxd/build.sh >/dev/null && cp vxd/SC55W.VXD out/
sh joy/build.sh >/dev/null && mkdir -p out/JOY && cp joy/NUKEDJOY.VXD joy/NUKEDJOY.INF out/JOY/

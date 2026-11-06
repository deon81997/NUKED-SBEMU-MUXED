#!/bin/sh
# Cross-build SC55W.EXE for Windows 98 (Pentium M / SSE2) with MinGW-w64.
set -e
cd "$(dirname "$0")"
N=/home/claude/nsc55j; M=/home/claude/munt/mt32emu; S=/home/claude/sbemu-src/sbemu; O=/home/claude/nopl3
ARCH="-march=pentium-m -mfpmath=sse -O2"
BID=$(( $(date +%s) % 100000 + 100000 ))
CXX="i686-w64-mingw32-g++ -std=c++23 $ARCH -DBUILD_ID=$BID -D_USE_MATH_DEFINES -I$N/build98/backend -I$N/src -I$N/src/backend -I$M/b98 -I$M/src -I$S -I$O"
mkdir -p out/obj
for f in opts romscan synths engine w98main install98; do
  $CXX -c helper/$f.cpp -o out/obj/$f.o &
done
i686-w64-mingw32-g++ -std=c++17 $ARCH -c $S/dbopl.cpp -o out/obj/dbopl.o &
i686-w64-mingw32-gcc $ARCH -c $O/opl3.c -o out/obj/opl3.o &
i686-w64-mingw32-gcc -O2 -c compat/compat98.c -o out/obj/compat98.o &
wait
i686-w64-mingw32-windres -I helper helper/sc55w.rc -O coff -o out/obj/sc55w.res
i686-w64-mingw32-g++ -static -mwindows -s -o out/SC55W.EXE out/obj/*.o out/obj/sc55w.res \
  $(ls $N/build98/*.o) $M/b98/libmt32emu.a -lwinmm -lcomdlg32 -lshell32
python3 tools/chkimp.py out/SC55W.EXE || true
ls -l out/SC55W.EXE
sh mididrv/build.sh && cp mididrv/SC55MIDI.DRV out/
sh vxd/build.sh >/dev/null && cp vxd/SC55W.VXD out/

#!/bin/sh
# Builds what build98.sh links besides our own code, for one CPU target:
#   $N/$OBJ/*.o          Nuked-SC55 core (this repo's upstream/Nuked-SC55, config.h = sc55/config/config.h)
#   $M/$LIB/libmt32emu.a Munt mt32emu (github.com/munt/munt d885a95, 2.8.5, static, internal resampler)
# Release target (Pentium M, SSE2):   sh tools/build-deps.sh
# 86Box test target (Pentium II):     ARCH="-march=pentium2 -O2" OBJ=build98p2 LIB=b98p2 sh tools/build-deps.sh
# Needs i686-w64-mingw32-g++ on PATH from tools/build-gcc.sh (msvcrt + win32 threads; NOT xPack, which is UCRT) and $MUNT = the munt checkout.
set -e
cd "$(dirname "$0")/.."
P=$(cd ../.. && pwd)/sc55-pack
N=${N:-/home/claude/nsc55j}; MUNT=${MUNT:-/home/claude/munt}; M=$MUNT/mt32emu
ARCH=${ARCH:-"-march=pentium-m -mfpmath=sse -O2"}
OBJ=${OBJ:-build98}; LIB=${LIB:-b98}
CXX=i686-w64-mingw32-g++; CC=i686-w64-mingw32-gcc

# Nuked-SC55: $N/src -> the repo's copy, config.h as the DOS build's
mkdir -p "$N/build98/backend" "$N/$OBJ"
[ -e "$N/src" ] || ln -s "$P/upstream/Nuked-SC55/src" "$N/src"
cp "$P/sc55/config/config.h" "$N/build98/backend/config.h"
for f in "$N"/src/backend/*.cpp; do
  $CXX -std=c++23 $ARCH -fno-rtti -DNDEBUG -I"$N/build98/backend" -I"$N/src" -I"$N/src/backend" \
    -c "$f" -o "$N/$OBJ/$(basename "$f" .cpp).o" &
done
$CC $ARCH -DNDEBUG -c "$N/src/backend/sha/sha224-256.c" -o "$N/$OBJ/sha224-256.o" &
wait

# Munt: config.h by hand (static, all API types, no version tagging)
mkdir -p "$M/b98" "$M/$LIB/obj"
cat > "$M/b98/config.h" <<'EOF'
#ifndef MT32EMU_CONFIG_H
#define MT32EMU_CONFIG_H
#define MT32EMU_VERSION      "2.8.5"
#define MT32EMU_VERSION_MAJOR 2
#define MT32EMU_VERSION_MINOR 8
#define MT32EMU_VERSION_PATCH 5
#define MT32EMU_EXPORTS_TYPE 3
#define MT32EMU_WITH_VERSION_TAGGING 0
#undef MT32EMU_RUNTIME_VERSION_CHECK
#endif
EOF
mkdir -p "$M/b98/mt32emu"; cp "$M/b98/config.h" "$M/b98/mt32emu/config.h"
SRCS="Analog BReverbModel Display File FileStream LA32FloatWaveGenerator LA32Ramp LA32WaveGenerator MidiStreamParser
 Part Partial PartialManager Poly ROMInfo Synth Tables TVA TVF TVP sha1/sha1 SampleRateConverter c_interface/c_interface
 srchelper/srctools/src/FIRResampler srchelper/srctools/src/SincResampler srchelper/srctools/src/IIR2xResampler
 srchelper/srctools/src/LinearResampler srchelper/srctools/src/ResamplerModel srchelper/InternalResampler"
for s in $SRCS; do
  $CXX -std=c++11 $ARCH -DNDEBUG -DMT32EMU_WITH_INTERNAL_RESAMPLER -I"$M/b98" -I"$M/src" \
    -c "$M/src/$s.cpp" -o "$M/$LIB/obj/$(echo "$s" | tr / _).o" &
done
wait
rm -f "$M/$LIB/libmt32emu.a"
i686-w64-mingw32-ar rcs "$M/$LIB/libmt32emu.a" "$M/$LIB"/obj/*.o
ls -l "$M/$LIB/libmt32emu.a"; ls "$N/$OBJ" | wc -l

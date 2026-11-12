#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# The Windows 98 release: SC55WM.EXE (Pentium M, SSE2), SC55W2.EXE (Pentium II, no SSE2), SC55W.EXE (launcher).
set -e
cd "$(dirname "$0")"
EXE=SC55WM.EXE OD=out/objm sh build98.sh
ARCH="-march=pentium2 -O2" OBJ=build98p2 LIB=b98p2 EXE=SC55W2.EXE OD=out/obj2 sh build98.sh
i686-w64-mingw32-gcc -march=i386 -Os -s -mwindows -o out/SC55W.EXE helper/launch.c
ls -l out/SC55W.EXE out/SC55WM.EXE out/SC55W2.EXE

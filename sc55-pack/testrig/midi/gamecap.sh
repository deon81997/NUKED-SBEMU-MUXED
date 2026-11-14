#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# gamecap.sh CASE [HC] - USB MIDI byte-exact check (beta.121): SBEMU /MUSB:1 /MIDICAP:1 + the fake Uno (UART pacing),
# a game or an MPUPLAY script plays, then the guest RAM is dumped and midicap.py compares what the game wrote to the
# MPU-401 with what the fake Uno received. Last line PASS/FAIL.
#   CASE: duke heretic descent (games: see README.md for unpacking them into $G) | miles gssysex mi2 intel (mkscript.py)
#         | rm (RMPLAY.COM plays MILES.TXT as RM.BIN from real mode: SBEMU's real-mode trap via JEMM + QPIEMU)
#   env OLD=dir: run OLD/SBEMU.EXE instead (before/after; script cases only: compared against the script itself)
#   env G=/tmp/g (unpacked games), S=/tmp/scr (MPUPLAY.EXE + scripts), SECS (default 75), SBX (more SBEMU options)
H=$(cd "$(dirname "$0")" && pwd); G=${G:-/tmp/g}; S=${S:-/tmp/scr}; C=$1; HC=${2:-uhci}; SECS=${SECS:-75}
SB="SBEMU /VMPU /JOY:1 /MUSB:1 /MIDICAP:1 $SBX"
[ -n "$OLD" ] && SB="C:\\OLD\\SBEMU /VMPU /JOY:1 /MUSB:1 $SBX"
DIRS="$S:T"; [ -n "$OLD" ] && DIRS="$DIRS,$OLD:OLD"
case $C in
  duke)    RUN='CD \DUKE|DUKE3D'; DIRS="$DIRS,$G/duke:DUKE" ;;
  heretic) RUN='CD \HERETIC|HERETIC'; DIRS="$DIRS,$G/heretic:HERETIC" ;;
  descent) RUN='CD \DESCENT|4GWBIND 4GWPRO.EXE DESCENT.UBN DCNTSHR.EXE -Q > NUL|DCNTSHR'; DIRS="$DIRS,$G/descent:DESCENT" ;;
  rm)      SB="C:\\SC55\\JLOAD C:\\SC55\\QPIEMU.DLL|$SB"; RUN='C:\T\RMPLAY > C:\OUT.TXT' ;;   # real-mode game: MILES.TXT as RM.BIN
  miles|gssysex|mi2|intel) U=$(echo $C | tr a-z A-Z); RUN="C:\\T\\MPUPLAY C:\\T\\$U.TXT > C:\\OUT.TXT" ;;
  *) echo "unknown case $C FAIL"; exit 2 ;;
esac
D=/tmp/gamecap-$C-$HC$RIGPORT.mem; rm -f $D
RIGTAG=-$C${OLD:+-old} python3 "$H/rig.py" $HC "$SB|$RUN" $SECS --dir "$DIRS" --dump $D --mem 64 > /dev/null 2>&1
L=$H/fakemidi-$HC-$C${OLD:+-old}.log
if [ -n "$OLD" ]; then python3 "$H/midicap.py" x "$L" --script "$S/$(echo $C | tr a-z A-Z).TXT"
else python3 "$H/midicap.py" $D "$L"; fi
r=$?; rm -f $D; exit $r

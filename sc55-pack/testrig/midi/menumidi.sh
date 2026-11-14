#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# menumidi.sh [HC] - MENU matrix, USB MIDI case (beta.121): one QEMU boot with the fake M-Audio Uno (UART pacing):
#   SBEMU /MUSB:1 /MIDICAP:1, then MENU state (SC55MENU /STATUS: "MENU USB MIDI: ... active"), GAMETEST /USB (sees the
#   device), MPUTEST /USB (tune + running status + split SysEx all sent: PASS), SBDIAG USB, then Duke 3D plays and its
#   MPU-401 stream must reach the fake byte-exact (midicap.py). Needs the games unpacked in $G (README.md). PASS/FAIL.
H=$(cd "$(dirname "$0")" && pwd); HC=${1:-xhci}; G=${G:-/tmp/g}; S=${S:-/tmp/scr}; D=/tmp/menumidi-$HC.mem; rm -f $D
RIGTAG=-menu python3 "$H/rig.py" $HC 'SBEMU /VMPU /JOY:1 /MUSB:1 /MIDICAP:1|SC55MENU C:\SC55 /STATUS > C:\OUT.TXT|GAMETEST /USB >> C:\OUT.TXT|MPUTEST /USB >> C:\OUT.TXT|SBDIAG USB >> C:\OUT.TXT|CD \DUKE|DUKE3D' ${SECS:-110} \
  --dir "$S:T,$G/duke:DUKE" --dump $D --mem 64 > /dev/null 2>&1
O=$H/out-$HC-menu.txt; F=0
chk() { if tr -d '\r' < $O | grep -q "$2"; then echo "$1: PASS"; else echo "$1: FAIL ($(tr -d '\r' < $O | grep -m1 "$3"))"; F=1; fi; }
chk "MENU state      " '^MENU USB MIDI: .*(0763:0150) on .*, active' '^MENU'
chk "GAMETEST        " '^GAMETEST: USB MIDI: .*active' '^GAMETEST'
chk "MPUTEST /USB    " '^USB MIDI: PASS' 'USB MIDI:'
chk "SBDIAG USB      " 'MPU-401 mode uart, .* 0 dropped, 0 USB errors' 'device'
r=$(python3 "$H/midicap.py" $D "$H/fakemidi-$HC-menu.log" | tail -n 1); echo "Duke 3D bytes   : $r ($(python3 "$H/midicap.py" $D --quiet | head -n 1 | cut -d, -f1-3))"
echo "$r" | grep -q PASS || F=1
rm -f $D; [ $F = 0 ] && echo "menumidi $HC: PASS" || echo "menumidi $HC: FAIL"; exit $F

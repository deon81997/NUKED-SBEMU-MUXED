#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# all.sh [OUT] - MENU matrix part 2: every DOS x MODE x HC x PLUG through run.sh, J jobs at once (default 2).
# One line per run in OUT (default /home/claude/vm/matrix.txt): A = phase-A discs fully OK of 4 (1 FAT16, 2 FAT32,
# 3 exFAT, 4 NTFS), B = after the swap of 3 (5 FAT16 on the swapped stick, 3, 4), bytes, sound, PASS/FAIL.
T=$(cd "$(dirname "$0")" && pwd); OUT=${1:-/home/claude/vm/matrix.txt}; : > $OUT
one() { r=$($T/run.sh $1 $2 $3 $4 2>&1 | tail -1); W=/home/claude/vm/mm-$1-$2-$3-$4
  ok() { tr -d '\r' < $W/UT.TXT | awk -v ph=$1 '/^CASE/{p=$4; d=""} /^DISC/{d=$2} /^UNMOUNT OK/{if(p==ph) print d}' | sort -u | tr '\n' ,; }
  a=$(ok A); b=$(ok B); bad=$(echo "$r" | grep -o 'BYTES [A-Z]* BAD' | wc -l); wav=$(echo "$r" | grep -o '[0-9.]* s loud' | cut -d' ' -f1)
  v=PASS; [ "$a" = "1,2,3,4," ] && [ "$b" = "3,4,5," ] && [ $bad = 0 ] && echo "$r" | grep -q DONE && awk "BEGIN{exit !(${wav:-0}>1)}" || v=FAIL
  printf '%-3s %-4s %-6s %-7s A=%-9s B=%-7s badbytes=%s loud=%ss %s\n' $1 $2 $3 $4 "$a" "$b" $bad "$wav" $v >> $OUT; }
for d in ${DOSES:-fd 71}; do for m in ${MODES:-j32 x32 j16}; do for h in ${HCS:-uhci ohci ehci ehcic xhci}; do for p in ${PLUGS:-before after}; do
  while [ $(jobs -r | wc -l) -ge ${J:-2} ]; do sleep 1; done; one $d $m $h $p &
done; done; done; done; wait; sort -o $OUT $OUT; cat $OUT

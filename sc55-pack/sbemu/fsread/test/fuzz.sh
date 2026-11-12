#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# fuzz.sh IMAGE SS N: corrupt a few random bytes in sectors the reader really reads (logged by
# FSTEST_LOG: single-sector reads = metadata), N times; fstest must never crash (ASan) or hang (20 s). Build fstest with ASan first.
img=$1; ss=$2; n=$3; FS=${FSTEST:-/tmp/fstest}; t=/tmp/fz.$$
FSTEST_LOG=$t.lbas $FS $img 0 $ss >/dev/null 2>&1; sort -u $t.lbas > $t.u
bad=0; det=0; i=0
while [ $i -lt $n ]; do
  cp $img $t.img
  python3 -I - $t.img $t.u $ss $i <<'PY'
import sys, random
img, lbas, ss, seed = sys.argv[1], open(sys.argv[2]).read().split(), int(sys.argv[3]), int(sys.argv[4])
r = random.Random(seed); f = open(img, 'r+b')
for _ in range(r.randint(1, 20)):
    l = int(r.choice(lbas)); f.seek(l * ss + r.randrange(ss)); f.write(bytes([r.randrange(256)]))
PY
  timeout 20 $FS $t.img 0 $ss >/dev/null 2>$t.err; rc=$?
  [ $rc -eq 1 ] && det=$((det+1))
  if [ $rc -ne 0 ] && [ $rc -ne 1 ]; then bad=$((bad+1)); echo "seed $i rc=$rc"; grep -m3 -E 'ERROR|SUMMARY|runtime' $t.err; fi
  i=$((i+1))
done
echo "$img: $n runs, $det with errors reported, $bad crashes/hangs"; rm -f $t.*

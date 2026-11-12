#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# run.sh ROOT VMDIR [maxsecs] [cfgsed]: DOS disk from ROOT, 86Box P2B-LS PII-450 dynarec, until C:\STAGE.TXT = DONE;
# prints the result files (R.LOG MNT.TXT CDON.TXT DIR.TXT)
R=$1; V=$2; MAX=${3:-900}; H=/home/claude/NUKED-SBEMU-MUXED/sc55-pack/testrig
mkdir -p $V/nvr; cp /home/claude/padtest/86vm/nvr/* $V/nvr/
sed -e 's/^cpu_use_dynarec = 0/cpu_use_dynarec = 1/' -e 's/^hdd_01_parameters.*/hdd_01_parameters = 63, 16, 243, 0, ide/' ${4:+-e "$4"} $H/cpu/p2b450.cfg > $V/86box.cfg
MKDISK_BOOT=msdos python3 $H/mkdisk.py build $R $V/hd.img 120 > /dev/null || exit 1
pkill -f "86Box.*-P $V" ; sleep 0.5; rm -f $V/a.sock
pgrep -f "Xvfb :9" >/dev/null || (Xvfb :9 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 0.5
( cd $V; DISPLAY=:9 SDL_AUDIODRIVER=dummy exec /home/claude/src/86box/build/src/86Box -R /home/claude/src/roms -N -L $V/86box.log --automation unix:$V/a.sock -P $V > $V/stdout.log 2>&1 ) &
P=$!; t0=$(date +%s)
while :; do
  sleep 5
  python3 $H/mkdisk.py get $V/hd.img STAGE.TXT /tmp/stage.$$ >/dev/null 2>&1 && grep -q DONE /tmp/stage.$$ && break
  [ $(( $(date +%s) - t0 )) -gt $MAX ] && { echo "TIMEOUT"; python3 $H/86box/a.py $V/a.sock @screen 2>/dev/null | grep -v '^$' | tail -15; break; }
done
kill $P 2>/dev/null; pkill -f "86Box.*-P $V"; sleep 1; rm -f /tmp/stage.$$
echo "elapsed $(( $(date +%s) - t0 ))s"
for f in R.LOG MNT.TXT CDON.TXT DIR.TXT; do echo "--- $f"; python3 $H/mkdisk.py get $V/hd.img $f /tmp/f.$$ >/dev/null 2>&1 && tr -d '\r' < /tmp/f.$$; done; rm -f /tmp/f.$$

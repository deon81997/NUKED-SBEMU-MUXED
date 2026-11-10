#!/bin/bash
# cpurun.sh ROOTDIR VMDIR [maxsecs]: build a DOS disk from ROOTDIR, run it in 86Box (P2B-LS, Pentium II 450,
# ES1371, OPTi USB card, emulated 360 pad when VMDIR/env says so) until C:\STAGE.TXT says DONE, then print C:\M.LOG.
R=$1; V=$2; MAX=${3:-900}; H=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $V; cp "$(dirname "$0")/p2b450.cfg" $V/86box.cfg
MKDISK_BOOT=msdos python3 $H/mkdisk.py build $R $V/hd.img 120 > /dev/null || exit 1
pkill -f "86Box.*-P $V" ; sleep 0.5; rm -f $V/a.sock
pgrep -f "Xvfb :9" >/dev/null || (Xvfb :9 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 0.5
( cd $V; [ -f $V/env ] && . $V/env; DISPLAY=:9 SDL_AUDIODRIVER=dummy exec /home/claude/src/86box/build/src/86Box \
  -R /home/claude/src/roms -N -L $V/86box.log --automation unix:$V/a.sock -P $V > $V/stdout.log 2>&1 ) &
P=$!
t0=$(date +%s)
while :; do
  sleep 10
  python3 $H/mkdisk.py get $V/hd.img STAGE.TXT /tmp/stage.$$ >/dev/null 2>&1 && grep -q DONE /tmp/stage.$$ && break
  [ $(( $(date +%s) - t0 )) -gt $MAX ] && { echo "TIMEOUT"; break; }
done
kill $P 2>/dev/null; pkill -f "86Box.*-P $V"; sleep 1; rm -f /tmp/stage.$$
echo "elapsed $(( $(date +%s) - t0 ))s"
python3 $H/mkdisk.py get $V/hd.img M.LOG /tmp/m.$$ >/dev/null 2>&1 && tr -d '\r' < /tmp/m.$$; rm -f /tmp/m.$$

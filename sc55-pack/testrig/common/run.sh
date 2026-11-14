#!/bin/bash
# run.sh [options] IMAGE BATCH [-- extra emulator args]
# Boots a DOS disk image headless, runs BATCH at the end of AUTOEXEC.BAT, waits until the batch has written a
# log with a PASS or FAIL line on C:, prints ONLY that log, kills the VM. Exit: 0 PASS, 1 FAIL, 124 timeout, 2 usage.
# The image is copied first (sparse), so IMAGE itself is never changed; use -i to run on IMAGE in place (faster
# for big images, but AUTOEXEC.BAT/RUNTEST.BAT/the log stay changed).
#
# BATCH contract: a DOS batch file; it must write its log to C:\RESULT.LOG (or -l NAME) and put a line with the
# word PASS or FAIL in it LAST, e.g.   SBEMU > C:\RESULT.LOG
#                                       IF ERRORLEVEL 1 GOTO bad
#                                       ECHO PASS >> C:\RESULT.LOG
#
# options:
#   -e qemu|86box   emulator (default qemu)
#   -t SECONDS      timeout (default 120 for qemu, 300 for 86box)
#   -l NAME         log file name in C:\ (default RESULT.LOG)
#   -r              replace AUTOEXEC.BAT with just the batch (default: append CALL C:\RUNTEST.BAT)
#   -p FILE         copy FILE into C:\ first (repeatable)
#   -s OUT.png      take a screenshot right before the VM is killed (only when asked)
#   -c CFG          86Box config (default /home/claude/86rig/vm/86box.cfg; its nvr/ dir is copied too)
#   -i              in place: no copy of IMAGE
#   -k              keep the work dir (image copy, emulator log) and print its path on stderr
# env: QEMU (default qemu-system-i386), B86 (86Box binary), ROMS (default /home/claude/86box/roms)
# While it runs, the work dir is /tmp/testrig-run-latest (shot.sh uses it).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ENG=qemu TMO="" LOG=RESULT.LOG REPL=0 INPL=0 KEEP=0 SHOT="" CFG=/home/claude/86rig/vm/86box.cfg
PUT=()
while getopts "e:t:l:rp:s:c:ik" o; do
  case $o in
    e) ENG=$OPTARG ;; t) TMO=$OPTARG ;; l) LOG=$OPTARG ;; r) REPL=1 ;; p) PUT+=("$OPTARG") ;;
    s) SHOT=$OPTARG ;; c) CFG=$OPTARG ;; i) INPL=1 ;; k) KEEP=1 ;;
    *) sed -n '2,30p' "$0" >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || { sed -n '2,30p' "$0" >&2; exit 2; }
IMG=$(readlink -f "$1") BAT=$2; shift 2
[ "${1:-}" = "--" ] && shift
EXTRA=("$@")
[ -f "$IMG" ] || { echo "no image $IMG" >&2; exit 2; }
[ -f "$BAT" ] || { echo "no batch $BAT" >&2; exit 2; }
[ -n "$TMO" ] || { [ "$ENG" = 86box ] && TMO=300 || TMO=120; }

WORK=$(mktemp -d /tmp/trun.XXXXXX)
ln -sfn "$WORK" /tmp/testrig-run-latest
VMPID="" XPID=""
cleanup() {
  [ -n "$VMPID" ] && kill "$VMPID" 2>/dev/null && sleep 0.3 && kill -9 "$VMPID" 2>/dev/null
  [ -n "$XPID" ] && kill "$XPID" 2>/dev/null && wait "$XPID" 2>/dev/null
  if [ $KEEP = 1 ]; then echo "work dir: $WORK" >&2; else rm -rf "$WORK"; fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM

if [ $INPL = 1 ]; then DISK=$IMG; else DISK=$WORK/disk.img; cp --sparse=always "$IMG" "$DISK"; fi

# first partition (MBR) or a plain FAT volume
OFF=0
if [ "$(od -An -tx1 -j510 -N2 "$DISK" | tr -d ' ')" = "55aa" ]; then
  LBA=$(od -An -tu4 -j454 -N4 "$DISK" | tr -d ' ')
  [ "$LBA" -gt 0 ] 2>/dev/null && OFF=$((LBA * 512))
fi
M="$DISK@@$OFF"
export MTOOLS_SKIP_CHECK=1 MTOOLS_NO_VFAT=0

for f in "${PUT[@]}"; do mcopy -o -i "$M" "$f" "::$(basename "$f" | tr a-z A-Z)" || exit 2; done
sed 's/\r$//; s/$/\r/' "$BAT" > "$WORK/RUNTEST.BAT"
mcopy -o -i "$M" "$WORK/RUNTEST.BAT" ::RUNTEST.BAT || exit 2
mdel -i "$M" "::$LOG" 2>/dev/null
if [ $REPL = 1 ]; then
  printf '@ECHO OFF\r\nCALL C:\\RUNTEST.BAT\r\n' > "$WORK/AUTOEXEC.BAT"
else
  { mtype -i "$M" ::AUTOEXEC.BAT 2>/dev/null | sed 's/\r$//; s/\x1a//g; s/$/\r/'; printf 'CALL C:\\RUNTEST.BAT\r\n'; } > "$WORK/AUTOEXEC.BAT"
fi
mcopy -o -i "$M" "$WORK/AUTOEXEC.BAT" ::AUTOEXEC.BAT || exit 2

echo "ENGINE=$ENG" > "$WORK/state"
case $ENG in
  qemu)
    ${QEMU:-qemu-system-i386} -machine pc -cpu pentium3 -m 64 -vga std -display none \
      -drive file="$DISK",format=raw,if=ide,index=0,cache=writethrough -boot c \
      -audiodev none,id=snd -device AC97,audiodev=snd \
      -monitor unix:"$WORK/mon.sock",server,nowait "${EXTRA[@]}" > "$WORK/emu.log" 2>&1 &
    VMPID=$!
    echo "MON=$WORK/mon.sock" >> "$WORK/state" ;;
  86box)
    B=${B86:-}
    for c in /home/claude/86box/86box/build/src/86Box /home/claude/86box/src/build/src/86Box "$(command -v 86Box)"; do
      [ -z "$B" ] && [ -x "$c" ] && B=$c
    done
    [ -n "$B" ] || { echo "86Box binary not found (set B86)" >&2; exit 2; }
    mkdir -p "$WORK/vm"
    [ -d "$(dirname "$CFG")/nvr" ] && cp -r "$(dirname "$CFG")/nvr" "$WORK/vm/"
    CYL=$(( $(stat -c %s "$DISK") / 512 / (16 * 63) ))
    sed "s|^hdd_01_fn = .*|hdd_01_fn = $DISK|; s|^hdd_01_parameters = [0-9]*, [0-9]*, [0-9]*,|hdd_01_parameters = 63, 16, $CYL,|" \
      "$CFG" > "$WORK/vm/86box.cfg"
    D=90; while [ -e /tmp/.X11-unix/X$D ] || [ -e /tmp/.X$D-lock ]; do D=$((D + 1)); done
    Xvfb :$D -screen 0 1024x768x24 >/dev/null 2>&1 & XPID=$!
    for _ in $(seq 50); do [ -e /tmp/.X11-unix/X$D ] && break; sleep 0.1; done
    DISPLAY=:$D QT_QPA_PLATFORM=xcb ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy \
      "$B" -N -P "$WORK/vm" -R "${ROMS:-/home/claude/86box/roms}" -L "$WORK/86box.log" "${EXTRA[@]}" > "$WORK/emu.log" 2>&1 &
    VMPID=$!
    echo "DISPLAY=:$D" >> "$WORK/state" ;;
  *) echo "engine must be qemu or 86box" >&2; exit 2 ;;
esac
echo "PID=$VMPID" >> "$WORK/state"

# wait for the log: a PASS/FAIL line, then unchanged for one more poll (DOS may still be closing the file)
T0=$(date +%s) PREV="" RC=124 OUT=""
while :; do
  if ! kill -0 "$VMPID" 2>/dev/null; then echo "emulator exited:" >&2; tail -5 "$WORK/emu.log" >&2; RC=3; break; fi
  OUT=$(mtype -i "$M" "::$LOG" 2>/dev/null | tr -d '\r\032')
  if printf '%s\n' "$OUT" | grep -qwE 'PASS|FAIL'; then
    if [ "$OUT" = "$PREV" ]; then printf '%s\n' "$OUT" | grep -qw FAIL && RC=1 || RC=0; break; fi
    PREV=$OUT
  fi
  [ $(($(date +%s) - T0)) -ge "$TMO" ] && break
  sleep 0.5
done
[ -n "$SHOT" ] && "$HERE/shot.sh" "$SHOT" >&2
case $RC in
  124) echo "TIMEOUT after ${TMO}s ($ENG), no PASS/FAIL in C:\\$LOG" >&2; [ -n "$OUT" ] && printf '%s\n' "$OUT" ;;
  3) ;;
  *) printf '%s\n' "$OUT" ;;
esac
exit $RC

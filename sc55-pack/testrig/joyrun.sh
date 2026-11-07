#!/bin/bash
# joyrun.sh IMG [QEMU USB ARGS...] - run the DOS test PC with fakepad.py on port 4711 for $SECS seconds (default 60).
# PAD=script picks the pad script. Writes qemu.log and shot.ppm (screen at the end) in the current folder.
HERE=$(cd "$(dirname "$0")" && pwd)
IMG=$1; shift
pkill -f fakepad.py; sleep 0.3
python3 "$HERE/fakepad.py" 4711 "${PAD:?set PAD=pad script}" &
sleep 0.5
rm -f mon.sock
qemu-system-i386 -machine pc -cpu pentium3 -m 64 -drive file="$IMG",format=raw,if=ide,index=0 -boot c \
  -audiodev none,id=snd -device AC97,audiodev=snd -display none -vga std -monitor unix:mon.sock,server,nowait \
  -chardev socket,id=pad,host=127.0.0.1,port=4711 "$@" > qemu.log 2>&1 &
QP=$!
sleep "${SECS:-60}"
echo "screendump shot.ppm" | socat - UNIX-CONNECT:mon.sock >/dev/null 2>&1; sleep 1
echo quit | socat - UNIX-CONNECT:mon.sock >/dev/null 2>&1; sleep 1
kill $QP 2>/dev/null; pkill -f fakepad.py

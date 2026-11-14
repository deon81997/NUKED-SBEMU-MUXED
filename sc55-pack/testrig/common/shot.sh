#!/bin/bash
# shot.sh OUT.png [WORKDIR] - screenshot of a VM that run.sh is running (default WORKDIR /tmp/testrig-run-latest).
# Only when asked: run.sh never takes one by itself (use run.sh -s OUT.png, or call this from another shell).
# QEMU: monitor screendump; 86Box: the Xvfb screen it runs on. Needs ImageMagick (convert/import).
set -e
OUT=${1:?usage: shot.sh OUT.png [WORKDIR]}
W=${2:-/tmp/testrig-run-latest}
[ -f "$W/state" ] || { echo "no running VM in $W" >&2; exit 1; }
. "$W/state"
case $ENGINE in
  qemu)
    python3 -I - "$MON" "$W/shot.ppm" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(5); time.sleep(0.1); s.recv(4096)
s.sendall(('screendump %s\n' % sys.argv[2]).encode()); time.sleep(0.5); s.recv(4096); s.close()
PY
    convert "$W/shot.ppm" "$OUT" ;;
  86box)
    import -display "$DISPLAY" -window root "$OUT" ;;
esac
echo "$OUT"

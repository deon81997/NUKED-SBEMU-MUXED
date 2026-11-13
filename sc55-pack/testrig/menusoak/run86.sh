#!/bin/bash
# run86.sh VMDIR IMG : 86Box headless with automation at VMDIR/a.sock
V=$1; cp --sparse=always $2 $V/hd.img; pkill -f "86Box.*-P $V" ; sleep 0.5; rm -f $V/a.sock
pgrep -f "Xvfb :9" >/dev/null || (Xvfb :9 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 0.5
cd $V; DISPLAY=:9 SDL_AUDIODRIVER=dummy nohup /tmp/dl/86box/src/build/src/86Box -R /tmp/dl/86box-roms/roms -N -L $V/86box.log --automation unix:$V/a.sock -P $V > $V/stdout.log 2>&1 &
for i in $(seq 50); do [ -S $V/a.sock ] && break; sleep 0.2; done; ls $V/a.sock

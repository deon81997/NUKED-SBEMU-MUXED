#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# run.sh VMDIR : start 86Box headless (Xvfb :9) with automation at VMDIR/a.sock
V=$1; pkill -f "86Box.*$V" ; sleep 0.5; rm -f $V/a.sock
pgrep -f "Xvfb :9" >/dev/null || (Xvfb :9 -screen 0 1024x768x24 >/dev/null 2>&1 &) ; sleep 0.5
cd $V; [ -f $V/env ] && . $V/env; DISPLAY=:9 SDL_AUDIODRIVER=dummy nohup /home/claude/86box/86box/build/src/86Box -R /home/claude/86box/roms -N -L $V/86box.log --automation unix:$V/a.sock -P $V > $V/stdout.log 2>&1 &
for i in $(seq 50); do [ -S $V/a.sock ] && break; sleep 0.2; done; ls -l $V/a.sock

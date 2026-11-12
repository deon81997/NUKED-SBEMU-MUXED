#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# hot-plug the fake 360 pad into the running Win98 VM (SeaBIOS hangs IO.SYS if it is there at boot)
pgrep -f fakepad.py >/dev/null || (cd /home/claude/NUKED-SBEMU-MUXED/sc55-pack/testrig && python3 fakepad.py 4711 live:/tmp/padlive.txt > /tmp/fakepad.out 2>&1 &)
sleep 1
timeout 20 python3 /tmp/qk.py /tmp/w98mon.sock "m:chardev-add socket,id=pad,host=127.0.0.1,port=4711" s:1 "m:device_add usb-redir,chardev=pad,bus=usb-bus.0,id=padd"

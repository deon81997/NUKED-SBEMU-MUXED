# 86Box test rig (headless, scripted)

86Box (github.com/86Box/86box @ 0935207) built with the SDL UI and the automation socket, plus a local
emulated **Xbox 360 pad** (`usb_xpad.c`, `86box-xpad.diff`: new file, 2 extra USB hot-plug slots
`usb_xpad`/`usb_xpad2`, automation `usb`, `usb plug|unplug <slot>`).

Build: `cmake -S . -B build -G Ninja -DQT=OFF -DSDL2=ON -DAUTOMATION=ON -DRELEASE=ON -DRTMIDI=OFF -DFLUIDSYNTH=OFF
-DMUNT=OFF -DSOUNDCANVAS=OFF -DDISCORD=OFF -DDISASM=OFF` (needs SDL2, OpenAL, slirp, sndfile, freetype, png, zstd dev
packages); ROMs from github.com/86Box/roms.

- `run.sh VMDIR` starts 86Box on Xvfb :9 with the socket at VMDIR/a.sock; VMDIR/env is sourced
  (`export XPAD=2 XPAD_STATE=.../xpad1.st XPAD_STATE2=.../xpad2.st`).
- Pad state file: `lx=.. ly=.. rx=.. ry=.. lt=.. rt=.. buttons=HEX` (re-read when it changes).
- `a.py SOCK cmd...`: automation requests, plus `@type:TEXT` (\n = Enter) and `@screen` (text screen).
- `p2bls-uhci.cfg` ASUS P2B-LS (440BX, PIIX4E UHCI, PII-400, ES1371, + OPTi OHCI card);
  `p54tp4xe-ohci.cfg` ASUS P/I-P55TP4XE (430FX, no USB, Pentium 133, ES1371, OPTi 82C861 OHCI card).

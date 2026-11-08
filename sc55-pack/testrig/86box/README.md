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

## Qt rig used for beta.50 (`vm86.py`, `6via90ap-es1371.cfg`)
86Box v6.0 (Qt UI) on Xvfb :86 with openbox; keys/typing with xdotool, screenshots of the 86Box window,
sound to a WAV file at real-time pace through OpenAL Soft's wave backend (`ALSOFT_DRIVERS=wave`; an ALSA
"file" device with a null slave runs unpaced and filled the disk). Machine: Acorp 6VIA90AP (VIA Apollo
Pro 133A, VT82C686B), Celeron 533, ES1371. The board's onboard VIA AC'97 (BIOS: OnChip Sound = Auto,
Onboard Legacy Audio = Disabled) is found by SBEMU, but 86Box 6.0 crashed (segfault) after SBEMU had
played for a while, so the config uses an ES1371 card.
Celeron 533: SC55LOG mk1-v2.00 0.41x real time, Nuked OPL3 2.1x, DBOPL 4.7x.

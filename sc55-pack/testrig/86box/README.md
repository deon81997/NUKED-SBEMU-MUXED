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

## Regression run: `regress.py`

`python3 regress.py [--only joytest,kmtest,mputest,timetest,midplay,menu,usbtest,vm2] [--out DIR]` copies the built
programs (`output/*.exe`) and a MIDI file onto the two VMs (disk changed only while the VM is off), boots them and
checks SBEMU (pads found, Pentium 133 without CMOV), JOYTEST (+GrIP view), KMTEST, MPUTEST (screen, to the end,
plain text when redirected), TIMETEST, MIDPLAY, MENU text + all three 3D styles (screenshot colour count) and
USBTEST with pad 2 unplugged and plugged again. PASS/FAIL per check in DIR/regress.txt, screenshots in DIR
(default `output/regress`). Takes about 10 minutes. Keys are typed 80 ms apart (faster drops letters while SBEMU
watches the keyboard). Known failure: TIMETEST finds no MPU-401 in this VM (see HANDOFF).

`USB_IGNADDR=n` in a VM's `env`: the emulated USB devices ack the first n SET_ADDRESS requests without taking the
address (tests usbpad.c's recovery for devices that stay at address 0).

`XPAD_KIND` / `XPAD_KIND2` in a VM's `env`: `one` makes that pad an Xbox One pad (GIP, 045E:02EA: silent until the
power-on packet, the Xbox button in its own packet asking for an ack), `hid` a generic HID gamepad (0079:0006 style
report descriptor: X Y Z Z Rz bytes, a hat, 12 buttons). Same state file format; buttons in the 360 bit layout.

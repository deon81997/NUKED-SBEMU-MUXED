# QEMU test rig

Headless DOS PC for testing the pack: FreeDOS (KERNEL.SYS/COMMAND.COM from Rufus `res/freedos`), JEMMEX,
HDPMI, SBEMU on QEMU's ICH AC97, a CD drive (VIDE-CDD + SHSUCDX).

- `mkdisk.py build <folder> c.img 120` - bootable FreeDOS FAT16 disk from a folder; `get` / `ls` read it back.
- `mkiso.py duke.iso` - ISO9660 with `SUPPORT\DUKE3D.IDF` of the size Duke 3D Atomic 1.5 checks (479985668 bytes, sparse).
- `vm.py start | shot NAME | key K | type TEXT | cmd HMP | stop` - drives QEMU through its monitor.
- `kt.c` - KT.EXE: sends a key script to SBEMU's key player (same side channel as GAMETEST).

QEMU 8.2.2 built with `--target-list=i386-softmmu`, `--python=/usr/bin/python3.12`, dtc/keycodemapdb from GitHub mirrors, tests/fp skipped.

## USB joystick (Xbox 360 pad)

- `fakepad.py PORT SCRIPT` - a pretend wired Xbox 360 pad (045E:028E) speaking QEMU's usbredir protocol; QEMU
  connects with `-chardev socket,id=pad,host=127.0.0.1,port=PORT -device usb-redir,chardev=pad,bus=uhci.0`.
  SCRIPT lines are `seconds field=value ...` (lx ly rx ry lt rt, buttons in hex), counted from the first poll.
  Every reconnect is a fresh plug-in, so `device_del` / `device_add` in the monitor tests hot-plugging.
  It logs the guest's requests (descriptors, SET_CONFIGURATION, the LED command) to `fakepad.log`.
- `joyrm.asm` - JOYRM.COM (`nasm -f bin`), a real-mode read of port 201h: loop counts for axes 0/1 and the button
  bits, to check the QEMM/QPIEMU (real-mode) trap path. `JOYTEST /LOG:n FILE` checks the protected-mode path.
- `joyrun.sh` - boots `c.img` (FreeDOS + JEMMEX + QPIEMU + HDPMI32I + `SBEMU /JOY:1` + JOYTEST) with the pad;
  extra arguments pick the USB controller, e.g. `-device piix3-usb-uhci,id=uhci -device usb-redir,chardev=pad,bus=uhci.0`
  or ICH9 EHCI with UHCI companions (`ich9-usb-ehci1` + `ich9-usb-uhci1..3,masterbus=ehci.0`, pad on `bus=ehci.0`).
- Tested (Nov 2026, QEMU 8.2 from Ubuntu, which has usbredir built in): enumeration, LED, sticks / D-pad /
  triggers / buttons through PM and RM traps, Start -> Enter key, EHCI handover, pad + USB keyboard on one
  controller, unplug / replug while JOYTEST runs.

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
  controller, unplug / replug while JOYTEST runs. OHCI (beta.41): `-device pci-ohci,id=ohci` alone, next to a UHCI,
  and as NEC-style companions of an EHCI: `-device ich9-usb-ehci1,id=ehci -device pci-ohci,masterbus=ehci.0,firstport=0,num-ports=3
  -device pci-ohci,masterbus=ehci.0,firstport=3,num-ports=3`, pad on `bus=ehci.0` (QEMU's plain `usb-ehci` takes no companions).
- beta.42 (all controllers + hubs): `qemu-xhci` and `nec-usb-xhci` (fakepad advertises 64-bit ids, 32-bit bulk
  lengths and bulk streams, which QEMU's xHCI requires), `usb-hub` on UHCI / OHCI / xHCI (pad at `port=N.M`), two
  hubs (the empty one is given up after 3 s), EHCI alone with `FAKEPAD_SPEED=high` (QEMU attaches no full-speed
  device to a companion-less EHCI and has no high-speed hub, so split transactions are untested), hot-plug at the
  root and into a hub. Note: `joyrun.sh` runs `pkill -f fakepad.py`; don't start it from a shell whose own command
  line contains that name.
- beta.43: `USBTEST.EXE C:\UT.TXT /T:75` as the only program (after HDPMI) gives a full step log; `hotg.sh`-style
  runs (pad added at 30 s, removed at 45 s, added again at 52 s via `device_add` / `device_del`) on every layout.

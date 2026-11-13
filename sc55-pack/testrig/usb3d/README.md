# USB keyboard/mouse + 3D soak (beta.100)

QEMU DOS PC (FreeDOS, JEMMEX+QPIEMU, HDPMI32I, `SBEMU /JOY:1 /USBKB:1 /USBMS:2`, CTMOUSE) that loops
`MIDPLAY /V:2` and `SC55MENU`, while `soak.py` sends random V/C/J/arrows (MIDPLAY 3D) and F8/F3/F4/F6/arrows
(MENU 3D) plus mouse moves to QEMU's usb-kbd / usb-mouse through the HMP monitor.

- `T3D=dir mk.sh SBEMU.EXE` - c.img from the release zip unpacked in /home/claude/rel (FreeDOS files in /home/claude/fd).
  `NOLOOP=1`: stop at the prompt instead.
- `run.sh` (in dir) - boots it; default one `pci-ohci` with usb-kbd + usb-mouse; `USBDEV=...` for other layouts,
  e.g. NEC-like EHCI + 2 OHCI companions (see testrig/README.md). Build SBEMU with `-DOSLOWFORCE` to run the
  NEC uPD720101 "ED rewrite per transfer" path on QEMU's OHCI.
- `T3D=dir python3 soak.py SECONDS [SEED]` - screenshots m###.png / s###.png; `ALIVE.TXT` on c.img gets M / S
  each time MIDPLAY / MENU ended normally.

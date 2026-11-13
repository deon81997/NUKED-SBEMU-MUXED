# USB MIDI test rig (beta.108)

`../fakemidi.py` is a pretend class-compliant USB MIDI interface (M-Audio Uno 0763:0150: audio-control interface +
MIDIStreaming interface, bulk OUT 02h / bulk IN 81h, 64 bytes; `FAKEMIDI_SPEED=high`: high speed, 512 bytes) on QEMU's
usbredir. It logs every OUT packet and the MIDI in it (`MIDI 90 3C 64`), and sends `FAKEMIDI_IN` events on its IN endpoint.

`rig.py HC "AUTOEXEC lines" SECONDS [--high] [--hotplug T] [--files ..] [--pad]` boots FreeDOS + JEMMEX + HDPMI32I with
the built `output/*.exe` in C:\SC55 on QEMU (Ubuntu's qemu-system-i386 8.2), the fake device on `HC` = uhci (PIIX3),
ohci (pci-ohci), ehcicc (ICH9 EHCI + 3 UHCI companions), ehci (no companions, with `--high`), xhci (qemu-xhci).
`--hotplug T`: device_del at T s, a new chardev + device_add 3 s later. `--pad`: fakepad.py's 360 pad on the same bus.
Results: `out-HC.txt` (C:\OUT.TXT), `fakemidi-HC[-hs].log`. Needs testrig/rufus (FreeDOS kernel, ms-sys boot code:
`git clone --depth 1 https://github.com/pbatard/rufus`) and the release zip unpacked in /home/claude/rel.

Checked for beta.108 (all six: uhci, ohci, ehcicc, xhci, ehci-hs, xhci-hs):
- `USBTEST C:\OUT.TXT /T:12 /F`: device listed, test note on + off arrive, MIDI IN events shown.
- `SBEMU /MUSB:1 /JOY:1|MIDPLAY /S /Q:35 C:\T.MID` (genmid.py song) with `--hotplug 30`: the MIDI streams the device got
  are identical event for event on all six; after the re-plug the music continues.
- `MIDPLAY /USB /S /Q:8 C:\T.MID` (no SBEMU).
- The byte -> USB-MIDI packet parser (running status, SysEx with real-time bytes inside, system common) on the host:
  190000 random bytes in, the packets decode back to the same stream.

QEMU limits: QEMU's OHCI keeps one transfer in flight per controller and one data buffer (a waiting bulk IN held every OUT
back, and OUT bytes came back as IN data), so on QEMU's OHCI (106B:003F) the MIDI IN pipe is left off. `-DOSLOWFORCE`
(NEC OHCI mode) + usbredir's asynchronous bulk OUT trips an assertion in QEMU (usb_cancel_packet); the NEC path for
MIDI OUT needs the real card.

`perf.sh NAME SBEMU.EXE "options"`: 86Box P2B-LS, Pentium II 450, ES1371: SBDIAG START, MIDPLAY 40 s, SBDIAG report.

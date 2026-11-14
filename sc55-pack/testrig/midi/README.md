# USB MIDI test rig (beta.108, real-Uno fake + matrix beta.114)

beta.114: `../fakemidi.py` now looks like the real M-Audio USB Uno 0763:0150 rev 1.25 (usb-devices dump at
xed.ch/help/midi.html: bcdUSB 1.00, ep0 64 bytes, no serial, Atr C0 / 0 mA, bulk IN 81h listed before bulk OUT 02h,
64 bytes, interval 0) and paces OUT like its 31250-baud UART (late answers = NAKs); `FAKEMIDI_OLD=1` = the beta.108
fake. Hostile modes: `FAKEMIDI_OUT=nak|stall`, `FAKEMIDI_BAD=zero|long|nojack`, `FAKEMIDI_PID=1010` (pre-firmware
MidiSport). `rig.py ... --dos71` boots MS-DOS 7.1 + HIMEM only + HDPMI32I (the user's setup) from the VM store image.
`unomatrix.sh [quick]` (ONLY=regex for a subset) runs USBTEST / SBEMU /MUSB + MIDPLAY with unplug at 15 s / MIDPLAY /USB
on uhci, ohci, ehcicc, xhci in dos71 and jem, plus the hostile devices; results.txt, last line PASS/FAIL.
QEMU's OHCI cannot take a late OUT answer (it stops sending the rest of a multi-packet TD; a cancel then trips
usb_cancel_packet's assertion): on ohci the matrix runs the Uno shape with `FAKEMIDI_PACE=0`. 86Box has no USB
pass-through device, so the fake cannot run there.

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

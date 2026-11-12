# SSF2T / 32RTM investigation screenshots (86Box P2B-LS, ES1371 on IRQ 12, 2026-11-11)

HDPMI builds: H1 = crazii HX 43182b4 + mov-cr0.diff; H2 = H1 with ?IRQ_ROUTE_IVT=0; H3 = ?TRAP_CLISTI=0;
H4 = ?SKIP_CLISTI=0; H5 = H1 + pushad/popad around the real-mode IRQ call. SBEMU = beta.91.

- s1: H1 + SBEMU, SF2TURBO: 32RTM exception 0D in the decompressor (`rep movsb`, bad ESI/ECX).
- s2: H1 without SBEMU: game stops at "Unable to Initialise Sound Driver" (text screen, image is mostly blank).
- s3: H1 + SBEMU /JOY0 /USBMS0 /USBKB0 /VTIMER0: exception 0D in the clear-screen `rep stosd`, ECX=001DEB46 (should be <=3E80h).
- s4: H2, same crash (IRQ_ROUTE_IVT not the cause).
- s5: SBEMU /PM0: sound init fails before the crash point.
- s6: H3: hangs while SBEMU loads (TF trick for POPF is needed).
- s7: H4, same crash, ECX=001DEB60.
- s8: H5, same crash (real-mode IRQ handlers not the cause).
- s9: H1 + SBEMU with 86Box dynarec off: crash again in the decompressor, ECX=001CF8F8 (not an emulator bug).
- i1-i10: the game's INSTALL.EXE menus (attempt to select No Sound; the setting did not save).

MAKESWP itself exited normally in every run on this machine (no exit hang).

## USB exFAT / NTFS drive letters (USB disks session 5, 2026-11-12)

QEMU 8.2, EHCI, two USB sticks hot-plugged after boot (stick 1: MBR NTFS + exFAT = D: E:, stick 2: GPT exFAT = F:),
`SBEMU /JOY:1 /USBDISK:1` built with sbemu/usbredir.c hooked in (scratch build). Each is the screen at the end of a
run: SBEMU's start-up lines ("USB disk: 3 drive(s) D: E: F:, first NTFS 29 MB, read-only") and whatever DOS printed
to the screen (error messages); the commands' normal output went to a log file, summarised in HANDOFF.md
("USB disks, session 5").

| File | Run |
|---|---|
| usbredir-freedos-0-replug-qemu-refused.png | FreeDOS, unplug/replug, first try (QEMU refused the re-plug: "drive already in use"; test rig, not our code) |
| usbredir-freedos-1-dir-type-copy.png | FreeDOS: DIR, TYPE, COPY from D: E: F:, missing file, write refused ("Unable to open file 'D:\X.SYS'") |
| usbredir-freedos-2-cd-and-writes-refused.png | FreeDOS: CD into a USB folder, relative TYPE, COPY to / MD / DEL / REN refused |
| usbredir-freedos-3-unplug-replug.png | FreeDOS: stick 1 unplugged (D: E: not ready, F: still works) and plugged back (D: E: back) |
| usbredir-msdos71-1-dir-type-copy.png | MS-DOS 7.1: same as freedos-1 ("Access denied - D:\X.SYS") |
| usbredir-msdos71-2-djgpp-program.png | MS-DOS 7.1: DJGPP program reading through the redirector (copy, seek from end, opendir, stat) |
| usbredir-msdos71-3-stat-sizes.png | MS-DOS 7.1: findfirst / stat / fstat sizes |
| usbredir-msdos71-4-unplug-replug-new-usbpad.png | MS-DOS 7.1: unplug / replug again with the DOS session's newer usbpad.c |

# SC-55 pack — handoff

## Current state (SBEMU 1.0.0-beta.44-usb-sc55, branch usb-xpad)
- SBEMU + Nuked-SC55 (SC-55/mkII), Munt MT-32/CM-32L (/VMT32), SoundFont (/VSF); MPU side channel 0xA5-0xB0 (stats, SBDIAG, key player, triple-Esc, front panel/LCD + CPU load).
- Key player holds each typed key 500 ms (vdiag.c key_tick); Duke 3D quit default `ESC W3 Q W2 Y W3 Y W3 ESC W3 Q W2 Y W3 Y` — verified in QEMU with Duke 3D Atomic 1.5 from logo, title and demo (GAMETEST /RUN, 20 s and 40 s tests).
- Tools: SC55MENU (game profiles GAMES.CFG, safe mode, CPU%, first-run auto setup), SBSET (/TUNE benchmark, MT-32 choice), GAMETEST (/RUN), MIDPLAY (live SC-55/MT-32 LCD panel), CDGUI (CUE checks/fix), Ctrl+Alt+F12 in-game LCD overlay (text + mode 13h).
- Fonts: Terminus (OFL) text 8x14/8x16 + VESA graphics sizes 18-32 (tools/fontdata.h generated).
- Pack: /tmp/claude-0/final/PKG_C -> SC55PACK-HYBRID.zip (merged with the other session's CD/boot changes: CDCHK.COM, CDON real-CD + M:, LASTDRIVE=Z, static CDSET\HYBRID.BAT, SETUP refreshes CDSET), ROMS in FILES/ROMS (private).

## 2026-11-04 (CD images screen, Windows 98 part)
- tools/cdgui.c rewritten: sub-folders of C:\CDIMAGES, disc label/date and root files read from the image
  (ISO 9660 in .ISO, raw/2048 BIN, CUE data track), track strip + music length, I = mount and run
  INSTALL/SETUP, C = files on the disc, S = sort (name/size/type/label), F1 help, F6 animation,
  CDGUI.CFG keeps folder/sort/selection, a mounted disc is unmounted before another is mounted,
  failed unmount reported ("STUCK"). Tested in QEMU (MS-DOS 7.1 from Win98, uCDD 0.9.5-a2).
- uCDD 0.9.5-a2 refuses to unmount CUE images in the test rig ("The drive is locked or in use"),
  ISO images unmount fine. Not yet checked with a real CUE/BIN rip.
- CDON.BAT / CDDIAG.BAT: SHSUCDX /R- (files on the CD not read-only: installers failed with
  "Access denied").
- Windows 98 part (SC55W.EXE / SC55W.VXD / SC55MIDI.DRV, SETUP98.BAT, README98.TXT): source in
  win98/ (build98.sh; MinGW-w64 + Open Watcom + JWasm), binaries in the pack zip under FILES\WIN98.
- README.md (repo root) rewritten for the project; SBEMU's README is now README-SBEMU.md.

- Renamed to NUKED SB-EMU: menu logo (big "NUKED SB-EMU", B glyph added, M now 5 columns wide in
  tui.h, narrower Status panel), SBSET title, UNINSTAL, README.TXT title, Windows MIDI device /
  tray / dialogs / Add-Remove entry (uninstall still recognises the old "SC-55 PACK" MIDI Mapper
  entry). Release zip: NUKED-SB-EMU.zip with folder NUKEDSB. C:\SC55 and the BOOTCFG REM markers
  keep their names (RESET.BAT and existing installs rely on them). makefile builds SC55MENU now.

## 2026-11-06 (beta.36): game freezes after installing the Windows 98 part
- Cause (most likely): SETUP98 v1 replaced the user's own SET BLASTER line in AUTOEXEC.BAT with one
  built from the helper's defaults (I7, T4). SBEMU takes /A /I /D /H /T /P from BLASTER when
  SBOPTS.TXT does not give them, so in DOS mode SBEMU and in Windows the VxD moved to another IRQ
  than the games were set up for: freezes / stuck sound.
- Fix: the installer restores a "REM [SC55W-OLD]" line, never replaces a user BLASTER line, reads
  the user's line like SBEMU does (Options::apply_blaster), and writes /A /I /D (/H) /T /P
  explicitly into SBOPTS.TXT/.BAT. Settings always write them too. IRQ 10 removed from the list
  (BLASTER holds one digit). BLASTER T now uses SBEMU's numbering. Tested in QEMU Win98.

## Not verified on the D810 yet
- MT-32 sound, Ctrl+Alt+F12 overlay, VESA graphics modes, automatic-setup numbers, beta.33 timer give-back change (choppy music).

## Restoring a new session
- Source from this repo. Attach: SC55PACK-HYBRID.zip (pack + ROMs + third-party DOS programs) and sc55-restore-1/2/3.zip (DJGPP in two parts, QEMU, FreeDOS boot files, JEMMEX; RESTORE.md inside).

## Next steps
1. User: D810 works (USBTEST, beta.44 code). Still to confirm: NEC PCI card on the P5PE-VM with USBTEST, then SBEMU /JOY:1
   + JOYTEST + a game on both machines.
2. Possible next: CardBus slot bring-up (D810 PC Card), Xbox One pads (GIP init), INT 15h AH=84h BIOS joystick, Renesas
   firmware upload; check EHCI split transactions (pad behind an Intel 5/6-series built-in hub) on real hardware.
3. Older: Automatic setup, MIDPLAY S panel, Ctrl+Alt+F12, MT-32 game, GAMETEST Duke run; SBDIAG if music is choppy.

## Files
- sbemu/vdiag.c (diagnostics, key player, overlay), sbemu/vmpu_sc55.c (MPU, panel 0xB0), sbemu/vmpu_mt32.cpp (Munt), sc55/sc55synth.cpp (SC55_GetLCD), main.c (/VMT32)
- tools/: sc55menu.c, sbset.c, gametest.c, midplay.c, cdgui.c, sbdiag.c, tui.h, theme.h, fontdata.h, safewr.h
- third_party/mt32emu (Munt 2.8.3), upstream/Nuked-SC55
- dosimg/ (test rig): mkdisk.py (FreeDOS FAT16 image), mkiso.py (ISO9660 + Duke IDF), vm.py (QEMU driver); QEMU built in work/qemu/b7 (configure: --python=/usr/bin/python3.12, tests/fp skipped, dtc/keycodemapdb from GitHub mirrors)
- USB pad: sbemu/usbpad.c (polled UHCI host, EHCI handoff, 360 pad), sbemu/vjoy.c (port 201h, mapping, keys), main.c (/JOY*),
  tools/joytest.c, tools/sbset.c (Joystick page), tools/sc55menu.c; testrig/fakepad.py (usbredir pad), joyrun.sh, joyrm.asm
- Build: DJGPP gcc 12.2 from github.com/andrewwutw/build-djgpp v3.4 (djgpp-linux64-gcc1220.tar.bz2);
  `export PATH=<djgpp>/bin:$PATH; make -j8` (delete output/*.o after header changes)

## Resume prompt
"Continue NUKED SB-EMU from sc55-pack/HANDOFF.md (beta.44-usb): github.com/deon81997/NUKED-SBEMU-MUXED, branch usb-xpad. Rebuild with DJGPP, then continue from Next steps."

## 2026-11-06 (beta.37): SBEMU "Not loaded" on the D810 after SC55.BAT
- SBEMU no longer quits when the real sound card uses the IRQ asked for (/I or BLASTER): it takes another of 7/5/9, warns, and updates BLASTER.
- SC55W.EXE install: BLASTER values SBEMU can't use (port not 220/240, IRQ not 5/7/9, DMA not 0/1/3) are ignored instead of copied into SBOPTS.TXT.
- MENU: reads the MPU port from /P (it was fixed at 330, so /P:300 showed "Not loaded"); when SBEMU isn't loaded, row 5 shows the first error from SBLOG.TXT ("Last start failed: ..."); logo moved left so the status box no longer covers the last U.
- Tested in QEMU (AC97): /A:240 /I:5 /D:0 loads and shows Loaded; /A:250 shows "Last start failed: Error: Invalid IO port address".

## 2026-11-06 (beta.38): logging for bug reports
- SBEMU writes SBDETAIL.TXT (next to SBEMULOG, or SBEMUDETAIL=<file>, OFF disables): command line, environment,
  DOS/Windows version, CPU, all PCI class-04 devices with IRQ line/pin/BARs, PIC mask, every option with * when
  set, QEMM/HDPMI found, chosen card + PCI ID + IRQ, "[step]" lines (fsync'd, so a hang shows the last step),
  resolved SB/MPU values, trap/ISR results. Closed before going resident; atexit records quits with the last step.
- BUGREP.BAT (MENU > Files > Make a bug report) gathers everything into BUGREP.TXT; MENU > Files > SBEMU start-up details.
- SC55W.LOG: Windows version, BLASTER, memory, wave/MIDI out devices; every 30 s a line if underruns/late events/resyncs grew.
- Fixed: REM lines containing ">" made COMMAND.COM create stray files (SETUP.BAT left an empty "2)" file).
- build98.sh: BUILD_ID could start with 0 and fail to compile as an octal number (stale w98main.o got linked).

## 2026-11-06 (beta.39): "Not loaded" while loaded; GAMETEST on any port / IRQ
- Cause of the false "Not loaded": every tool probed only port 330 (menu: only /P from SBOPTS) with one reset and
  one read. Wrong when the MPU-401 is on another port (SBSET offers 300/310/320/332/334), when settings were changed
  but the resident SBEMU still used the old ones, and when a report (0xB0 panel, 0xA6 SBDIAG, 0xAF) was left
  half-read: the ACK read got a report byte.
- tools/mpuport.h: mpu_find(pref) = saved /P, BLASTER's P, then all SBSET ports; probe drains the data port, resets
  twice (UART mode), looks for 0xFE in up to 16 bytes. Used by SC55MENU, GAMETEST, MPUTEST, MIDPLAY, SBDIAG, TIMETEST.
- SBEMU: MPU 0xFF / 0x3F drops unread report bytes (DIAG_ResetFlush; not while a key script is received).
  Warns when BLASTER can't be written (environment full).
- MENU status shows the running SBEMU's values from BLASTER (SBxxx Ix Dx Pxxx). After a Settings screen, a loaded
  SBEMU takes the saved settings over at once (SBEMU @SBOPTS.TXT > SBAPPLY.TXT): tested port/IRQ/DMA/MPU move.
- SBSET starts from BLASTER for values not in SBOPTS and always writes /A /I /D /H /T /P.
- GAMETEST sets Doom-engine DEFAULT.CFG and Apogee/Build *.CFG sound lines to SBEMU's values before each test
  (backup <name>.GTB); bottom line shows the live values. Tested with stand-in games at 240/5/0 + MIDI 300.
- makefile: rules for gametest/sbdiag/timetest; VERSION fixed (git describe gave "98-2-g...").

## 2026-11-06 (beta.40-usb, branch usb-xpad): USB Xbox 360 pad as the gameport joystick
- /JOY:1 (Settings > Joystick): SBEMU traps port 201h (QEMM/QPIEMU + HDPMI) and answers with a USB 360 pad.
  /JOYAX axes (LX LY RX RY LT RT TR -, ~ inverts), /JOYBT buttons, /JOYKEY pad buttons typed as keys via KBC D2h
  (default Start=Enter, Back=Esc), /JOYDZ dead zone. D-pad also drives joystick A.
- USB: own polled UHCI host (no USB IRQ: LEGSUP=8F00, USBINTR=0), EHCI BIOS handoff + CONFIGFLAG=0 so ports go to the
  UHCI companions, one controller runs at a time, enumeration state machine, hot-plug. Matches interface FF/5D/01
  (wired) and FF/5D/81 (wireless receiver, untested). Polled from the sound ISR and on 201h reads (max every 0.5 ms).
- Axis time = 24 us + pos * 1100 us (TSC); unmapped axis / no pad = never times out.
- Not supported: OHCI, xHCI, hubs, Xbox One pads. BIOS USB keyboard/mouse stop while it runs.
- QEMU tests passed: PIIX3 UHCI, ICH9 EHCI+UHCI, pad + usb-kbd, hot-plug, PM (JOYTEST /LOG) and RM (JOYRM) paths.
- Watch on the D810: Pentium M TSC follows SpeedStep (axis scale shifts if the clock changes; recalibrate in the game).
- Release zip: release/NUKED-SB-EMU-1.0.0-beta.40-usb.zip (beta.39 + new SBEMU, SBSET, SC55MENU, JOYTEST, README.TXT).

## 2026-11-06 (beta.41-usb): OHCI controllers (NEC uPD720100/720101 PCI cards, SiS/ALi/nVidia/AMD)
- usbpad.c: controller abstraction (UHCI / OHCI). OHCI: SMM takeover via OCR, HCR reset, FmInterval/PeriodicStart,
  port power, root-hub reset (PRS/PRSC). Pad's IN endpoint = interrupt ED in every HCCA slot, two TDs used in turn
  (fill the empty tail, append a new one); control/OUT on a control ED (skip bit + 1 ms around changes, CLF).
  Inactive OHCIs stay operational with an empty HCCA so their ports still report plug-ins.
- QEMU passed: plain OHCI, UHCI + OHCI card (pad next to usb-kbd), ICH9 EHCI + 2 OHCI companions (NEC layout),
  OHCI hot-plug; UHCI and EHCI+UHCI re-tested. Release zip: NUKED-SB-EMU-1.0.0-beta.41-usb.zip.

## 2026-11-06 (beta.42-usb): every controller kind + hubs
- usbpad.c rewritten around a device table (parent hub, route string, TT hub/port) and per-controller ops.
- xHCI: BIOS handoff, protocol caps (only USB 2 ports powered/used), Intel XUSB2PR switch, HCRST order and PORTSC
  safe-write mask from xhci98's xhciqual (GPL-2.0); scratchpads, polled event ring (IMAN off), enable slot /
  address device / evaluate context / configure endpoint (pad endpoints; hub flag + ports), stall recovery
  (reset EP + set TR dequeue), disable slot on unplug.
- EHCI without companions: driven directly (async control QH, periodic interrupt QH, two qTDs in turn, split
  transaction hub address/port + S/C masks for full-speed devices behind a high-speed hub). EHCI with companions:
  handed over as before.
- Hubs (depth 4) on every controller: power, port status/reset via class requests, polled every 250 ms; several hubs
  take turns (3 s). USB steps trace in SBDETAIL ("USB steps:").
- QEMU passed: UHCI, OHCI, ICH9 EHCI+UHCI, EHCI+2 OHCI, EHCI alone (high-speed pretend pad), qemu-xhci, nec-usb-xhci,
  hubs on UHCI/OHCI/xHCI, two hubs, mixed UHCI+xHCI, hot-plug at root and in a hub. Not testable in QEMU: split.

## 2026-11-06 (beta.43-usb): review for real hardware, hot-plug fixes, USBTEST
- MMIO mapped page-aligned + offset (EHCI BARs can be 256-byte/1K aligned; a host rounding them would make the EHCI
  look companion-less and steal the NEC card's ports). MAX_HC 16 (ICH5 + NEC card = exactly the old 8).
- EHCI with companions: ports handed over explicitly (CONFIGFLAG=1, Port Owner + Port Power on every port,
  CONFIGFLAG=0) and ports that come back to the EHCI with a device on them are re-handed while scanning
  (QEMU did this on unplug: the replug was never seen on EHCI+companion layouts).
- OHCI: WDH/SF cleared after completions. PCI INTx disabled on MMIO controllers.
- Game reads of 201h only collect reports (USBPAD_PollLight); enumeration, hub checks, recovery stay in the sound
  interrupt. Trace stops once SBEMU is resident (no libc formatting in the ISR).
- USBTEST.EXE (tools/usbtest.c, same usbpad.c): PCI list, per-controller setup, steps, live input, plug/unplug log,
  /T:n auto-exit. MENU > Tests > USB pad finder.
- QEMU hot-plug (plug, unplug, replug) passes on: ICH EHCI+UHCI, UHCI, OHCI, NEC-style EHCI+2 OHCI, qemu-xhci,
  nec-usb-xhci, hubs on xHCI/OHCI/UHCI; SBEMU+JOYTEST end to end on ICH, NEC and xHCI+hub.

## 2026-11-07 (beta.44-usb): FIRST REAL-HARDWARE SUCCESS (D810, ICH6 UHCI)
- Cause of "nothing works on real hardware": CPU cache. The controller ran our frame list but acted on stale descriptors
  and the CPU never saw its writes (QH element read back terminated while the TD looked untouched). Fix: CLFLUSH
  descriptors / buffers before the controller is pointed at them and before reading results (fl() in usbpad.c,
  all controller kinds; skipped on CPUs without CLFLUSH).
- Also from real logs: OHCI HCCA per controller set once (NEC: pad found, no input), OHCI root reset 50 ms (NEC: pad
  "device not responding"), first descriptor read retried, register dumps in traces.
- D810 USBTEST log: hub 413C:0058 (Dell) found, flash drive 13FE:3600 skipped, pad on UHCI3 port 2, ~2900 reports,
  all buttons and sticks.

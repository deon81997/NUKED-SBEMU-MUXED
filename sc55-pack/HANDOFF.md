# SC-55 pack — handoff

## NEW SESSION QUICK START (Win98 line, newest: beta.81-w98.4, 2026-11-10)
- Read "VERSION NAMES" below, then the newest section at the END of this file ("beta.81-w98.4").
- Nothing outside the repo survives a new session. Rebuild what you need:
  - DOS (SBEMU): DJGPP gcc 12.2 cross (github release andrewwutw/build-djgpp), `make -j16` in sc55-pack.
  - Win98 EXE: `sh win98/tools/build-gcc.sh` (GCC 13.2 msvcrt + win32 threads, ~30 min; sources cloned from
    github, list in the script). NOT xPack/UCRT MinGW. Then `sh win98/tools/build-deps.sh; sh win98/build98.sh`
    (also builds SC55W.VXD, SC55MIDI.DRV and joy/NUKEDJOY.VXD into win98/out). Needs Open Watcom v2 at
    /home/claude/ow and JWasm at /home/claude/jwasm-src (win98/vxd/build.sh); Munt/Nuked-SC55 are in the repo.
  - Import check: `python3 win98/tools/chkimp2.py out/SC55W.EXE` with Windows 98 DLLs in /home/claude/w98dll
    (from the user's QuickInstall FULL.866, extracted with testrig/padkeys/mercy.py): only GetModuleHandleW and
    LoadLibraryW may be W-STUB.
  - Test PCs: QEMU 8.2 (with usbredir + pixman) and 86Box built from source; DOS boot disks with
    testrig/mkdisk.py (MKDISK_BOOT=msdos), Win98 VM per testrig/w98vm/README.md; fake 360 pad testrig/fakepad.py.
- Release recipe: pull; zip = newest zip in release/ with the changed files swapped in (python zipfile, keep
  every other entry); release/ keeps one zip; README.md news (5 newest); HANDOFF section; commit `w98: ...`;
  push -> the workflow publishes the tag from the zip name. Personal pack = public zip + the 52 entries of the
  user's personal zip that the public one lacks (ROMs, MSCDEX, VIDE-CDD, GSCDROM, MOUSE.COM); send it to the user.
- Standing user rules: after every major milestone push a backup + HANDOFF note; plain-language updates; say
  what still needs testing on the real PCs (Dell D810; ASUS P5PE-VM + NEC USB card + wired Xbox 360 pad).
- Last task (done in beta.81-w98.4): CPU-usage optimisation in DOS and Windows. Waiting for the user's next request.

## VERSION NAMES - two Claude sessions work on this repo (read first)
- DOS session: plain `beta.NN` (tag `v1.0.0-beta.NN-usb`, zip `NUKED-SB-EMU-1.0.0-beta.NN-usb.zip`).
- Windows 98 session (win98/, pack/README98.TXT, FILES\WIN98): `beta.NN-w98.M` = Win98 update M built
  on DOS beta.NN (tag `v1.0.0-beta.NN-w98.M-usb`, zip `NUKED-SB-EMU-1.0.0-beta.NN-w98.M-usb.zip`).
  M counts up across the whole Win98 line (w98.1, w98.2, ...) and does not restart with a new beta.
  Commits start with `w98:`; HANDOFF/README sections are titled `Win98 w98.M (on beta.NN)`.
- Never reuse the other session's next number: pull before releasing; the DOS session takes the next
  beta.NN, the Win98 session only ever adds -w98.M on top of the newest beta.NN. (beta.74 and beta.75
  were Win98 releases from before this rule.)
- Win98 work in progress (pad buttons -> keys per game, untested): branch `w98-pad-wip`, see its HANDOFF.md.
- release/ holds one zip, the newest. Build the next zip from that one (it has both sessions' files).
- README.md "What's NEW!!" shows the 5 newest entries; when adding one, move the oldest visible entry to the
  top of the "Older news" <details> block under them.

## Current state (SBEMU 1.0.0-beta.51-usb-sc55, branch usb-xpad)
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
1. User (beta.45): NEC card on the P5PE-VM with USBTEST (pad answered SET_ADDRESS then went silent, CC5: now 20 ms
   settle + retries); USB keyboard / mouse in SBEMU on real hardware (CTMOUSE after SBEMU, KMTEST); two pads in a game.
   Watch: a real BIOS whose INT 15h C2 serves a USB mouse itself (SeaBIOS does when the mouse is there at POST).
   /JOYMODE 1 CH and 2 FCS verified in Descent 1 (QEMU, see below); GrIP still needs a game with GrIP support.
2. Possible next: CardBus slot bring-up (D810 PC Card), Xbox One pads (GIP init), INT 15h AH=84h BIOS joystick, Renesas
   firmware upload; check EHCI split transactions (pad behind an Intel 5/6-series built-in hub) on real hardware.
3. User (beta.48): SC55J16.BAT + Jazz Jackrabbit on the D810 (sound gaps seen only in QEMU HDA?, joystick calibration,
   key response). Possible: SBEMU16 local 61h/DMA-count handling to cut real-mode trips; other 16-bit DPMI games.
5. User (beta.51): NEC card again with USBTEST (OHCI fix below); Xbox One / HID pads on real hardware; INT 15h joystick
   game; mouse in the menus with CTMOUSE; 3D menu (F8, F4 speed) on real VESA cards (D810); themed test programs.
   Open: TIMETEST reports "No MPU-401 answered" with SBEMU loaded (86Box; MPUTEST finds it) - predates beta.50.
   Voodoo/Glide 3D style dropped for now (user: not needed).
4. Older: Automatic setup, MIDPLAY S panel, Ctrl+Alt+F12, MT-32 game, GAMETEST Duke run; SBDIAG if music is choppy.

## Files
- sbemu/vdiag.c (diagnostics, key player, overlay), sbemu/vmpu_sc55.c (MPU, panel 0xB0), sbemu/vmpu_mt32.cpp (Munt), sc55/sc55synth.cpp (SC55_GetLCD), main.c (/VMT32)
- tools/: sc55menu.c, menu3d.h (3D menu), sbset.c, gametest.c, midplay.c, cdgui.c, sbdiag.c, tui.h, toolui.h (test-program screens), theme.h, fontdata.h, safewr.h
- third_party/mt32emu (Munt 2.8.3), upstream/Nuked-SC55
- dosimg/ (test rig): mkdisk.py (FreeDOS FAT16 image), mkiso.py (ISO9660 + Duke IDF), vm.py (QEMU driver); QEMU built in work/qemu/b7 (configure: --python=/usr/bin/python3.12, tests/fp skipped, dtc/keycodemapdb from GitHub mirrors)
- USB: sbemu/usbpad.c (polled UHCI/OHCI/EHCI/xHCI host, hubs, pipes for 2 pads + keyboards + mice), sbemu/vjoy.c (port 201h,
  2-pad mapping, pad keys), sbemu/vkbc.c (8042 injection D2/D3, HID->set 1, PS/2 mouse emulation on trapped 60h/64h),
  main.c (/JOY*, /USBKB, /USBMS), tools/joytest.c, usbtest.c, kmtest.c, sbset.c (Joystick page), sc55menu.c;
  testrig/fakepad.py (usbredir pad), joyrun.sh, joyrm.asm. Test scripts in work/joyvm: multi.sh (2 pads + usb-kbd/mouse,
  LATE=1 adds kbd/mouse after POST so SeaBIOS doesn't serve INT 15h C2 itself), qmp.py (keys/mouse via QMP)
- 16-bit PM: tools/sbemu16.asm (NASM .COM, HDPMI16 trap client -> real-mode in/out; 201h answered from vjoy's info
  port 206h with a per-read virtual clock; saves FS/GS), third_party/hdpmi16 (HDPMI16I patch + build.sh, crazii/HX
  43182b4, JWasm/JWlink built from GitHub in /home/claude/dl), SETUP makes SC55J16.BAT. testrig/sbt.asm (SB DMA+IRQ
  test, 'SBT P' from 16-bit DPMI), joy16.asm. Jazz VM: /home/claude/games/jvm (starthda.sh = HDA + wav capture).
- 86Box rig: testrig/86box (regress.py = full regression run, README.md; build in /home/claude/86box/86box, ROMs /home/claude/86box/roms, VMs
  /home/claude/86vm/{p2b,u1,u2}); emulated 360 pad usb_xpad.c with hot-plug via automation.
- Build: DJGPP gcc 12.2 from github.com/andrewwutw/build-djgpp v3.4 (djgpp-linux64-gcc1220.tar.bz2);
  `export PATH=<djgpp>/bin:$PATH; make -j8` (delete output/*.o after header changes)

## Resume prompt
"Continue NUKED SB-EMU from sc55-pack/HANDOFF.md (beta.51-usb): github.com/deon81997/NUKED-SBEMU-MUXED, branch usb-xpad. Rebuild with DJGPP, then continue from Next steps."

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

## 2026-11-07 (beta.45-usb): USB keyboard, USB mouse, two pads
- usbpad.c reworked: every controller runs all the time with its own schedule (UHCI: FL -> 6 pipe QHs -> control QH;
  OHCI: HCCA -> 6 pipe EDs; EHCI: periodic list relinked per pipe; xHCI: per-pipe rings); device table across
  controllers; root ports enumerated one at a time (state machine), hubs checked one port per tick with 100 ms debounce.
- HID boot keyboards / mice (SET_PROTOCOL boot, SET_IDLE 0, LEDs via SET_REPORT); composite receivers give one pipe each.
- vkbc.c: keys as set 1 via 8042 D2 (repeat 500/33 ms), mouse as PS/2 via D3 with command emulation when 60h/64h trapped
  (/USBMS auto = USB mouse present at start). No injection from inside the port 60h read trap: QPIEMU re-reads the port
  after the handler and swallowed the injected byte (lost keys in QEMU).
- EHCI reclaim sets Port Owner directly (toggling CONFIGFLAG unplugged everything on the companions in QEMU).
- SET_ADDRESS settle 20 ms + 4 tries of the device descriptor (NEC OHCI log: pad silent after 5 ms).
- QEMU: 2 pads + kbd + mouse on UHCI+hub, OHCI, NEC EHCI+2 OHCI, ICH EHCI+UHCI, xHCI(+hub); kbd hot-replug; SBEMU +
  CTMOUSE + KMTEST (keys incl. shifted, mouse move + click), JOYTEST two-pad mapping.
- Release zip: release/NUKED-SB-EMU-1.0.0-beta.45-usb.zip.

## 2026-11-07 (beta.46-usb): stick types (/JOYMODE)
- vjoy.c: 1 CH Flightstick Pro (hat = button-line combos F/B/7/3), 2 Thrustmaster FCS (hat on axis 4: 0, 1/4, 1/2, 3/4,
  full), 3 Gravis GamePad Pro GrIP (24-bit frame, clock steps per 201h read, pads on lines 4/5 and 6/7), 4 native (no
  201h trap). From Linux analog.c / grip.c. JOYTEST /GRIP decodes like grip.c. SBSET "Stick type" + flight Sticks preset.
- QEMU checked: CH / FCS hat directions + button in live JOYTEST; GrIP all buttons on 2 pads (JOYTEST /GRIP /LOG).
- Note: JOYTEST /LOG with a pad plugged in after JOYTEST starts logs -1 in QEMU (live view is fine) - harness quirk.

## 2026-11-07 (beta.47-usb): D810 regression fixes (beta.45/46: inputs stop after moments in SBEMU, USBTEST crash)
- UHCI: poll_in waits for the QH element to become T before re-arming (the HC writes TD status first, QH a moment
  later; re-arming in between got the new TD overwritten -> pipe dead). Only on real hardware.
- UHCI controllers run only while they have a device (uhci_run on pick, off when own==0), as beta.44 did; beta.45 ran
  all four ICH6 UHCIs from the start (suspected cause of the USBTEST crash/reboot on the D810).
- Watchdog: a running UHCI halted by an error (STS 0x38) is traced and restarted. "no reports" diag now = no pad input.
- Next: user retests USBTEST + SBEMU/JOYTEST on the D810.

## 2026-11-07: real game test - Descent 1 (user's D-Fend ISO, registered 1.5) in QEMU with SBEMU
- /JOYMODE:1 + Descent "CH Flightstick Pro": detected, 3-step calibration with pad A, customize screen reads
  HAT up/right + CH buttons, in flight: stick turns, A fires (energy drops), hat slides. Works.
- /JOYMODE:2 + Descent "Thrustmaster FCS": detected, calibrated, customize screen reads HAT up/down/left. Works.
- Descent II demo (user's d2demo10.zip): no GrIP (supports CH/TM/Gravis analog/SideWinder) - not needed for coverage.
- Harness: testrig/fakepad.py "live:FILE" script = pad state read from FILE whenever it changes (manual driving);
  game VM scripts in work: /home/claude/games/dvm (start.sh, m.sh shot|key|pad|cmd). QEMU needs -device AC97 for SBEMU.

## 2026-11-07 (beta.48-usb): 16-bit protected-mode games (SBEMU16 + HDPMI16I), tested with Jazz Jackrabbit in QEMU
- Jazz (Borland 16PMODE) needs HDPMI16; only a crazii-fork HDPMI16I coexists with the pack's HDPMI32I (built from source).
  CD Jazz FILE0001.EXE needs the Turbo Pascal RTE 200 patch (offset 223467 F7D0F7D2B93700F7F1 -> B8FFFF909090909090).
- SBEMU16 traps 00-21,42-43,61,81-8F,A0-DF,220-22F,201,330-38B (port 60h must NOT be trapped: QPIEMU re-reads it).
- Results: SB Pro detected, music + SFX play (HDA capture; AC97 in QEMU gives silence even for real-mode tests), joystick
  detected/calibrated/moves menus, all keys register with 400 ms holds (100 ms taps drop: QEMU frame time). Silence seen
  earlier was the VM's CONFIG.000 with volume 1 (mixer multiplier [928]=1), not an emulation bug.

## 2026-11-08 (beta.49-usb): 86Box testing (real chipset emulation) + Pentium-class CPU fix
- 86Box built from source with automation + a local emulated Xbox 360 pad (testrig/86box). PCem not used (no PCI sound,
  no USB). Jazz + SBEMU16 on P2B-LS/ES1371: continuous music (QEMU's gaps were QEMU's HDA).
- USB on 86Box: PIIX4E UHCI (P2B-LS) and OPTi 82C861 OHCI card (P/I-P55TP4XE, Pentium 133): 2 pads enumerate,
  reports, hot-unplug/replug OK; SBEMU 2-pad mapping OK; /JOYMODE 1 CH hat F/B/7/3, 2 FCS axis-4 0/.25/.5/.75/full,
  3 GrIP both pads all decoded by JOYTEST.
- BUG FIXED: SBEMU crashed (SIGILL, CMOV) on any CPU without CMOV (Pentium/MMX, K5/K6, Cyrix): Nuked OPL3 was built
  with the SC-55's -march=i686. Now OPL_CFLAGS i586; SC-55/MT-32 (i686 cores) refused with a message when CPUID has
  no CMOV (vmpu_sc55.c), SBSET automatic setup likewise. README notes the SC-55/MT-32 CPU minimum.
- /JOYMODE and /JOYDZ now apply to an already loaded SBEMU (VJOY_SetLive; not to/from 4). VMPU warning uses CR LF.
- Not covered by 86Box: EHCI/xHCI, CardBus; USB keyboard/mouse through SBEMU not yet run there.

## 2026-11-08 (beta.50): upstream updates
- SBEMU (crazii) since cbdbb7c: CS4281 init/AC97/clock fixes, ES1370 UART/clock fixes, AK4531 routing bits,
  SB Live! MPU-401 passthrough (8db907d, 2d1cd2a, 9aa26dc, 3963f4a). Nuked-OPL3-fast 1.8-fast.2 -> 1.8-fast.3.
  Munt libmt32emu 2.8.3 -> 2.8.5 (MidiStreamParser sysex overflow fix). jcmoyer Nuked-SC55 0.7.0 -> 66d1272
  (compile fixes only). SHSUCDX built from adoxa/shsucd 5ea0787 (Associated Files fix; NASM, 3.09 source
  rebuild matched the shipped binary byte for byte). uCDD 0.9.5-a3 only changes the resident-audio DPMI host:
  the pack's mount-only UCDD.EXE stays a2. Jemm 5.87pre1 skipped (pre-release). Win98 part already used the
  latest Munt and Nuked-SC55.
- opl3.h changed size of opl3_chip: a stale output/sbemu/opl3emu.o crashed programs after FM output.
  makefile now rebuilds every object when any project header changes.

## 2026-11-08 (beta.50-usb): optional 3D menu, test programs in the menu style
- SC55MENU 3D menu (tools/menu3d.h): software renderer, VESA 640x480 (32/16/15/24 bpp, LFB or banked), styles Panels,
  Carousel, Module (F3). F8 / Settings > 3D menu; MENU3D.CFG makes MENU start in 3D; Shift = text; no VESA -> text + note.
  Tested in 86Box (P2B-LS, S3 ViRGE).
- tools/toolui.h on tui.h: header band, panels, lamps, stick/mouse plots, log area, key bar; style/font/display mode
  from THEME.CFG, Shift safe mode, F4. JOYTEST (axes + dot per stick, G = GrIP view), KMTEST, MPUTEST (steps, piano
  keyboard, drum lamps, Enter replays; menu no longer pauses after it), USBTEST (pads, USB keys/mouse, result, last
  step), TIMETEST (phases, log, result) now full-screen; redirected output (not a tty) keeps the old plain text.
  MIDPLAY uses the MENU style palette and font. SBDIAG stays a report (viewed in MENU). All checked in 86Box.
- TIMETEST "No MPU-401" in 86Box (SBEMU without SC-55 ROMs, upstream MPU handler): not fixed. Findings: MPUTEST's exact
  detect() code fails when linked into TIMETEST and succeeds in MPUTEST and in small test programs; small code changes
  (an extra store, a delay) flip it. EAX..EBP are preserved across traps (checked). Suspect the HDPMI port-trap path
  (I2FHDPMI.ASM InOutExcHandler) or SBEMU's trap state; needs SBEMU /MDBG serial log or an SC-55 ROM set in the VM.

## 2026-11-08 (beta.51-usb): NEC OHCI fix, regression run, 3D speed, mouse, more pads, BIOS joystick, SBDIAG screen
- USBTEST from the user's NEC card (uPD720101, 3.2 GHz): wired 360 pad "desc18!" (DNR right after SET_ADDRESS) and the
  Logitech receiver dropping out. Cause found: o_run_tds / poll_in treated a TD's error code as final while the HC
  still retried it (ErrorCount < 3, ED not halted) and pulled the TD. Now only a halted ED is an error. Also: re-reset
  before SET_ADDRESS on later tries, SET_ADDRESS resent if the device stayed at address 0 (86Box USB_IGNADDR hook),
  OHCI UnrecoverableError / not-operational -> reset + re-enumerate, "gone" trace shows PortStatus / IntStatus.
- testrig/86box/regress.py: 27 checks on 2 VMs (P2-400 UHCI+OHCI, P133 OHCI), ~10 min; keys 80 ms apart.
- First-run auto-setup offer comes before the 3D menu. 3D: 16-px fixed-point spans, 4 floor quads, cached panel,
  half resolution (auto <15 fps), F4 speed, fps note. 86Box P2-400: 7-8 fps full, 15-16 half; P133 2-3 / 5-6.
- Mouse (INT 33h) in tui.h (pointer drawn by tui_flush, K_MOUSE / K_RCLICK / wheel) and the 3D menu (hit regions).
- usbpad.c: Xbox One (GIP: power-on + S-init, guide packet ack) and HID gamepads (report descriptor parser: X Y Z Rx Ry
  Rz, hat, buttons 1-13). 86Box usb_xpad XPAD_KIND=one|hid. INT 15h AH=84h via a real-mode stub (/JOYBIOS, default on;
  testrig/j15). SBDIAG full screen (SBDIAG DEMO). TIMETEST MPU detection still open (see above).

## beta.52-usb (2026-11-08): main beta.51 (D810 Win98 IRQ-0 freeze fix, CDPLAY) + usb-xpad beta.51 merged
- D810 froze in SC55.BAT only after Windows 98 -> Restart in MS-DOS mode (cold DOS boot fine). SBDETAIL: AC'97 00:1e.2
  had IRQ line 0, cmd 0000, BAR0/1 = 1 (Windows switched the chip off). AU_init assigns BARs (0xF000 hack) but took
  IRQ line 0 as the card IRQ -> handler on the timer vector -> freeze at "installing the sound card interrupt handlers".
- main.c: IRQ 0/1/2/8/13 (and >15) are invalid -> pcibios_AssignIRQ (PCI BIOS routing). pcibios.c: a linked device's
  line of 0 etc. is not taken as a shared IRQ. SBDETAIL logs the card's PCI cmd/BARs/IRQ after init and USB controllers.
- Reproduced in 86Box with PCIZAP (line register -> 0): beta.50 hangs, beta.51 routes IRQ 11; SBQ (8-bit DMA through
  SBEMU, DMA count must move) passes zapped and normal.

## beta.53-usb (2026-11-08): no sound on the D810 after Windows 98 -> MS-DOS mode
- beta.52 no longer froze there but was silent. sc_ich.c (SBEMU): PCI power state back to D0 (PM capability),
  always a full AC'97 cold reset (upstream did only a warm reset when the link was already out of cold reset, which
  keeps the codec registers Windows left: DAC/amp powered down, EAPD), codec register reset (00h) + power-up (26h = 0,
  wait for the ready bits). ICH_Diag -> SBDETAIL "ICH AC'97 set-up:" (cmd, BARs, PM state, GLOB_CNT/STAT, codec
  power/volume before, codec id and power after).
- QEMU AC97 rig (rig98/t53.img, /tmp/qrun.sh, ACZAP = Windows-like state: 26h=FF00, master mute, cmd 0, BARs 0, IRQ 0):
  plays after ACZAP (QEMU ignores the codec power-down bits, so the D810 is the real test).

## beta.54-usb (2026-11-08): D810 after Windows 98 still silent and the USB pad dead in JOYTEST (USBTEST fine)
- USBTEST polls USB itself; SBEMU polls it from the sound card interrupt (VJOY_Tick in MAIN_Interrupt; port 201h
  reads only do a light poll, no enumeration/recovery). Silence + dead pad = card interrupts not arriving.
- main.c: card IRQ set to level mode in the ELCR (4D0h/4D1h) - an IRQ routed after Windows may be edge mode;
  SBDETAIL logs ELCR before/after, ICH PIRQ A-H routing (LPC 00:1f.0 60h/68h), and "Sound card interrupts so far"
  (MAIN_CardIrqs, waited up to 0.5 s before going resident); zero -> red warning on screen.
- Neither QEMU (AC97) nor 86Box (ES1371) loses interrupts with ELCR cleared, so not reproduced; waiting for the D810's
  SBDETAIL. 86Box has no Intel ICH AC'97; QEMU's AC97 (82801AA) is the only emulated ICH sound for sc_ich.c tests.

## beta.55-usb (2026-11-08): NEC uPD720101 OHCI + wired 360 pad (P5PE-VM USBTEST)
- Log: desc8 (try 2), SET_ADDRESS accepted, then DNR at the new address; still answers at 0, every try incl. re-reset
  and resent SET_ADDRESS. Logitech receiver on the other OHCI fine.
- usbpad.c enumerate(): OHCI root ports now use the Windows/Linux order: GET_DESCRIPTOR(64) with ep0 MPS 64 (full
  speed; first short packet ends it), second port reset, then SET_ADDRESS. UHCI / EHCI / hubs unchanged.
- QEMU pci-ohci + usb-kbd + fakepad: both enumerate, pad input read (fakepad.log shows len=64 first).

## beta.56-usb (2026-11-08): NEC OHCI still "still at 0" with the Windows order (beta.55 USBTEST, P5PE-VM, E5800)
- First request did go out with MPS 64 (ed00400000), then the same pattern. The ED trace can't show what the HC used.
- Theory: the NEC caches the shared control ED, so the in-place FA change after SET_ADDRESS isn't seen (requests to
  "address 1" still go to the old one; the "still at 0" probe then hits the real address 1, and the resent
  SET_ADDRESS fails). o_run_tds: when FA/EN/MPS/speed change, clear CLE, wait 2 frame starts, ControlCurrentED = 0,
  rewrite the ED, set CLE again (OHCI 5.2.7). QEMU OHCI + kbd + fakepad still fine.

## beta.57-usb (2026-11-08): NEC experiments per try (user can't test right now; one USBTEST run tells more)
- beta.56 (ED re-read with CLE off) did not help: same "still at 0". Official Microsoft wired 360 pad.
- enumerate() on OHCI root ports, by earlier failed tries of the port: try 1 Windows order (beta.55); try 2 a second
  control ED (ctl+8, linked after the main one, only for addresses != 0, kept for the whole enumeration); try 3 also
  SET_ADDRESS split (SETUP run, 2 ms, status run). Trace: "[tryN: 2nd ED split]", "sa[TD0 TD1]" (TD dword 0 after
  SET_ADDRESS), "@2ms new:yes/no old:yes/no" (GET_STATUS at the new address, then at 0), "setaddr!" / "setaddr2!".
- QEMU: tries 2/3 forced (-DOFORCE=n) enumerate kbd + fakepad; the split try aborts QEMU with usb-redir (QEMU limit),
  fine with usb-kbd.
- Asked the user for: pad and Logitech receiver swapped on the NEC card; pad on the P5PE-VM's own ports.

## beta.58-usb (2026-11-08): CD-Audio (HYBRID /VCDA) actually plays now
- Found in 86Box (ES1371, IRQ 10, TEST.CUE): the image was read with DOS calls inside the card ISR before EOI; a
  BIOS disk read waiting for IRQ 14 (lower priority than IRQ 10 on the slave PIC) hung the PC; at the prompt
  (InDOS 1) reads never ran, the open gave up after 600 ticks. Nothing had ever been mixed.
- cdlink.c: Mix (ISR) only consumes the ring and handles commands; CDLINK_EngineService does open/close/refill with
  interrupts on (shared fields under irq_save/irq_restore, session counter drops stale work). main.c: MAIN_CDService
  after the card's EOI (card IRQ masked, STI, 4 sectors, InDOS 0) and an INT 28h hook (InDOS 1, 16 sectors).
  Host tests use inline I/O (CDLINK_EngineSetInlineIO default 1): tests/cdlink_test.c passes.
- Mailbox flags bits for diagnostics: 02 service wanted, 04 DOS free, 08 open, 10 open failed, 20 read, 40 mixed,
  80 read error. /tmp/t/cdq.c "CDQ M B" prints the box.
- 86Box: while a program runs (CDQ busy-wait) tracks 2/3/4 come out both/left/right in order, pos advances at real
  time; at the prompt CDPLAY 3 = left only, CDPLAY 2/4 run to the end (pos, flag 40; the WAV capture missed some).
- CDPLAY.COM play-range patch (EDX clobber) shipped since beta.51.

## beta.59-usb (2026-11-08): P5PE-VM + NEC: beta.57/58 USBTEST found the pad and receiver but no input, and the PS/2
## keyboard went dead inside USBTEST (Esc did nothing, restart needed). Log not received yet.
- ohci_reset: OwnershipChange interrupt enabled before OCR (Linux pci-quirks does this; some BIOSes need it for the
  SMI), trace "BIOS handoff ok/FAILED"; legacy keyboard emulation (HcRevision bit 8 -> HceControl 100h) switched off
  if left on, trace "keyboard emulation xxx off" - a left-on emulation traps ports 60h/64h.
- USBTEST: Esc also read straight from the KBC (port 60h scancode 01h) and closes by itself after 2 minutes with no
  key, so a dead keyboard no longer forces a restart.

## beta.60-usb (2026-11-08): beta.58 USBTEST log from the P5PE-VM NEC card
- try 3 (second ED + split SET_ADDRESS) got the official wired 360 pad addressed and enumerated: SETUP and status
  stage queued together never moved it off address 0, 2 ms apart works. (Logitech receiver fine either way.)
- But "reports 0", same as on the D810 UHCI; and the PS/2 keyboard was dead in USBTEST (beta.59: HceControl off).
- 360 wired pads now get Linux xpad_start_xbox_360's vendor read (C1h/01h/0100h, 20 bytes) before the LED command;
  trace "init" / "init-stall". QEMU fakepad answers it and still reports.
- TODO if the NEC still shows no reports: periodic EDs are rewritten in place (start_in); the NEC may cache them -
  alternate two ED slots per pipe or toggle PLE around the change.

## beta.61-usb (2026-11-08): split SET_ADDRESS first on real OHCIs
- beta.60 SBEMU on the P5PE-VM: "nothing connected yet" - the pad needed try 3 (several seconds of failing tries),
  past SBEMU's 3 s window. Now try 1 on an OHCI = second ED + split SET_ADDRESS, try 2 plain, try 3 Windows order.
  QEMU's OHCI (vendor 106B) keeps the old order (split + usb-redir aborts QEMU).
- Still waiting for a beta.60+ USBTEST.TXT from the NEC (init / handoff / emulation traces, reports).

## beta.62-usb (2026-11-08): beta.61 USBTEST on the NEC: pad on try 1 (split), "keyboard emulation 1ff off" (it
## WAS left on by the BIOS), init-stall (official pad doesn't need it), but reports 1 then nothing; Logitech nothing.
- One TD per IN pipe completes, the tail-appended next one apparently never does. USBTEST now dumps every IN pipe
  (USBPAD_Dump: ED info/tail/head, both TDs, EMPTY/HALTED/waiting) at 5 s and 20 s and on D; R restarts every pipe
  with a full ED rewrite (USBPAD_Rearm -> start_in). Next step depends on that dump.

## beta.63-usb (2026-11-08): the NEC drops tail-appended interrupt TDs - found with the beta.62 dump
- P5PE-VM USBTEST 5 s dump, all three pipes: ED "EMPTY" (head == tail) while our newly armed TD (CC F) sits in the
  other slot, outside head..tail; one report per pipe, then nothing. The HC ran off a stale copy of the old empty
  tail TD (or ED) and never saw the append.
- poll_in (OHCI): our TD still pending + ED empty + tail != our TD -> trace "[OHCI n lost a TD: ED rewrite per
  transfer from now]", HC[h].oslow = 1, re-arm that slot. arm_in with oslow: ED skipped, wait for a frame start
  (<= 1.2 ms), head AND tail rewritten to the new TD / new empty tail (toggle carry kept), unskipped.
- QEMU with oslow forced (-DOSLOWFORCE): all fakepad changes arrive (reports 21); normal build unchanged.
- SBEMU on the NEC (beta.62 SBDETAIL) found pad 1, keyboard and mouse at start-up (split SET_ADDRESS first).

## beta.64-usb (2026-11-08): beta.63 USBTEST on the NEC works fully (360 pad, 0F30:010B HID pad, Logitech keys and
## mouse, "[OHCI n lost a TD: ED rewrite per transfer from now]"), but SBEMU hangs in its /JOY step (SBDETAIL ends at
## "[step] USB joystick / keyboard / mouse"). beta.62 SBEMU got past it (no data flowed then).
- Not reproduced: QEMU AC97 + pci-ohci + usb-kbd/usb-mouse/fakepad with oslow forced, SBEMU loads.
- SBEMU now streams the USB trace into SBDETAIL during USBPAD_Init ("USB live: ...", USBPAD_SetTraceHook), so the
  next hang shows the last USB step. Suspects: VKBC callbacks with real data (first time on real HW), LED SET_REPORT,
  the HID pad on low speed, the sound ISR (SC-55) running alongside.

## beta.65-usb (2026-11-08): beta.64 SBDETAIL from the P5PE-VM: USBPAD_Init finished (all found, both OHCIs went
## oslow), the hang is after it, before "Joystick mode" is logged.
- In that stretch: 201h trap, INT 15h hook (JOYBIOS), KBC 60h/64h traps, and MAIN_JOY_On (USB polled + VKBC inject
  from the sound ISR, with real data for the first time). Now each is a MAIN_Step, MAIN_JOY_On is set last, and
  1 s of ISR polling runs before going on ("USB: 1 s of polling went fine"). QEMU: all steps pass.

## beta.66-usb (2026-11-08): the P5PE-VM lock = oslow waiting inside the sound ISR
- beta.65 SBDETAIL: last step "USB: polling from the sound interrupt on", the 1 s check never ended. arm_in (oslow)
  busy-waited up to 1.2 ms for a frame start per report; in SBEMU's ISR (SC-55 running, 360 pad reporting
  continuously) the ISR never got out.
- arm_in oslow is now two-stage and never waits: stage 1 skips the ED and notes the frame (q->opend, q->ofn);
  poll_in does stage 2 (head + tail rewrite, unskip) once 2 frames have started. start_in arms synchronously
  (ArmSync; it has skipped + waited itself). QEMU with oslow forced: USBTEST all reports, SBEMU passes the 1 s check.

## beta.67-usb (2026-11-08): EMM386 error 06 on the P5PE-VM = USBTEST run while SBEMU (beta.66) was resident
- Both drive the same OHCI controllers (SBEMU from its sound ISR); USBTEST resets them and frees its schedule memory
  under SBEMU's feet. beta.66 SBEMU itself loaded there (it got that far).
- USBTEST refuses to start when an MPU-401 answers (mpu_find: SBEMU), /F overrides; README note. 86Box: runs before
  SC55.BAT, refuses after.

## beta.68-usb (2026-11-09): Dark Forces IMUSE.EXE (setup) "freezes" with the SC-55
- Repro (86Box, Celeron 366 and 533): General MIDI, port 330, Test Music -> never shows "Testing music"; a key
  press then halts DOS with "Internal stack overflow" (8 nested BIOS INT 09 frames at F000:ADFC, DOS STACKS full).
- Not the cause (each tested): /VTIMER:0, /JOY:0, PC-speaker traps, STACKS size, HDPMI (HDPMI alone + 86Box MPU
  works), MPU semantics (SBEMU loaded with /P:300 + 86Box's own MPU at 330 works), /VSCVOICES:4.
- Cause: once the SC-55 plays, the sound ISR took 98% of the CPU (on-screen gauge: 257 irq/s, up to 5.4 ms each;
  idle 1%). One 1 ms render slice cost ~4 ms there; the FIFO's fixed 2-3 ms per-interrupt budget always allowed
  it, so the game starved: iMUSE sent ~3 MIDI bytes/s, and its BIOS keyboard handler (EOI then STI) nested.
- Fix (vmpu_sc55.c VMPU_GenSamples): CPU credit - each interrupt earns 60% of the time since the previous one,
  each slice spends its cost, slices shrink to the credit left, none when it is used up. /VSCAUTO:0 disables it.
  After: Test Music runs to the end and returns, keys work. Fast PCs never reach the ceiling.
- Debug aids used (not in the build): /tmp/df-debug.patch (DOS STACKS probe, MPU access ring, ISR load gauge).

## beta.69-usb (2026-11-09): SC-55 core ~20% faster, bit-exact
- upstream/Nuked-SC55/src/backend (our fork), all output-identical (host bench FNV hash of every sample, mk1-v1.00,
  mk1-v2.00, mk2-v1.01; FX on/off; voice limit 0/16; 60 s two-file run hash 14e29e60):
  - mcu_timer: each FRT/TMR keeps `due` = next tick that changes state (overflow/compare/IRQ request); a tick whose
    request is already pending counts as quiet. TIMER_Clock only steps timers that are due (TIMER_AdvanceSel);
    MCU_Step skips the call entirely until next_due.
  - pcm: wave ROM bank table (rom_base/rom_mask, rebuilt when cfg 0x20 changes) replaces PCM_ReadROM's
    branches for the 5 sample reads per voice (PCM_ReadROMFast).
  - mcu: MCU_Step calls PCM_Update / MCU_UpdateAnalog only when they have work.
- Render path (callgrind, 32-bit -march=i686, 4 s of 19.mid): 6.34G -> 5.13G instructions (-19%).
  Host: mk1 +7..45%, mk2 +10..18%. 86Box Celeron 366 SC55WAV (32 kHz): 0.28x -> 0.35x real time (+24%).
- Dark Forces setup Test Music rechecked (86Box): runs closer to real time (bar 16 s -> ~6 s, music no longer
  lags the game).
- Remaining profile: PCM_Update 45% (20-bit DSP arithmetic, inherent), timers 14%, MCU_Step 11%, MCU_Read 7%.
- Bench: /home/claude/bench (bench.cpp, build.sh, ir.sh; SC_SET=<romset> env picks the ROM set).

## beta.70-usb (2026-11-09): bug sweep (86Box, Celeron 366)
- Checked OK: SBEMU start-up, MPUTEST, TIMETEST (now finds the MPU with the SC-55 loaded; 0 ticks lost), SBDIAG,
  MIDPLAY play/seek/views/browser, MENU (all pages, launching a game and coming back), CDGUI mount, CD audio
  (CDT.BAT: track plays to the end), GAMETEST, JOYTEST, Dark Forces setup Test Music.
- Fixed:
  - MIDPLAY: after the file browser (B) or playlist (Tab) the title bar, column headers, "Notes" row and key bar
    stayed blank (ui_flush compares with `shown`, which was not reset after the list screen cleared the screen).
    modal_end() and ui_begin() now invalidate `shown`.
  - MENU (3D): four descriptions ran past the 4-line box ("...") - shortened (checker: /tmp/desc.py, 74 cols x 4).
    Scroll arrows in the item list when items are hidden (Settings has 8, the box shows 7).
  - SBSET Automatic setup: skips the mkII when the SC-55 without effects already needs over 50% (the mkII is
    always heavier, it could never be picked) - saves a full init + 2 measurements on slow PCs. "About a minute"
    text corrected. The "a SoundFont would suit this PC better" warning overflowed the dialog - two lines now.
  - TIMETEST: SBEMU statistics line wrapped mid-word - fits in two lines now.
- Not a bug (checked): Automatic setup leaves /VSCAUTO on (value index 0 = "1").

## beta.71-usb (2026-11-09): CUE images can be unmounted
- Repro (86Box): CDIMG TEST (CUE with audio) -> UCDD -unmount: "The drive is locked or in use." right after the mount,
  nothing open; ISO fine. Clean uCDD a2/a3 mount-only builds from source unmount fine, but have no SBCDA1 mailbox,
  so SBEMU /VCDA finds nothing ("no SBCDA1 box", PLAY AUDIO 8103).
- The pack's UCDD.EXE (40757 bytes, from the user's original pack; patch source not found anywhere) sets the unit's
  AUDIO_ENTRY [si+16h] to its mailbox handler on mounting an image with audio, and clears it (plus box state=0,
  cmd=4 stop, gen++) in clear_unit - but kept the non-resident build's guards `cmp dword [si+16h],0 / jnz` in the
  control function (op 2 -> 8002h) and in the IOCTL eject. scripts/ucdd-unmount-fix.py NOPs both (payload 3C1h,
  6D2h).
- 86Box with SBEMU /VCDA: TEST.CUE track 2 plays, CDIMG /U mid-track (box state 0 cmd 4, music stops), CDIMG DUKE3D,
  track 2 plays again; CUE/CUE/ISO mount-unmount cycles all fine.
- GPL note: uCDD is GPL-3.0; the SBCDA1 mailbox patch's source is not in this repo - should be recreated against
  vorvek/uCDD source (a3) at some point.
- Also checked (QEMU 8.2, FreeDOS + JEMMEX NOEMS + HDPMI32I + SBEMU /JOY:1, fakepad lx=+max ly=-max rx=-max A):
  - INT 15h AH=84h (/JOYBIOS): J15 -> vector 0000:CF2C (hooked), DX=0: CF=0 AL=E0 (button 1 down), DX=1: AX=00FF
    BX=00FF CX=0000 DX=0080 - matches the pad. QEMU's mkdisk needs testrig/rufus (sparse clone of pbatard/rufus
    src/ms-sys/inc; not committed).
  - Pad behind hubs, J15 values correct on: UHCI+hub, OHCI+hub, xHCI+hub, ICH9 EHCI+UHCI companions with a hub on
    the EHCI port (QEMU's FS hub goes to a companion), UHCI with two hubs deep.
  - Not testable in QEMU/86Box: FS pad behind a HS (USB 2.0) hub on EHCI = split transactions. Reviewed against
    EHCI 1.0: QH C flag on FS/LS control, Mult 1, hub addr/port from the nearest HS hub (ports 1-based), async split
    S-mask 0, periodic S-mask 01h / C-mask 1Ch (Linux's pattern), overlay split state zeroed, hub port speed bits
    9/10. Looks right; needs a real USB 2.0 hub (USBTEST shows the TT hub/port).

## beta.72-usb (2026-11-09): F3 style everywhere, MIDPLAY 3D view
- F3 = next style in every program, saved in THEME.CFG for all: MIDPLAY (own key handler, case 0x13D), SBSET
  (getkey() wrapped as sb_getkey: F3 -> theme, returns 0 = redraw), CDGUI (had it), GAMETEST, and the toolui.h
  programs (JOYTEST, KMTEST, MPUTEST, USBTEST, TIMETEST, SBDIAG) through tu_poll(). tui.h: tui_theme_next().
  Safe mode (Shift) ignores it. Checked in 86Box: MIDPLAY Candy->Midnight->Groovy, SETTINGS, JOYTEST.
- tools/m3core.h: the 640x480 CPU 3D core split out of menu3d.h (VBE, timing, colours, panels, text, rasterizer,
  boxes, knob, sky, mirror floor); menu3d.h keeps the menu (scene data, cards, styles, loop). fontdata.h got an
  include guard. MENU's Panels/Carousel/Module rechecked in 86Box.
- tools/midplay3d.h: MIDPLAY's third view (S: table -> panel -> 3D, MIDPLAY.CFG VIEW=0..2). Original NUKED
  SB-EMU module: LCD with the live SBEMU panel (part, instrument, 16x16 level dots; own meters for MT-32/SF2),
  position knob + ring, a 63-white-key keyboard texture lit per note in its channel colour (g_viz.nl/ncol), 16
  channel columns on the floor in front. <=16 fps; frame > 25 ms -> half resolution, still > 40 ms -> "slow PC"
  note (MIDI events are sent from the same loop). F4 toggles half/full. TAB/B lists switch to text and back
  (modal_begin/modal_end); no VESA -> table view. Makefile: midplay links -lm and depends on tools/*.h.
- 86Box Celeron 366: works, half resolution, shows the slow-PC note (expected).

## beta.73-usb (2026-11-09): faster start-up, two bug fixes
- 86Box Celeron 366, SC55.BAT-style start (/VMPU, mk1 ROMs): 15.1 s -> 6.9 s; plain batch start 15.4 -> 6.1 s.
- ROMHASH.CAC kept missing: DJGPP stat() returned different st_mtime for the same file depending on the start
  (Nov 7 23:30:08 vs Dec 14 03:06:34 seen), so every ROM was SHA-256'd again (~6 s). sc55synth.cpp file_stamp()
  now uses the raw FAT date/time from the handle (_dos_getftime) on DJGPP; stat() elsewhere. Old caches get
  rewritten once (stamp format changed).
- rom_io.cpp unscramble(): lookup tables for the address/data bit shuffles (810M -> 50M instructions, hashes same).
- Firmware boot default 4 -> 3 emulated s (sc55synth.cpp; measured ready: mk1 ~2.3 s, mkII ~2 s).
- SBDETAIL.TXT: the disk commit (fsync, ~25 ms) now only at [step] lines and USB trace lines; other lines are
  only fflush'd (option list alone was 1.9 s). New "Start-up time per step" table at the end (TSC-based).
- MAIN_CPrintf: bare LF inside a message now becomes CR+LF (USB "Not found? Run USBTEST" line started in the
  column where the previous line ended); no more buf[-1] read for messages under 2 chars.
- TIMETEST "No MPU-401" without ROMs: not reproducible; with /VSCROM:C:\EMPTY SBEMU warns, keeps the MPU-401
  (silent) and TIMETEST runs normally.
- Remaining start-up: ~3.9 s SC-55 init (firmware boot = MCU+PCM emulation, PCM_Update 43%), ~1 s USB poll check.
  A boot snapshot would remove most of the 3.9 s (needs mcu/submcu/timer/lcd/pcm/engine state serialised).

## beta.74-usb (2026-11-09): Windows 98 part catches up (first update since beta.38)
- SC55W.EXE built against our Nuked-SC55 fork (beta.69 speed-ups + beta.73 ROM cache) instead of upstream jcmoyer.
- CPU credit in the audio thread (like /VSCAUTO's beta.68 one in DOS): render time capped at CpuMax% (SC55W.INI,
  default 60); render thread priority by load (HIGHEST < 50%, ABOVE_NORMAL < 85%, else NORMAL; was HIGHEST always).
- Run key starts SC55W.EXE /BOOT: synth start waits StartDelay s (default 15) so Windows' startup isn't competing.
- Freeze guard: SC55W.INI [State] Step= (vxd, attach, synth, wave, ok; INI flushed each time). A /BOOT start with
  Step != ok is skipped with a message naming the step (then Step=ok so the next boot tries again). Normal exits
  and Windows shutdown set ok. First 60 s: "trace:" line every 5 s (load, priority, throttled, underruns).
- D810 "freezes a few seconds after boot" since beta.67 (user report): Win98 binaries were byte-identical since at
  least beta.55, so the cause is on the machine (settings, other drivers). Not reproduced: 86Box P2-400 (SB16 PnP,
  Daemon Tools 3.47, SC-55 at 160-365% load) stays responsive with the old and new build (resp.py: Notepad in
  2.8-3.3 s). Waiting for the D810's SC55W.LOG; the guard now stops a boot loop and logs the step.
- Build without the old /home/claude toolchain: MinGW-w64 from Ubuntu; Munt 2.8.5 (d885a95) static lib built by
  hand (b98/config.h written by hand); SC-55 core from upstream/Nuked-SC55 (backend/config.h by hand). VxD and
  SC55MIDI.DRV unchanged (Open Watcom/JWasm not set up this session) - reuse the beta.73 binaries.
- Test rig: 86Box P2B-LS + SB16 PnP Win98 SE (QuickInstall stock image), emulation on 240/300; test builds with
  -march=pentium2 (86Box has no SSE2 CPU). PLAYMID.EXE (no-CRT MIDI file player), resp.py (Start>Run notepad timer).
  QEMU -cpu coreduo Win98 hung at the logo after hardware redetection (not pursued).

## beta.75-usb (2026-11-09): Windows 98 part - joystick in DOS boxes, MIDI volume
- Toolchain back: JWasm built from Baron-von-Riedesel/JWasm (/home/claude/jwasm-src), Open Watcom v2 snapshot
  (Current-build ow-snapshot.tar.xz -> /home/claude/ow). The unchanged sources rebuild SC55W.VXD and SC55MIDI.DRV
  byte-identical to beta.74's, so the toolchain matches the earlier one.
- Interface version 5 (common/sc55wif.h): cfg.joy (bit0 201h, bit1 INT 15h), shared joy_axis[4] / joy_buttons /
  joy_present / joy_reads, midi_volume / midi_volume_set; status ST_OK_JOY 0x40, ST_OK_JBIOS 0x80,
  ST_ERR_JOYPORT 0x20000, ST_ERR_JBIOS 0x40000.
- VxD: 201h trapped (Install_IO_Handler); system VM passes through to the real port (vxd_port_in/out, so VJOYD and
  Win32 programs are unaffected); DOS VMs: write starts the one-shots, axis bit = 1 for 8 + axis*1000/65535 us,
  absent stick = bit stays 1, buttons active low. INT 15h hooked with Hook_V86_Int_Chain (0041h) /
  Unhook_V86_Int_Chain (0118h): AH=84h DX=0 buttons in AL, DX=1 axes 0..255 in AX BX CX DX, CF clear; system VM
  and no joystick -> passed on. PM API AX=2 (DX left, CX right) = MIDI volume, kept across re-attach (vol_saved).
- EXE: joy_main thread (100 Hz, ABOVE_NORMAL): joyGetPosEx on JOYSTICKID1/2, caps re-checked every 3 s (hot plug),
  stick B = joystick 2 or joystick 1's U/Z + R. Status line + "joystick:" log line. Engine multiplies the MIDI
  synth's output by the Windows MIDI volume (L/R); "Windows MIDI volume" log line on change.
- SC55MIDI.DRV: dwSupport = MIDICAPS_VOLUME | MIDICAPS_LRVOLUME; MODM_GET/SETVOLUME (device or handle level),
  volume re-sent to the VxD at each open.
- Tested in 86Box (P2B-LS, SB16 PnP, Win98 SE, -march=pentium2 test EXE): local 86Box patch qt/sdl_joystick.c
  ($JOYSTATE file "x y b1 b2 b3 b4" drives game port 0 joystick 1; plat_joystick_nr=1 so it counts as connected),
  Game Controllers "2-axis, 4-button joystick" OK. DOS box JOYRM/J15 (testrig): right+up+b1 -> 00AA/0004 EC,
  INT 15h AL=E0 AX=00C0 BX=0003; left+down+b3b4 -> 0004/00AA 3C, AL=30 AX=0003 BX=00C0 (uncalibrated range in
  Windows, so not full 00FF); stick B bits stay 1. Without SC55W INT 15h gave the BIOS's 0001s.
  VOLTEST.EXE: NUKED SB-EMU support 3, set 0x80004000 reads back, SC55W.LOG "left 25%, right 50%".
- Xbox 360 Windows 98 driver: done in Win98 w98.1 (below).

## beta.76-usb (2026-11-09): USB mouse without JEMM (D810: touchpad + USB mouse)
- User report (D810): with the USB mouse plugged in, the touchpad worked and the USB mouse was ignored. Cause: the
  normal launchers run without QPIEMU/QEMM (enableRM = 0), so only PM port traps exist; CTMOUSE / MOUSE.COM are
  real-mode, their 60h/64h I/O went to the real KBC and found the touchpad. The USB mouse only ever worked
  with SC55JEM / SC55QMM.
- Fix: INT 15h stub (sbemu/int15.asm, bytes in main.c MAIN_I15Stub; entry now at 10h, data at 0..15) also
  passes AH=C2h (PS/2 BIOS mouse services, which CTMOUSE and MOUSE.COM 8.20 use) to the BIOS and records on
  success: byte 9 enabled (C200h BH), 10 packet size (C205h), 11 resolution (C203h). Without enableRM and
  /USBMS != 0, SBEMU hooks INT 15h (joystick part only if /JOYBIOS) and VKBC_SetBios(lin): vkbc.c inject()
  sends 3-byte packets with D3h when the BIOS mouse is enabled and packet size is 3 (or unset); the BIOS IRQ 12
  handler hands them to the driver like the touchpad's. No KBC traps in this mode; hot-plug works; no wheel.
  With JEMM/QEMM the old trap path is unchanged.
- Known limit: USB and touchpad moving at the very same moment can interleave bytes (one odd packet).
- QEMU (SeaBIOS; USB mouse hot-added after POST so SeaBIOS's own USB mouse support stays out; PS/2 mouse =
  "touchpad"; /tmp/mq run2.sh + MT.EXE INT 33h logger): CTMOUSE 2.1b4 and MOUSE.COM 8.20 both get USB moves +
  left button and PS/2 moves + right button alternately. beta.73 control: PS/2 only. J15 (BIOS joystick) same
  answers as beta.71 (vector offset 0000 -> 0010 only).

## Win98 w98.1 (on beta.76) - 2026-11-09: Xbox 360 pad driver in the pack
- Release: release/NUKED-SB-EMU-1.0.0-beta.76-w98.1-usb.zip (= beta.76 zip + new SC55W.EXE, README98, SOURCE.ZIP,
  FILES\WIN98\X360), tag v1.0.0-beta.76-w98.1-usb. VXD / MIDI driver unchanged.
- win98/x360: morii's unofficial x360c driver for 98 SE/ME (Microsoft XP x360c.sys 5.2.3790 patched; from the user's
  x360c.w98.x86.en.zip: X360C.INF, X360HORI.INF, X360MDCZ.INF (renamed from x360c_hori/_mdcz to 8.3), .SYS, CCP/CFF
  DLLs, X360CSA.EXE, README.JP = Shift-JIS readme) + MDGX/ (X360DrvWin98SE.7z "X360 fixed": combined INF + X2.INF
  right-click install, ADVPACK). Same .sys/.dll in both. X360.TXT = English steps + translated README.JP tips.
- install98: copies FILES\WIN98\X360 tree to <pack>\WIN98\X360 (copy_tree); uninstall deletes it (delete_tree).
- w98main: when no Windows joystick, x360_without_driver() walks HKEY_DYN_DATA\Config Manager\Enum (present
  devnodes), HardWareKey USB\ + one of the 25 VID/PIDs from the INFs, driver missing = no HKLM\Enum\<key> Driver
  value or devnode Problem != 0 -> status/log line + one prompt per run (Yes = X360.TXT in Notepad + folder;
  No = SC55W.INI X360Prompt=0). X360_TEST=1/2 build defines fake it with the 440BX host bridge (PCI) for 86Box.
- Bug found: Windows 98 CreateThread fails when lpThreadId is NULL (the prompt never showed) - always pass &tid.
- Tested in 86Box (P2B-LS, Win98 SE, pentium2 test builds): setup copies X360 (12 files + MDGX); no-driver build:
  status line, prompt, Yes opens X360.TXT + folder, No writes X360Prompt=0 and the next start does not ask;
  has-driver build: "none found"; uninstall removes WIN98\X360. Not testable here: the real pad + driver install
  (86Box/QEMU have no USB pad) - needs the D810.

## beta.77-usb (2026-11-09): SETTINGS, CD images and GAMETEST in 3D
- tools/form3d.h (new): generic 3D list screen on m3core.h. F3VIEW = app, title, status, tabs, rows (F3_HEAD /
  F3_ITEM / F3_VALUE, mark icons, dim, colour), info, foot, keys, accent. Styles = MENU's: 0 Panels (tabs down the
  left, label + inset value with arrows, sliding highlight, 10 rows scrolling), 1 Carousel (cards; LRU cache of 24
  card textures keyed by content; >14 rows: only +-6 around the selection), 2 Module (LCD = page / row / scrolling
  value, tab buttons, knob). f3_key(): keyboard + mouse hits (row click selects, again = Enter; value arrows =
  Left/Right; tab click = F3K_TABSEL; right click = Esc); F3 (style) and F4 (speed) handled inside. Pop-ups in 3D:
  f3_pick, f3_choice/f3_yesno/f3_message, f3_input. On/off = MENU3D.CFG exists (shared with MENU; F8 anywhere
  toggles it); Shift / SC55SAFE = text.
- sbset.c: s3_run() (tabs Main + the 5 sub-menus; pick lists in 3D; SoundFont browser, T (tune) and check_sf
  switch to text and back; Esc on a tab = Main, on Main = quit with Yes/No/Cancel; /PAGE: opens its tab, Esc quits).
  In 3D the synth only becomes the SoundFont when a file is chosen (looking at the tab no longer marks it unsaved).
- cdgui.c: c3_build()/c3_details(); ui_message/ui_yesno pick 3D or text; F1 help and C (files) are text.
- gametest.c: g3_build() (Games / options / Run groups; option values "[*50ms]" = cursor, * = tested); Enter on
  game/option rows = Space; ui_input = f3_input in 3D; result viewers, best settings and test runs are text.
- makefile: cdgui and gametest link -lm; sbset.o, sc55menu and cdgui depend on tools/*.h.
- 86Box (Celeron 366, Panels full / Carousel+Module auto half): all three in all styles, pick list, message,
  input, F8 both ways, help/results text and back, Esc out.

## PLANNED, SHARED (Win98 + DOS): gamepad buttons -> keyboard keys per game (Win98 session started it)
- Data: DOSBox Pure's Keyb2Joypad database (Jemy Murphy and bigjim; eXoDOS community; shipped in DOSBox Pure, GPL-2).
  tools/padmap/k2j.py decodes DOSBox Pure 0.9.9's keyb2joypad.cpp (data only, raw-deflate tables) to JSON:
  3557 games, 4232 file ids, every bind has an action name ("Jump", "Pogo"). Newer data is on
  codeberg.org/schelling/dosbox-pure (blocked from the container; user may attach it). Data terms: no explicit
  license found, so the user's decision: the data is NOT shipped. DOWNLOADS.TXT links
  https://raw.githubusercontent.com/schellingb/dosbox-pure/0.9.9/keyb2joypad.cpp -> the pack's FILES\PADMAP;
  tools/padmap/padconv.c (own raw inflate, plain C, no downloaded code) turns it into PADMAP.TXT on the user's
  machine: same 3557 games / 4232 ids as k2j.py. Built for Win32 (MinGW); DOS = same source with DJGPP.
- Game id = FNV-1 32 (multiply, then xor; not FNV-1a) over the UPPERCASE 8.3 file name, then ^= (file size << 3)
  (verified: WOLF3D.EXE 98402, PRINCE.EXE 123335, KEEN4E.EXE 103224/105108). Ids also cover data files
  (GAMEMAPS.WL6), so match the started EXE first, then the files in its folder.
- Button names in the data are RetroArch/SNES positions: B = bottom (Xbox A), A = right (Xbox B), Y = left (Xbox X),
  X = top (Xbox Y), L/R = LB/RB, L2/R2 = LT/RT, L3/R3 = stick clicks, Select = Back. Ids 16-19 = LX LY RX RY
  (two keys each: negative/positive). Key codes = DOSBox KBD_* enum; 200-225 = mouse/joystick/hat specials
  (JoyButton1-4, Joy1Up.. -> send to the game port instead of a key).
- Proposed shared files (same format and lookup code for SC55W and the DOS USB layer):
  PADMAP.TXT  readable, generated (one section per game, Xbox button names, "key:action" values, id= list);
  PADUSER.TXT the user's own sections/overrides, same format, wins over PADMAP;
  PADMAP.BIN  compiled from both: sorted (u32 id, u16 entry) table + entries, so DOS reads only the matched entry.
  Lookup when a program starts (DOS: EXEC hook; Win98: program running in the DOS box), else [default]
  (D-pad = arrows, Start = Enter, Back = Esc, A/B/X/Y = game port buttons 1-4).
- Split: Win98 session = format, converter, engine (C, shared), SC55W + VxD key injection (VKD_Force_Keys).
  DOS session = feed the engine from its USB pad code and inject through its existing 60h/64h traps.
  DONE on both sides: Win98 in w98.2, DOS in beta.79 (sbemu/padmap.c = pad.cpp in plain C, see beta.79 below).

## Win98 w98.2 (on beta.77) - 2026-11-09: pad buttons -> keys per game
- Release: release/NUKED-SB-EMU-1.0.0-beta.77-w98.2-usb.zip (= beta.77 zip + new SC55W.EXE, SC55W.VXD, README98,
  DOWNLOADS.TXT, SOURCE.ZIP, empty FILES\PADMAP), tag v1.0.0-beta.77-w98.2-usb. SC55MIDI.DRV unchanged.
- Decided by the user: the Keyb2Joypad data is NOT shipped, only linked (DOWNLOADS.TXT); the X360 driver stays.
- VxD (IF version 6): IOCTL_SC55W_KEYS 0x16 (VKD_Force_Keys into the VM that has the keyboard; when that is the
  system VM only break-only sequences go through - see below), IOCTL_SC55W_FOCUS 0x17 (SC55W_FOCUS: owner VM + its
  program list). INT 21h V86 hook (cfg.joy bit2): AX=4B00h -> nested DOS calls (Save_Client_State,
  Begin_Nest_V86_Exec, Exec_Int 21h: 2Fh get DTA, 60h TRUENAME into the DTA, 4Eh FINDFIRST for the size; DTA bytes
  saved/restored) -> pushed on a per-VM list (8 VMs x 4 programs); AH=4Ch/31h/00h pop. Create_VM/Destroy_VM clear
  a slot. VMM ordinals 82h Begin_Nest_V86_Exec, 84h Exec_Int, 86h End_Nest_Exec, 8Dh/8Eh Save/Restore_Client_State
  and VKD (0Dh) 7 Force_Keys, 8 Get_Kbd_Owner: VERIFIED in 86Box (programs tracked, keys typed).
- helper/pad.cpp/.h: PADMAP.TXT + PADUSER.TXT parser (user sections override line by line, [Default] too), key
  names -> set-1 scan codes (E0 for extended), JoyButton1-4 / Joy(2)Up.. -> game port (axis bound to JoyLeft
  JoyRight = analog pass-through), pad_select (newest program's name+size id, then files in its folder),
  pad_step (key diff -> make/break). w98main: [Pad] INI (A..RS button numbers, default x360 order 1-10, D-pad =
  POV, Triggers=Z, RStickX=U, RStickY=R), PadKeys=1, FOCUS poll every 100 ms, "pad:" log lines, status line
  "Pad keys: layout ... Last button: X (Windows button n)". PADMAP.TXT rebuilt at start from
  <base>\PADMAP\keyb2joypad.cpp when newer (padconv.c built in, -DPADCONV_LIB). install98 copies FILES\PADMAP.
- Fixed in testing:
  - VKD hands the shift keys a DOS VM holds (Ctrl/Alt/Shift, forced keys included) on to Windows when it takes
    the keyboard: pad A held in Keen (LCtrl) + Start menu -> Ctrl stuck in Windows (Esc = Ctrl+Esc, menu would
    not close). Now pad_step sends the breaks of held keys at once when focus leaves DOS (the VxD lets
    break-only sequences into the system VM) and again when a DOS box has the keyboard back unless still held
    (g_stale; a non-shift key like Space stays down in the DOS VM otherwise).
  - padconv: titles that differ only in case (OverKill 1992 / Overkill 1996, Wipeout / WipEout) were merged by
    pad.cpp (titles match without case): now numbered like exact repeats ("Overkill (2)"): 3557 sections.
  - The id hash is FNV-1 (multiply, then xor), not FNV-1a as the comments said (code was right).
- Tested in 86Box (P2B-LS PII-400, SB16 PnP, Win98 SE from the user's QuickInstall 1.1.0 stock ISO, installed in
  QEMU then booted in 86Box; game port joystick "2-axis, 4-button" in Game Controllers; Pentium II test build):
  Setup copies PADMAP, first start makes PADMAP.TXT (3557 games, 4232 ids), log shows the program chain
  (COMMAND.COM, MODE.COM, KEEN4E.EXE "found by" its id). KT (fake Keen 4): A -> 1D 9D, B -> 39 B9, stick left ->
  E0 2A E0 4B E0 CB E0 AA (VKD adds the NumLock fake shifts, as a real keyboard), Y -> 1C 9C, X -> 38 B8.
  KD (Default + PADUSER "[Default] Y=F1"): only Y -> 3B BB. Focus test: A held, Start menu, released, Esc back:
  1D 9D ... 9D, Windows keyboard fine afterwards. Not testable here: the real 360 pad (D810 / P5PE-VM).
- Test rig (rebuilt this session; nothing of it is in the repo but the scripts):
  - Win98 image: QuickInstall ISO (github.com/oerg866/win98-quickinstall v1.1.0 win98qi_v1.1.0_stock.iso) booted
    in qemu-system-i386 -cpu pentium2 (press Enter at the loader prompt within its countdown), DISK -> cfdisk
    (1 GB, type c, bootable, Write), INSTALL defaults; then 86Box with testrig/86box/86box-joystate.diff applied
    (sdl_joystick.c: $JOYSTATE file "x y b1 b2 b3 b4" drives game port joystick 1) on top of 86box-xpad.diff.
    86Box config: p2bls, sb16_pnp, joystick_type = 2axis_4button, joystick_0_nr = 1; F1 at the CMOS message.
  - win98/test/keys: k.py (keys/typing/screenshots over the automation socket: win+r, @type:..., shot),
    joyseq.sh (A, B, stick left, Y, X), mkkeen.py (fake KEEN4E.EXE from KEYLOG.COM), KT/KD.BAT now write
    C:\KT.TXT / C:\KD.TXT (COM1 without an automation backend hung KEYLOG; read the files with mtools after
    shutdown, offset 1048576). Copy files with mtools while 86Box is off; killing 86Box -> ScanDisk next boot.
  - Toolchain: mingw-w64 (Ubuntu, win32 threads), Open Watcom snapshot, JWasm (Baron-von-Riedesel), Munt
    d885a95 (b98/config.h by hand: 2.8.5, EXPORTS_TYPE 3), Nuked-SC55 from upstream/ (build98/backend/config.h
    by hand); win98/third_party stands in for the old sbemu-src / nopl3 paths (symlinks). build98.sh takes
    ARCH= OBJ= LIB= for the Pentium II test build (ARCH="-march=pentium2 -O2" OBJ=build98p2 LIB=b98p2).
    The VxD rebuilt byte-identical to the committed WIP one, so the toolchain matches.
- Next: real pad on the D810 / P5PE-VM (button numbers via the status line); DOS session: feed pad.cpp's format
  from usbpad.c and inject through vkbc.c (PLANNED, SHARED above).

## beta.78-usb (2026-11-09): USB mouse in BIOS mode (D810 random moves / clicks; Award: no movement at all)
- Cause, found in 86Box (P2B-LS, Award BIOS, KBC trace): the BIOS's IRQ 12 handler reads a whole PS/2 packet in one
  interrupt - after byte 1 it polls port 64h (interrupts off) for bytes 2 and 3 and gives up after ~70 polls.
  SBEMU put the bytes in one per sound interrupt (D3h), so byte 2 could never come while the BIOS waited: on
  Award nothing moved; on the D810 (Dell BIOS, most likely the same style - not confirmed) packets slip - dx/dy bytes with bit 3 set
  (08 18 28..) were taken as packet starts: the random moves and clicks. QEMU/SeaBIOS takes one byte per IRQ,
  which is why beta.76 worked there. The "in flight" theory of the paused branch was not the cause (its wait is
  kept: harmless, and right for slow D2h keyboard bytes).
- BIOS mode fix (sbemu/int15.asm, main.c MAIN_I15Stub, now 361 bytes; data 0..1Fh, INT 15h entry 20h, deliver
  24h): on C207h the stub gives the BIOS its own handler (bioscb) and keeps the driver's (byte 16); bioscb passes
  the BIOS's packets (touchpad) on with busy (byte 15) set. vkbc.c bios_deliver: the USB packet (status, X, Y at
  20/22/24) goes to the driver's handler by a DPMI far call to deliver (refused while busy / no handler / mouse
  off: the movement is kept for the next try). No KBC involved, so packets cannot slip. Counters: 26 delivered,
  27 refused.
- Trap mode (SC55JEM/QMM): VKBC_IOHandler on a trapped port 64h read with mouse bytes queued calls inject() first,
  so the BIOS's polling loop finds the next byte. Before: no movement on Award either.
- 86Box rig: KBC_OB_DELAY_US was 2^32 times too long (TIMER_USEC is 32.32 fixed point): with it set, every D2h/D3h
  byte was lost - the other half of "the scripted USB mouse never moved". Fixed in 86box-xpad.diff.
- Test VM (rebuilt this session; disk not in the repo): Windows 98 QuickInstall image (see Win98 w98.2) with
  MSDOS.SYS BootGUI=0 = MS-DOS 7.1 like the D810's MS-DOS mode, HIMEM (or JEMMEX for SC55JEM), pack installed by
  SETUP, 86Box P2B-LS (UHCI + OPTi OHCI, ES1371, PS/2 mouse = touchpad), env XPAD=2 XPAD_KIND2=mouse. Scripts kept
  in /home/claude/86vm (mt2.py: KMTEST pointer before/after USB move + click + PS/2 move; msseq.sh).
  Results (pointer x from 320): beta.77 BIOS mode: USB 320 (no move), PS/2 416. beta.78 BIOS mode, KBC delay 0 and
  300 us: USB 368, PS/2 +96 after it. beta.77 SC55JEM: USB no move; beta.78 SC55JEM (delay 0 / 300): USB 368.
  USB + PS/2 moving together (KMTEST log): USB x rises steadily, one right click down/up, no stray clicks.
  MENU (SC55MENU) clicked with the USB mouse (BIOS mode, delay 300): Games tab opened. J15: INT 15h AH=84h through
  the moved entry still answers (vector seg:0020). No real game with a mouse was available in the rig.
- Not testable here: the real D810 (Dell BIOS) - user to confirm with KMTEST and a mouse game.

## beta.79-usb (2026-11-09): pad buttons -> keys with a layout per game, in plain DOS (DOS side of PLANNED, SHARED)
- sbemu/padmap.c/.h: win98/helper/pad.cpp ported to plain C (DJGPP + host gcc), same files and rules: PADMAP.TXT +
  PADUSER.TXT (user sections override line by line, user Id= lines win), same key table/names, JoyButton1-4 /
  Joy..(2).. game port actions, analog pass-through for whole-axis binds, [Default]. Verified against pad.cpp on the
  host (/home/claude/padtest/w/diff.cpp, windows.h shim): all 4233 ids x 40 random pad states, same scan codes,
  game port buttons/axes/present (after a release between layouts - SBEMU lets every held key go on a layout change).
- PADMAP.BIN (PADMAP_Build/Load): "PADMAP1\x1A", stamps (FAT date/time + size of PADMAP.TXT, PADUSER.TXT,
  keyb2joypad.cpp), sorted (u32 id, u32 layout) table, per layout: title, set slots (0-15 buttons, 16+2a / 17+2a
  axis neg/pos) with action bytes. 280 KB for 3557 games, loaded whole into SBEMU's memory at start (no file I/O
  while resident). PADMAP_Prepare(dir): PADMAP\*.CPP (KEYB2J~1.CPP in plain DOS) -> PADMAP.TXT through padconv.c
  (built in: sbemu/padconvl.c, -DPADCONV_LIB) when missing or the .cpp's stamp changed, then BIN when any stamp
  changed. 86Box PII-400: 1.5 s + 0.85 s, once.
- Program tracking: sbemu/int21.asm (scripts/mkint21.py -> sbemu/int21bin.h, 515 bytes, entry C0h), hooked only when
  layouts are there. AX=4B00h: TRUENAME + FINDFIRST (own DTA, the caller's put back) before DOS is entered, RMCB
  (AL=1 own id; answer 0 = scan the folder: AL=2 per file, max 1024; AL=3 done), then the stub runs the EXEC itself
  (pushf/call far) and calls back AL=4 when it returns - every way a program ends, no exit hooks. CF of the EXEC into
  the caller's flags, IF as the caller had it (iret). main.c MAIN_Int21: stack of 8 programs; layout = newest with its
  own id, else newest found by a folder file, else Default (as SC55W). Pick log (last 8) for SBEMU /PADLOG (AMIS AL=03h;
  the /PADLOG run opens neither SBEMULOG nor SBDETAIL). Unhooked if start-up fails.
- vjoy.c: VJOY_Tick runs PADMAP_Step on both pads (keys from either; game port from pad 1) -> VKBC_Bytes (new, raw
  set-1 bytes into vkbc's D2h queue). Mode 0 + Default: /JOYAX /JOYBT as before minus what the layout types keys with
  (D-pad = arrows, no longer joystick A). Mode 0 + a game's layout: port 201h / INT 15h AH=84h = the layout's Joy
  binds (none = no joystick). Modes 1-3: port unchanged, layout only adds keys. /JOYKEY: used when layouts are off or
  missing; a non-default /JOYKEY overrides its buttons in every layout. Held keys don't repeat.
- Options: /PADKEYS (default 1; also live from a second SBEMU run: stub byte 8 + VJOY_PadKeys), /PADLOG. SETTINGS >
  Joystick > "Keys per game" (text + 3D). USBTEST /PAD[:GAME.EXE]: layout + why + id, live key codes per button.
  PADCONV.EXE (DOS build of tools/padmap/padconv.c) in FILES\SC55. SETUP copies FILES\PADMAP to C:\SC55\PADMAP.
  SBDETAIL: "Pad layouts: ...", "Pad layout Default: ...", "Pad keys: on (INT 21h stub at ...)".
- Tested (testrig/padkeys, README there): QEMU 8.2.2 (built here: usbredir 0.7 parser, pixman) + fakepad live, PIIX3
  UHCI, AC97: FreeDOS + JEMMEX + QPIEMU; MS-DOS 7.1 (from the user's Win98 QuickInstall ISO, MercyPak extract,
  MS boot record) + HIMEM + HDPMI (D810 MS-DOS mode set-up); MS-DOS 7.1 + JEMMEX + QPIEMU with /PADKEYS:0 / :1 live.
  86Box P2B-LS PII-400 (ES1371, PIIX4 UHCI + OPTi OHCI, emulated 360 pad, built here with header-only SDL2/OpenAL/
  sndfile/zstd from GitHub and a SLiRP stub) with HIMEM and with JEMMEX+QPIEMU: fake Keen (own id) A 1D 9D, B 39 B9,
  X 38 B8, Y 1C 9C, stick left E0 4B/CB, D-pad up E0 48/C8, Start 1C 9C, RT 3B BB, PADUSER RB=F2+F3 3C 3D BC BD;
  RUN.EXE next to WOLF3D.EXE (folder) LAlt+Left 38 E0 4B ... B8 E0 CB; unknown = Default (A no key, PADUSER
  [Default] Y=F1 3B BB). Real games (user's files, QEMU, MS-DOS 7.1 + HIMEM + HDPMI, screenshots): Keen 4 v1.4
  (102355-byte EXE, not in the ids: found by EGAGRAPH.CK4 in its folder) menus by Start/D-pad/Back; Doom II 1.9
  (DOS/4GW; found by README.TXT -> [DOOM]): Y menu, stick cursor, Start new game/skill, walk, RB fire, B map, LT run.
- After the release: the user's Supergames CD (shareware): all 13 installed games get their layout (Bio Menace,
  Doom 1, Duke 1, Gods, Keen Dreams, Keen 1, Keen 4 C/E, Keen 6, Major Stryker, Epic Pinball, Spear of Destiny,
  Wolf3D 1.4 - mostly by a data file in the folder). Full Wolf3D 1.4 (user's copy; found by GAMEMAPS.WL6) played in
  86Box (testrig/padkeys/drive86s.py + wolf2.steps, screenshots via automation "screenshot <path>"): Start through
  the menus, stick walks, RB fires (ammo 8 -> 7), stick sideways strafes (LAlt+Left), Y = Options menu. QEMU shows
  this Wolf3D build black with or without SBEMU (QEMU's VGA, not ours).
- Not testable here: the real 360 pad on the D810 / P5PE-VM (NEC), real KBCs (Dell's EC).

## beta.80-usb (2026-11-09): pad key repeat, stick threshold from /JOYDZ, shared keys, lost-IRQ1 recovery
- Game sweep first (86Box, MS-DOS 7.1 + HIMEM + HDPMI, testrig/padkeys drive86s.py + game.steps; VMs built per game
  by /home/claude/padtest/gt/mk.sh): the user's Supergames CD - Bio Menace, Doom 1 sw, Duke 1, Gods, Keen Dreams
  (START.EXE launcher needs key 5; KDREAMS.EXE alone refuses), Keen 1, Keen 4 CGA/EGA, Keen 6 demo, Major Stryker,
  Epic Pinball, Spear of Destiny, Wolf3D 1.4 sw: all pick their layout and react to the pad, no hangs.
- vkbc.c: Held[2][128] count per key (USB keyboards, /JOYKEY, pad): a break is sent only when the last holder lets go
  (PS/2 keyboard not seen). VKBC_PadBytes (pad layout keys): counted + repeat of the last pad key (500 ms, 33 ms;
  not Caps/Num/Scroll Lock). USB keyboard repeats no longer go through the count (usage_key again=1 -> put_key).
- BUG found with repeat in 86Box (Award BIOS): after a few repeated bytes nothing more arrived (release and later keys
  lost; SBEMU alive). A keyboard byte stayed in the KBC unread (OBF set, no IRQ 1 - most likely put in while the
  BIOS IRQ 1 handler had the keyboard off, AD..AE), so inject() never found the controller idle again. Fix: a keyboard
  byte unread for 50 ms is read back and queued in front again (fresh IRQ 1); VKBC_Unstuck() counts it. Probably the
  cause of rare lost/stuck keys with fast USB keyboard typing before, too. After: Space held 45 repeats + release,
  then Ctrl the same, all later keys arrive (86Box), QEMU unchanged.
- padmap.c PADMAP_SetDeadZone (from VJOY_Init / VJOY_SetLive): stick-to-key point 13000 + (dz - 10) * 328, clamped
  3300..31000 (10% = SC55W's 13000; pad.cpp equivalence test still 0 mismatches).
- SBEMU /PADLOG prints only the answer (no banner, no QEMM/HDPMI notes).
- Tested: QEMU usb-kbd + fakepad: pad Ctrl down, USB Ctrl down, pad up (no break), marker 39 B9, USB up -> 9D.
  86Box JEMMEX+QPIEMU scan-code run (Keen/Wolf folder/Default) with repeats.

## Win98 w98.3 (on beta.80) - work in progress (2026-11-10)
User asked for a Win98 overhaul: controllers in DOS boxes AND Windows games, a virtual NUKED game port (Device
Manager, address 200-20F) for PCs whose sound driver has none, a tabbed settings window in SC55W (incl. a Joystick
tab on top of the x360c driver), then release beta.80-w98.3; afterwards CPU-usage work in DOS + Windows.
- Toolchain (DONE): the EXE needs msvcrt.dll + win32 threads. xPack MinGW is UCRT (112 bad imports on Win98) - do
  not use it. win98/tools/build-gcc.sh builds GCC 13.2 (mingw-w64 v11.0.1, --with-default-msvcrt=msvcrt,
  --enable-threads=win32) into /home/claude/gccb/prefix (~30 min). Then
  PATH=/home/claude/gccb/tools/bin:/home/claude/gccb/prefix/bin:$PATH; sh tools/build-deps.sh; sh build98.sh.
  tools/chkimp2.py out/SC55W.EXE (Win98 DLLs from FULL.866 in /home/claude/w98dll): only GetModuleHandleW and
  LoadLibraryW W-STUB, same as shipped. VxD (Open Watcom + JWasm) and SC55MIDI.DRV rebuild byte-identical.
- Windows' joystick polling (VJOYD, MSANALOG) is ring 0, so an I/O trap can't feed Windows games. Plan: a VJoyD
  minidriver NUKEDJOY.VXD (original interface, Win95 DDK msjstick sample: github joncampbell123/
  windows_sdk_collection win/win95/ddk-1995-07/ddk/mmedia/samples/msjstick/vxd/vjoyd.h). Facts:
  VJOYD_Device_ID = 0x449 (MULTIMEDIA_OEM_ID 0x440 + 9), service 1 = VJOYD_Register_Device_Driver
  (EAX poll, EBX cfg, ECX hwcaps, EDX joyid; call it in Sys_Dynamic_Device_Init - VJOYD loads the VxD named by
  MediaResources\Joystick\<drv>\CurrentJoystickSettings\JoystickNOEMCallout and only registers on FIRST init, so
  SC55W.EXE must never CreateFile it). Poll: int __stdcall poll(int type, JOYOEMPOLLDATA*) type 0..5 = POLL1..6,
  6 = GETBUTTONS; data {id, do_other, JOYPOS jp (X Y Z R U V), dwPOV, dwButtons, dwButtonNumber};
  return 1 OK, 0 FAIL (unplugged), -1 "you poll". hwcaps(joyid, {maxbuttons, maxaxes, numaxes}).
  OEM type key: MediaProperties\PrivateProperties\Joystick\OEM\<name>: OEMName, OEMData (JOYREGHWSETTINGS),
  OEMCallout = NUKEDJOY.VXD.
- Win98 test VM (DONE, QEMU): the user's QuickInstall ISO installs itself in QEMU (qi.iso as CD, an empty
  FAT32 partition made by testrig/mkfat32.py; spam Enter at boot - its timer prompt shows 0s in QEMU), then
  boot from disk only. /home/claude/w98vm: run.sh (P3+SSE2, 128 MB, SB16, PIIX3 UHCI), base98.img (fresh),
  t1.img (x360c + SC55W installed + NUKED game port + NUKED joystick added), d.img = FAT16 transfer disk built
  from /home/claude/w98d by testrig/mkdisk.py. Helpers in /tmp (recreate if gone): qk.py (monitor: keys/text/
  screenshots), vmboot.sh / vmoff.sh / vmrun.sh (Start > Run), padplug.sh (fake 360 pad hot-plugged AFTER boot:
  with usb-redir present at boot SeaBIOS leaves IO.SYS spinning with IF=0), dget.sh (file from D:).
  D:\UPD.BAT updates SC55W in the VM (start /w SC55W /EXIT first), D:\SETINI.BAT Tn switches [Joystick].
- NUKEDJOY.VXD WORKS (Joystick 2 in Game Controllers = "NUKED SB-EMU Joystick", polled by us). Lessons:
  VJOYD's service table has NO Get_Version: Register_Device_Driver is service 0 (MSGAME.VXD: cd 20 00 00 49 04).
  Windows 98 VJoyD only binds a callout that registers the DirectX 5 way, done like MSANALOG.VXD: EAX=-1,
  EDX -> VJREGDRVINFO (44 bytes, function 1, flags 0 for us - MSANALOG uses 0x20000 ISANALOGPORTDRIVER),
  EBX=ECX=0, VJPOLLREG {24, poll, 0, init, escape, ctrlmsg}; register on Sys_Dynamic_Device_Init AND on
  control message 70000000h (VJoyD re-asks). Poll(id, *mask, VJPOLLDATA) must fill every field. The joystick's
  calibration (Joystick<n>Configuration) is empty when Game Controllers adds it: SC55W writes 0..65535 and
  calls joyConfigChanged. NUKEDJOY notes what VJoyD did in HKLM\Software\NUKED SB-EMU\NukedJoy (regedit /e).
- Verified in the VM: WJOY.EXE (win98/test) shows NUKED joystick X/Y/Z/R/POV/buttons from the pad;
  JPORT.EXE (DOS, in a DOS box): Port=209 moves the game port (201h then reads as no stick), INT 15h, CH
  (D-pad up = all 4 lines), FCS (hat on axis 4: right = 64/255), GrIP frames (sync bits 18-22, A at bit 6).
  x360c under QEMU's fake pad: Windows U axis stays 0 (right stick X shows on V) - check RStickX on real pad.
- RELEASED beta.80-w98.3 (tag v1.0.0-beta.80-w98.3-usb): zip = beta.80 zip + SC55W.EXE, SC55W.VXD (interface 7),
  README98.TXT (x2), SOURCE.ZIP, new FILES\WIN98\JOY\NUKEDJOY.VXD + NUKEDJOY.INF. SC55MIDI.DRV unchanged.
  Settings window: tabs "Sound and MIDI" / "Joystick" / "Pad keys" (Ctrl+Tab), helper/w98main.cpp (pages are child
  dialogs IDD_PAGE_JOY/IDD_PAGE_PAD; page 0 = the old controls shifted down 18 DLU). Joystick tab: game port on/off,
  address 200-20F, stick type, INT 15h, "also for Windows programs" ([Joystick] WindowsPort: the VxD answers the
  system VM too), NUKED joystick feed/mode/dead zone, Set up... / Remove (helper/gameport.cpp writes the device the
  way the INF install does: Enum\Root\MEDIA\NNNN + Services\Class\MEDIA\NNNN, ForcedConfig with the address,
  LogConfig 201,200,202..20F, the OEM type and a JoystickN of that type with calibration), live test view.
  Pad keys tab: PadKeys, layout status, PADUSER/PADMAP buttons, [Pad] map with learn-by-pressing.
  Uninstall removes the game port + joystick. Install copies WIN98\JOY.
- Tested in the Win98 VM (QEMU): automatic Set up on a fresh install (t3.img) -> after restart Joystick 2 =
  NUKEDJOY.VXD, values follow the pad, Device Manager lists "NUKED SB-EMU Game Port" without a problem mark;
  Remove; Windows program reading 201h (WJOY P) gets our port only with WindowsPort=1; DOS box port at 209h,
  CH/FCS/GrIP. DOS regression (QEMU, MS-DOS 7.1, JEMMEX + QPIEMU + HDPMI, SBEMU beta.80 /JOYMODE:0-3, JPORT):
  standard, CH (D-pad up = all 4 lines), FCS (hat axis 4), GrIP frames, INT 15h - all OK
  (/home/claude/padtest/gt/joyreg). Red Baron 3D installs and reaches its main menu in the VM; QEMU mouse clicks
  did not reach its menus, so flying it with the pad is left for real hardware.
- Not testable here / to check on the user's hardware: real wired 360 pad + x360c (fakepad's right stick showed on
  Windows axis V, triggers did not move Z - check [Pad] RStickX/Triggers with the Pad keys tab); Start/Back
  numbering of x360c; Red Baron 3D in flight; the D810 / P5PE-VM.
- NEXT (user asked): CPU-usage optimisation in DOS (SBEMU) and Windows (SC55W, VxD), after this release.

## beta.81-w98.4 (2026-11-10): CPU usage in Windows 98 and DOS (RELEASED, tag v1.0.0-beta.81-w98.4-usb)
Zip = beta.80-w98.3 zip + SBEMU.EXE, SBSET.EXE, SC55WAV.EXE, SC55LOG.EXE (DOS), SC55W.EXE, SC55W.VXD, SOURCE.ZIP,
README98.TXT (x2), README.TXT. NUKEDJOY.VXD and SC55MIDI.DRV unchanged (sources unchanged). makefile VERSION beta.81.
Windows (SC55W):
- Output sleep (helper/w98main.cpp + engine.cpp/.h): when the synths sleep, no DMA/DAC, nothing waits in the VxD's
  rings and the last second was exact silence, the audio thread calls waveOutReset and stops feeding the card
  (eng.st.out_asleep; Status "paused while silent"). While asleep it checks Engine::has_input every 20 ms and calls
  vxd_now() every 250 ms (the VxD drops its 1 ms timer from IOCTL_SC55W_TIME). Input -> wake_output() (fresh time
  window, no resync/late counts), all buffers rendered at once. No-output path: same idea, 250 ms ticks.
  SC55W.INI OutputSleep=0 turns it off (for a driver that clicks or drops the first note). Off while RecordWav= is on.
- VxD 1 ms timer only while DMA runs (+2 s); no timeBeginPeriod(1) in SC55W; joystick thread 10 Hz when unused
  (timeBeginPeriod(1) only while busy); settings reload only after a Joystick page save.
- Engine: denormal flush in the DC blocker + PCM underrun fade (a sleeping SC-55 cost MORE than a playing one on
  P4/Pentium M), DC blocker skipped when settled; synth idle after 2 s (4 s in the first 8 s).
DOS (SBEMU): Nuked OPL3 core (/FMCORE:1) sleeps after 2 s of silent output until the next register write. NOTE: a
keyed-on voice with its envelope fully down outputs -1/0 flicker, not exact 0 - silence = |sample| <= 1
(testrig/cpu/opl_sleep_test.cpp, host test; run it after touching opl3emu.cpp). SC-55 tail 750 ms (was 1500).
Measured: 86Box P2B-LS, Pentium II 450, dynarec ON (stable to +-0.02% for CPULOAD in Windows; the interpreter drops
to ~0.1x real time once a busy loop runs). Win98 = QEMU base98x.img moved to 86Box (padded to 2072 cyl, 63/16),
ESS Solo-1 card (Win98 found C:\DRIVER\SOLO1.INF; ES1371 has no driver in the QuickInstall image, SB16 gave no
waveOut device). VM: /home/claude/padtest/w86 (86box.cfg, hd.img base saved as w86-hd-solo.img, d.img = MEASW disk).
Pentium II builds of SC55W: ARCH="-march=pentium2 -O2" OBJ=build98p2 LIB=b98p2 for build-deps.sh and build98.sh.
  CPULOAD loops/s (higher = more left over): no SC55W 148.87M; beta.80-w98.3 idle 146.48M (1.6%); before output
  sleep 147.11M (1.2%); beta.81-w98.4 148.50M (0.25%); back to 148.43M right after MIDI notes.
  MIDIT.EXE (testrig/cpu) plays 3 notes through "NUKED SB-EMU" after the output slept: 86Box "audio start DIR"
  capture shows all 3 at the same level and timing as the old build. Tools: testrig/cpu/measw.c (MEASW runs a
  script from D:), cpuload.c, midit.c, oplt.c (DOS FM notes). 86Box DOS + ES1371: the automation audio capture
  stayed silent for old and new SBEMU alike (rig issue, not looked into) - the OPL sleep was tested on the host.
- SC55W.EXE needs SSE2 (Pentium M build): on a PII/PIII it dies with PSHUFD at start (README98 says so now).
  A non-SSE2 build (ARCH above) works, tested in 86Box, if a user ever needs one.
- DOS idle overhead of SBEMU (PII-450) is ~1.3%, mostly the card interrupt round trip; not changed.
- To test on the real PCs: sound after a pause (first note of a MIDI file / first sound in a DOS game after some
  seconds of silence) with the SigmaTel AC'97 driver on the D810; if it clicks or the first note is missing,
  OutputSleep=0 and tell us.

## beta.82 (2026-11-10): virtual CD overhaul - "read error" when a game starts (RELEASED, tag v1.0.0-beta.82-usb)
User report: "virtual CD roms always give a read error in DOS" - when STARTING A GAME, Win98 boot menu -> DOS,
BIN/CUE (one BIN) and ISO. File reads were always fine (CDCHK2: 165 files + 300 seeks OK in every setup).
ROOT CAUSE: with a real CD-ROM driver loaded (BOOTCFG / Win98 boot, D810 has a drive), CDON.BAT gave the real drive
D: first and M: second; INT 2Fh 1500h ("first CD drive") = D:, empty -> the game reads D: -> CDR101 / read error.
SHSUCDX hands out ascending letters only, so listing UCDD first moved the real drive to N:.
FIX (user chose: keep M:, list it first, real drive stays D:):
- SHSUCDX 3.09 from source (third_party/shsucdx, adoxa/shsucd 5ea0787, zlib-style licence, marked ALTERED) + /P:x:
  MovePrimary moves drive x's DrvEnt to the front of the table before InitDrive (only lookups by letter exist, Bufp
  is assigned in table order later) -> 1500h CX, 1501h, 150Dh list it first. CDON/CDDIAG use /P:M.
- uCDD 0.9.5-a3 from source (third_party/ucdd, vorvek/uCDD 6c2f3e3, GPL-3) replaces the binary-patched a2 (whose
  SBCDA1 patch source was lost): tools/ucdd/build.py + mkcdset.sh (NASM; upstream's build.py was blocked as external
  code, ours does the same EXE packing). Define SBEMU_LINK: src/sbemulink.asm = the drive's own CD-Audio when no
  external audio entry is attached (driver.asm hooks under %ifdef SBEMU_LINK: audio_request fallback, seek, IOCTL 1/15,
  status bits 0310h, busy bit 200h in .done for non-read requests while playing, reset, clear_unit -> va_unit_gone).
  PLAY/STOP(pause, 2nd = reset)/RESUME/SEEK, Q-channel, audio status, channel info/control; position from BIOS ticks
  (4.119 sectors/tick) or SBEMU's box pos when SBEMU is attached (box.flags bit 0), ends at the range end (SBEMU's
  state 0, or timer +3 s safety). SBCDA1 v1 box layout unchanged (cdlink.c untouched); start/end/pos are FILE
  sectors = image sector - the start track's synthetic gap (PREGAP lines now map right: tested). IMAGE_PATH still 26.
  PLAY on a data track / data-only disc -> 810Ch (as upstream resident audio). No timer hook: no CPU when idle.
- CDIMG.BAT unmounts first (a3 refuses -mount over a mounted disc). CDGUI unchanged (uses errorlevels, ejects first).
- UCDD-DOC.TXT / UCDD-REL.TXT from a3 (with a header on this build), SHSUCDX.TXT /P section, FILES\CDSET\UCDDSRC.ZIP
  (GPL source). README.TXT section 9 "SINCE BETA.82", section 10 PREGAP limit dropped.
Tested (86Box P2B-LS, MS-DOS 7.1, empty ATAPI drive, testrig/cdtest): A VIDE-CDD only (M first, list "M D"),
B VIDE-CDD + MSCDEX (SHSUCDX /I: only M visible to the API, as before), C no real drive - ISO + CUE all OK;
SBEMU /VCDA with the new driver: box found, flags 6F (attached/open/read/mixed), pos advances, eject stops it.
Not fixed / known: empty drive DIR -> MS-DOS "General failure" (SHSUCDX returns not-ready for the free-space call;
MSCDEX would say "Not ready"); 150Fh untested (tester call form); multi-BIN CUE still unsupported (a3 too);
games that scan A:..Z: with 150Bh still find the real D: first (README says: boot without the real driver).
To test on real PCs: a CD game from an image on the D810 booted via the Win98 menu with BOOTCFG's CD driver;
CD music games with HYBRID.BAT; disc changes for multi-CD games.

## beta.83 (2026-11-10): MIDPLAY rewrite - speed in all display modes, pages, smarter menus (RELEASED, tag v1.0.0-beta.83-usb)
User: "improve the midplay player, optimize its performance in all display modes, make it look better and make its
menus more functional and smart"; then "the resolution in the 3d portion should be fixed it seems blurry".
tools/midplay.c rewritten (~2000 lines; parser and song_info() scanner kept). Layout of the file:
- Platform: key defines (K_DEL/INS/CUP/CDOWN/CLEFT/CRIGHT/F4-F10, Ctrl+A/O/P/S), keys read via the BIOS buffer
  (0x41A/0x41C) + INT 16h AH=10h, out_byte, MPU detect/UART. /X = dry run (no MPU; for timing the drawing alone).
- CLOCK: TSC is the time base (uclock() under SBEMU was non-monotonic and slow; the BIOS tick froze while IRQ0 was held
  off -> 519 ms stall). now_us(): TSC rate calibrated against the BIOS tick over ~1 s windows; a big rate change needs
  two agreeing windows (SpeedStep); torn 32-bit tick reads and midnight are ignored (a torn read once looked like
  +24 h and skipped songs instantly).
- Engine struct P: speed 25-400 (set_speed keeps the song position), mp_pump (late stats), mp_seek replays program/
  controllers, mp_start sends the reset (GS/GM/none) + 250 ms, mp_finished = end + 1.2 s tail. g_mute/g_solo +
  audible(): note-ons of muted channels are not sent (timing unchanged).
- MUSIC DURING DRAWING: tui.h tui_hook / m3core.h m3_hook (= mp_pump) are called per text row, per scan line in the
  VESA text renderer, in retrace waits, every 8 rows in the 3D rasterizer/present and every 16 in sky/upscale.
  NOT inside lcd_query (would interleave with SBEMU's MPU display report).
- Frame pacing by cost: text frames <= 25% CPU (>= 40 ms apart), 3D frames start at most every 2.5 frame-times.
- tui.h: tui_grow_span() draws only changed cells in the VESA text modes; tui_gline[] per glyph line (0 empty, 1 full,
  2 mixed) -> memset for empty/full lines. Affects every tui.h program (all rebuilt; MENU checked in G640x480).
- 3D: m3_vbe_probe() caches the VBE mode lookup (done before the music starts), the mode is set with 0x8000 (no
  clear), mp3_prepare() pre-builds buffers/textures, the module face texture is cached (mp3_face). Always full
  640x480 (auto half resolution removed after the "blurry" report); F4 toggles half by hand; mp3_slow (> 200 ms
  frames) shows the hint.
- Pages: Player / Playlist / Files / Options / Help, none blocks; songs auto-advance. g_back only from non-modal pages.
  Playlist: type-to-find (2 s), Enter, Del, Ctrl+Up/Down, F5 sort (file/title/length/shuffle), Ctrl+S/O .M3U, Ins,
  mouse; titles/lengths filled in the background by pl_info_step only when the next event is >= 20 ms away.
  Files: drives from INT 21h 4408h (no disk access) listed at the end, .M3U entries, "Selected" info panel, Ins mark,
  Ctrl+A add folder, Ctrl+P play folder, opens in the current song's folder. Options: 10 items -> MIDPLAY.CFG (VIEW
  REPEAT SHUFFLE RESET START GAP SPEED DIR LAST POS). MIDPLAY.M3U saved on exit; START=1 resumes song + position.
  Paths go through dosify() (upper case, backslashes).
- Fade-in of the Groovy palette is done in ui_frame (tui_key, which used to do it, is no longer used).
Test switches (not in README): /T:file stats log, /Q:sec quit after, /V:n start view, /X dry. HOSTTEST build:
  gcc -O2 -DHOSTTEST -o /tmp/mphost tools/midplay.c   (virtual clock; 200% speed takes exactly half the time)
Test rig: testrig/midplay/genmid.py (STRESS.MID / DENSE.MID). 86Box P2B-LS PII-450 dynarec, STRESS.MID:
  before: text 2.7 ms avg / 15 ms max late with SBEMU, 3D 84 / 252 ms.  after (SBEMU): table 0.87/2.7, panel 0.73/4.8,
  G640x480 0.67/2.5, 3D 1.0/6.5, switch to 3D mid-song 0.93/7.8 ms. Dry: every view in VGA80x25/80x50/G640x480/
  G1024x768 has 0 late events. Release zip re-checked: 0.79/2.6, 0.84/5.4, 0.95/5.0 (3D at 41 frames in 30 s).
Zip = beta.82 zip + MIDPLAY, SBEMU (version), SC55MENU, SBSET, GAMETEST, JOYTEST, KMTEST, MPUTEST, SBDIAG, TIMETEST,
  USBTEST, CDSET\CDGUI (all tui.h) + README.TXT (section 8 rewritten).
To test on real PCs: MIDPLAY with the SC-55 in 80x25, 80x50, 640x480 (F4) and 3D (S); playlist sorting/saving;
  quit and restart with Options > Start = last playlist; mouse on the time bar; P5PE with the NEC USB card.

## beta.84 (2026-11-10): SBEMU did not save the program's FPU state (RELEASED, tag v1.0.0-beta.84-usb)
Found while retaking MIDPLAY screenshots: pure-black horizontal lines across MIDPLAY's 3D view, only with SBEMU loaded
(/X dry run: 0 of 12 frames; with SBEMU: 6-10 broken rows in every frame). Pure black = light value 0 = garbage floats.
Not a z-buffer problem (the rasterizer has one); a host build of the same scene (5406 frames) was clean too.
CAUSE: x87 code run from SBEMU's interrupt and port traps without fnsave. Found with a call-graph scan of an
unstripped LTO link (objdump of /tmp/sbsym.exe: the makefile link line without -s; script approach: per-function FP
opcodes + calls, stop at functions that fnsave): ISR -> VPCSPEAKER_GenSamples (doubles every interrupt),
AU_setmixer_one (CD volume), USB pad paths, printf error paths; port traps -> SBEMU_DSP_Write (fcoms),
DBOPL Chip::WriteReg, VJOY axis (port 201h), printf/debug paths. The synth renders already saved (fpu_buffer).
x87 has 8 registers: a program with most in use (3D inner loops at -march=i386) lost values to stack overflow.
FIX: MAIN_FPU_ENTER/LEAVE around the whole card-IRQ service in MAIN_InterruptPM and MAIN_InterruptRM (incl.
TimerReplay and CDService); fnsave + frstor around every handler call in HDPMIPT_TrapHandler (PM) and the QEMM RM
dispatcher. Buffers are on the stack (nesting-safe). After fnsave (which resets the FPU) fldcw reloads the program's
control word, so a game's own IRQ handler called from SBEMU rounds as the game set it. vmpu.c (SoundFont) fpu_buffer
static -> per call (VMPU_FPU_BUF), so the ISR saving during a trap can no longer overwrite the trap's saved state.
Measured: 3D view 12/12 frames clean; STRESS.MID timing 0.88/3.9, 0.91/5.7, 0.91/5.3 ms (beta.83: 0.79/2.6, 0.84/5.4,
0.95/5.0) = noise. Cost ~250 cycles per trapped port access / interrupt.
Screenshots retaken (docs/shots/midplay-*.png; playlist/files/options new) with testrig-style 86Box automation:
/home/claude/mptest/boot.sh ROOT VM boots without waiting; a.py "screenshot path", "key down 0x0f"... (TAB 0x0f,
F2 0x3c, Esc 0x01, Enter 0x1c, Down 0x150), "@type:sp".
Zip = beta.83 zip + SBEMU.EXE + README.TXT (troubleshooting entry "3D GAMES WITH GLITCHES ONLY WHILE SBEMU RUNS").
To test on real PCs: a 3D DOS game (Quake, Descent, Duke 3D) with SBEMU + SB digital sound + pad: look for glitches
that used to happen only with SBEMU; MIDPLAY 3D view.

## beta.85 (2026-11-10): MIDPLAY 3D ~3x more frames, fast floor, playlist columns (RELEASED, tag v1.0.0-beta.85-usb)
User video from the D810 (beta.84): no more black lines, LOAD 48% (SC-55), 3D at ~5-6 fps ("running slow").
- midplay3d.h pacing: was "start a frame at most every 2.5 frame-times" (drawing <= 40% CPU, from before the music was
  sent from inside the drawing) -> back to back, cap 60/s (0.016 s; user: "let the PC draw up to the VESA framerate").
- m3core.h m3_floor_rows(): opaque floor (alpha 256) drawn per screen row - no roll, so a flat floor has one depth per
  row: one divide, one fog value, exact linear u/v (16.16, wrapped 128x128), x/z clipped to -12..12. Host check vs
  the 4-quad path over 300 camera positions: avg 1.2 levels difference on ~27k px (fog rounding), ~50 edge px/frame;
  0.71 -> 0.27 ms on the host. Mirror pass (alpha < 256, menus) keeps the quads. Used by MENU/SETTINGS/CDGUI/GAMETEST 3D.
- 86Box PII-450 + SBEMU + STRESS.MID, 3D: 41 -> 140 frames / 30 s, draw 296 -> 216 ms, late max 6.5 -> 8.4 ms.
- User asked "flat black background at 60 fps?": sky + floor now ~30% of a frame; kept the look.
- Playlist: File column first (8), Title second (22, empty when the file has none) - user request.
- 48 kHz "garbled" on the D810 (22 kHz fine): NOT CPU - SBDIAG in 86Box: ISR 59% at 22050 vs 60% at 48000 (STRESS.MID).
  Suspect the ICH AC'97 48000 path (sc_ich.c: VRA left off when freq_set == 48000; freq_card = 48000 / clock corrector
  from snd_intel_measure_ac97_clock). Asked the user for SBLOG.TXT (codec id + "AC97 clock check" line) at 48000 and
  what it sounds like. 86Box's ES1371 does not take that path; QEMU (ICH AC'97) not installed here.
Zip = beta.84 zip + MIDPLAY, SC55MENU, SBSET, GAMETEST, CDSET\CDGUI (m3core), SBEMU (version) + README.TXT.

## beta.86 (2026-11-10): MENU 3D speed + version on screen (RELEASED, tag v1.0.0-beta.86-usb)
User: "make the sc55 menu faster and better looking quickly" (session ran out of usage).
- DONE (built, not tested in 86Box, not in a zip): menu3d.h Panels background uses integer math per pixel (was a
  float->int conversion per pixel, 307200 a frame, slow in -march=i386 x87 code).
- NEXT: (1) test Panels in 86Box (MENU3D.CFG STYLE=0 makes SC55MENU start in 3D; M3PROF=1 env logs fps to
  C:\M3PROF.TXT), compare fps with beta.85; (2) Carousel's mirror floor (m3_draw_floor alpha 160) still uses the
  4-quad path: give m3_floor_rows an alpha (tex m3_floor, blend, no z-write) for it; (3) "better looking": ask user
  what they want; (4) release as beta.86 (zip = beta.85 + SC55MENU + SBEMU version).
- Also open: 48 kHz garbled on the D810 (see beta.85 section).

- 2026-11-10 17:01 user: the D810 48 kHz garbling "went away after a restart" - closed unless it comes back (if so: SBLOG.TXT at 48000, AC97 clock check line).

beta.86 done: Panels background integer math; m3_floor_rows(y, alpha) also does the alpha < 256 mirror floor (m3_floor
texture, blend, z test, no z write; host: 1.12 -> 0.46 ms, same picture as the quads). SC55MENU header line 5 shows the
version (makefile passes -DMENU_VER=$(VERSION); "beta.NN" cut out, yellow at column 44). 86Box PII-450, QUALITY=1 (full),
M3PROF fps: Panels 8.5 -> 10.1, Carousel 8.6 -> 11.4. Test script /tmp/menufps.sh (root4 copy + TUNED.FLG so it is not
a first run + MENU3D.CFG STYLE=n, Esc twice + Enter to quit, M3PROF.TXT copied to DIR.TXT).
Zip = beta.85 zip + SC55MENU, SBSET, GAMETEST, CDGUI, MIDPLAY (m3core), SBEMU (version).
Still open: "better looking" menu (ask the user what they want).

## beta.87 (2026-11-10): smoother big text, menu footer fix (RELEASED, tag v1.0.0-beta.87-usb)
User asked for "sharper text, new colours, a cleaner layout, nicer 3D styles - all of this".
- m3core m3_text: fonts 1-3 (10x20, 12x24 bold, 16x32 bold) get inside-corner smoothing: empty pixel with a set pixel
  left/right AND above/below drawn at ~40% (bit ops on prev/cur/next glyph rows). Font 0 (8x16) unchanged.
- menu3d m3_overlay: description box 354..472, title 360, 3 lines from 384, divider at 449, keys at 452 (the 3rd
  description line used to overlap the key line in Carousel/Module).
- Screenshot script /tmp/menushot.sh EXE STYLE|t TAG [scancodes] -> /tmp/ms_TAG.png (root4 copy).
- NEXT for the look: new colours (e.g. 3D styles following the text menu's 7 colour styles), nicer 3D styles; ask the
  user which they like (mockups), the text menu already looks good.

## beta.88 (2026-11-10): 3D look - colour styles, neon glow, fuller Carousel, nicer Module (RELEASED, tag v1.0.0-beta.88-usb)
User picked all four: match text-menu colours, brighter 90s look, fuller Carousel, nicer Module.
- m3core: scene colours are variables (m3_sky0/1, m3_gridc, m3_tile0/1, m3_floorbg, m3_fogc, m3_glowc, m3_accent[6]);
  m3_theme_from_pal(tui_palette()) maps a text style's 16 colours (1 desk, 5 highlight, 11 grid/glow, 9/13/10/12/14/11
  accents) and rebuilds the floor (now with a soft glow edge on the grid lines); m3_theme_gen counts changes;
  m3_theme_sync() re-themes when tui_theme changed (menu loop every frame, MIDPLAY mp3_draw, form3d/menu open).
- menu3d: m3_catcol = m3_accent; Panels waves via a 64-entry LUT from the theme (faster: 10.1 -> 11.2 fps); title
  gradient from the theme; F6 cycles tui_theme (skips Classic) and saves THEME.CFG; corner note shows the style name.
- m3_text_glow(): 12 faint offset copies in the glow colour + the text; used for the big titles, the selected Panels
  item, the description title, Carousel count/arrows.
- Carousel: a glowing floor marker per ring slot (min 7), "si / n" under the card, < > when n > 1.
- Module: brushed metal panel texture, LCD backlight halo, m3_knob3d machined (angle-shaded cap, knurled rim,
  orange line pointer) - MIDPLAY's knob too.
- fps (86Box PII-450 full res): Panels 11.2, Carousel 11.5, Module 11.7 (beta.86: 10.1 / 11.4 / -).
NEXT (user agreed): optimization pass - (1) SC-55 emulation hot spots, (2) build the SC-55 parts for i686 (CMOV;
needs a PPro/PII anyway) and measure, then (3) MMX inner loops for the 3D renderer, (4) profile SoundFont, SB
resampler, USB polling.

## beta.89 (2026-11-10): SC-55 core profile-guided build (RELEASED, tag v1.0.0-beta.89-usb)
- makefile SC55PGO: gen (-fprofile-generate), use (default when sc55/pgo/*.gcda exist: -fprofile-use=sc55/pgo
  -fprofile-prefix-path=$(CURDIR) -fprofile-correction -fprofile-partial-training, missing/mismatch warnings off), off.
- Profiles: instrumented SC55WAV (DJGPP GCC 12, same flags) run in 86Box with GCOV_PREFIX=C:/PGO,
  GCOV_PREFIX_STRIP=20 -> 8.3 files -> renamed to output#upstream#Nuked-SC55#src#backend#<name>.gcda. Recipe:
  testrig/sc55bench/pgo.sh. Redo after changing the core (a changed file silently builds without its profile).
- 86Box PII-450 mk1-v2.00 SC55WAV: 18000 -> 20456 frames/s (+13.6%), OUT.WAV md5 7355ccee606d0d3332ef52ed66663adf both.
  SBEMU live (STRESS.MID, SBDIAG): SC-55 needs 340% -> 302%; MIDI timing unchanged (max 2.6 ms late).
- Host bench (testrig/sc55bench): PGO +8-10% on x86-64, same hash.
- Queue after this (user): Blood sound broken in Windows 98 (reported with beta.85; game files in user upload
  BLOOD.zip: SB16 A220 I7 D1 H5, 44 kHz 16-bit stereo, FX and music on SB/OPL); MMX 3D renderer; Win98 DOS-box
  performance, Windows bug sweep, DOS/Windows feature parity.

## beta.89-w98.5 (2026-11-10): Blood's sound in Windows 98 (RELEASED, tag v1.0.0-beta.89-w98.5-usb)
User: "Blood's sound is broken in Windows" (beta.85; game = user upload BLOOD.zip, BLOOD.CFG SB16 type 6, 16-bit
stereo 44000, I7 D1 H5, FX + music on SB/OPL).
- Rig: 86Box Win98 VM /home/claude/padtest/w86 (P2B-LS PII-450 dynarec, ESS Solo-1; its legacy SB holds 220/388/330 and
  DMA 1 -> tests used /A:240 /I:7 /D:3 /H:7 /T:6 /P:300 and BLOOD.CFG to match; FM 388 stays on the ESS). D: disk
  built from /home/claude/w98blood (BLOOD\, SET240*.BAT, UPD.BAT, INIRESET.BAT, BUF300.BAT, TRON.BAT); /tmp/w86run.sh
  types into Start > Run (Win+R = key 0x15b + 0x13). 86Box "audio start DIR" records main.wav (the card's PCM);
  /tmp/gaps.py measures silent gaps. SC55W for the VM: ARCH="-march=pentium2 -O2" OBJ=build98p2 LIB=b98p2.
- Measured gaps (share of the time between first and last sound): SB16 16-bit 49-56% (median gap 80 ms, ~40 ms of
  sound in every ~120 ms), SB Pro 8-bit 5%. SC55W.LOG: resyncs 307 in 30 s.
- Cause: not DMA - a full-screen DOS VM gets most of the time slices (WinTimeSlice 100,50), SC55W (system VM) runs in
  short turns and the 80 ms waveOut buffer runs dry between them; pcm_render then also dropped the backlog
  (avail > 4 x target). BufferMs=300 by hand: 7% gaps (= the game's own pauses).
- Fix (helper only, VxD unchanged): w98main.cpp BufferAuto (default 1): while sh->dma_active (or < 1 s ago) and >= 3
  underruns within 3 s, buffer 80 -> 160 -> 300 ms (wave reopened, logged); back to BufferMs after 60 s without DMA.
  engine pcm_slack_ms: the drop threshold is also >= 1.5 x the buffer. Result: 1% gaps, no settings.
- Also new: SC55W.INI TraceFile=1 -> SC55W.TRC (VxD trace ring every 100 ms, file reopened each time - Windows only
  updates a file's size on close). NOTE: while a DOS game runs full screen even SC55W.LOG writes may sit in the cache.
- Possible better fix later: let the VxD boost the system VM (Adjust_Exec_Priority / Call_Priority_VM_Event) while DOS
  DMA sound plays, for low latency without the bigger buffer.
Zip = beta.89 zip + FILES\WIN98\SC55W.EXE (Pentium M build), SOURCE.ZIP, README98.TXT (x2).

## beta.90-w98.6 (2026-11-10): Windows parity - LCD panel in DOS boxes, Pentium II/III build, MIDPLAY clock in Windows
User picked (Windows-vs-DOS gaps): SC-55 LCD ("just do it in a DOS box"), virtual CD + CD music ("DOS box" approach:
uCDD/SHSUCDX inside the DOS box; user: the virtual CD "doesn't work with games properly" - ask which games), PII build.
- LCD: SC55W_SHARED gets lcd_seq + lcd[152] at the END (NUKEDJOY's wjoy offset unchanged; it checks no size/version);
  SC55W_IF_VERSION 8, size check 529560. Helper: Sc55 attaches a null LCD_Backend (the core only keeps LCD_Data with a
  backend, as sc55synth.cpp in DOS), MidiSynth::lcd()/kind() (SC-55 LCD RAM + CG, MT-32 getDisplayState, SF2 kind 1);
  audio loop publishes the SBEMU B0h packet every 100 ms ('L', on, kind, load permille, shed, 80 + 64 bytes).
  VxD mpu_write: B0h copies sh->lcd into lcd_pkt, data port returns its 150 bytes, status 3Fh while bytes wait.
  Tested in the 86Box Win98 VM: MIDPLAY /P:300 /V:1 in a DOS box shows the live panel ("the module's real display").
- PII: win98/buildall.sh builds SC55WM.EXE (pentium-m SSE2), SC55W2.EXE (pentium2, build98p2/b98p2 objects) and
  SC55W.EXE = helper/launch.c (i386, CPUID bit 26 -> starts the right one with the same command line, waits, returns
  its exit code). build98.sh takes EXE= and OD=. install98 copies/removes both builds; the Run key gets the launcher.
  Tested: the PII VM runs SC55W2.EXE through the launcher.
- MIDPLAY clock in a Windows DOS box: clock_init timed 3 BIOS ticks; Windows delivers ticks late and in bursts ->
  per_us tiny -> the song ran to its end at once and hung there. Now INT 2Fh AX=1600h (enhanced mode) -> 18 ticks,
  retried while not 20 MHz..10 GHz. VM: 22.9 s emulated = 23 s of song.
- NOTE: "MPU 330h" in the first test = the ESS card's own MPU (mpu_find); with /P:300 our MPU (correct).
Zip = beta.89-w98.5 zip + WIN98\SC55W.EXE (launcher), SC55WM.EXE, SC55W2.EXE, SC55W.VXD, SOURCE.ZIP (+launch.c,
buildall.sh), README98.TXT (x2); DOS MIDPLAY.EXE, SBEMU.EXE, SC55MENU.EXE (version beta.90).
NEXT: virtual CD in a DOS box under Windows (+ CD music via the VxD) and the virtual-CD-with-games problems.

## beta.91 (2026-11-10): MENU game profiles with a CD image mounted over the old disc (RELEASED, tag v1.0.0-beta.91-usb)
User: games from the virtual CD give "no CD found", read errors or init errors.
- Found: MENUGO.BAT (sc55menu.c write_go, game profile with a CD image) ran `UCDD -mount` without `-unmount`; uCDD has
  one drive -> "No uCDD drive is free", errorlevel 1, M: keeps the OLD disc; MENU printed the error, PAUSE, then
  started the game anyway (wrong disc). Reproduced in 86Box (rcd root: SUPER.ISO then TISO.ISO). Fixed: -unmount
  first; on failure the game is not started (GOTO GOEND). CDIMG.BAT and CDGUI already unmounted first.
- Checked OK in 86Box (rcd: root0 + C:\CDSET + C:\CDIMAGES, LASTDRIVE=Z): user's Supergames_1.iso on M:, with
  SBEMU + SC-55 loaded: COPY of GAMES\DOOM\DOOM1.WAD bit-identical, DOOM1 -timedemo demo1 from M: completes.
- Asked the user for game names, error text (photo), launch path, ISO or CUE, image location, version. Other
  candidates: games installed from a real CD at D: that look for D:; running a game from its installed folder.
- User wants "Super Shareware Collection.iso" (archive.org) tested; the session has no network: ask for an upload.
- Red Baron 3D (user upload, /home/claude/games/rb3d/RedBaron3D.iso) is a WINDOWS game: the "uCDD in a DOS box"
  plan gives a drive only to DOS programs; Windows games need a Windows 98 virtual CD driver (bigger job).
- Later (network now on): user's "Super Shareware Collection.iso" (archive.org, 485 MB, 1994 SIMTEL/BBS ZIP CD) on M:
  with SBEMU + SC-55 loaded (86Box): DIR M:\GAMES = all 284 entries (7-sector directory), COPY M:\GAMES\D*.ZIP and
  W*.ZIP = 26 files bit-identical to the image, GO.COM (a TheDraw screen that exits) OK. Test disk 700 MB:
  testrig/mkdisk.py MKDISK_SPC=32 (16 KB clusters), /tmp/runbig.sh = cdtest/run.sh with DISKMB=/CYL= (1422 cyl).
  No read problem found outside the MENU disc-swap bug: waiting for the user's game names / error photos.

## beta.91-w98.7 (2026-11-10): Windows 98 -> DOS transition review (RELEASED, tag v1.0.0-beta.91-w98.7-usb)
User: bug-hunt Win98 -> DOS ("Restart in MS-DOS mode", Shut Down -> MS-DOS, full-screen DOS box) with the pack installed.
- NOT tested in 86Box: the Win98 VM image (user's QuickInstall FULL.866 / qi.iso) and the 86Box build are not in the
  repo, so this session had no Win98 rig. Code review only + DJGPP build (andrewwutw build-djgpp v3.4 gcc 12.2.0).
- Found + fixed: SBEMU in a Windows DOS box (SC55.BAT run there). It had no Windows check: HDPMI32I does not install
  under Windows' DPMI, so it ended with "Both real mode & protected mode support are disabled" (confusing); with a DPMI
  host that lets it through it would program the PCI card under the Windows driver. main.c now checks INT 2Fh AX=1600h
  before DPMI_Init: AL not 0/80h -> yellow message (SC55W gives DOS boxes the sound; use Restart in MS-DOS mode), exit 1.
  SET SBEMU_IN_WINDOWS=1 skips the check. In MS-DOS mode Windows is unloaded (AL=0), so nothing changes there.
- Checked, no change: VxD unload path (shutdown_all -> IOCTL DETACH -> CloseHandle, FILE_FLAG_DELETE_ON_CLOSE ->
  Sys_Dynamic_Device_Exit -> detach + Cancel_Time_Out): every trap removed (SB 16 ports, 388, MPU, IRQ, DMA 8/16,
  joy port, INT 15h/21h hooks, min timer period); sc_tick after DETACH is safe (xf.active=0, sh checked). WM_ENDSESSION
  reaches the hidden top-level window and sets [State] Step=ok (no false "did not finish starting" next boot).
  AUTOEXEC.BAT: only the REM [SC55W] + SET BLASTER pair, never added over a user BLASTER line. Nothing touches
  CONFIG.SYS or DOSSTART.BAT. The VxD never programs the real card (ESS Solo-1 legacy 220/388/330 stays the Windows
  driver's; SBEMU has no ESS Solo-1 driver, so on the 86Box Solo-1 VM DOS-mode SBEMU stops at "No usable sound card").
- Minor, left: the joystick thread is not joined in shutdown_all (it only gets failing DeviceIoControl calls after
  the handle closes; the process ends anyway).
- README98: SBEMU does not load in a DOS box; one line for C:\WINDOWS\DOSSTART.BAT (CALL C:\SC55\SC55.BAT).
Zip = beta.91 zip + FILES\SC55\SBEMU.EXE, README98.TXT (x2). VERSION unchanged (beta.91).
NEXT: test on real PCs; with the user's Win98 image rebuild the 86Box VM and run the three switch paths.
## SC-55 SSE2 PCM_Update experiment (2026-11-11, dropped, no code change)
- Tried: split the voice loop into 3 passes (address/ROM/DPCM/interp/IRQ/envelopes in slot order; filter + volume +
  pan + sends for all voices, 4 at a time with SSE2 PMADDWD; mix chain in slot order), CPUID pick, C fallback.
  Bit-exact on both paths (sc55bench mk1-v2.00, 19.mid, 30 s: hash 1317ac59).
- Result: slower. Callgrind 4 s: original 5.06 G Ir, SSE2 5.27 G, C split 5.45 G. Wall (best of 5): 2.30 / 2.50 / 2.66 s.
  The vectorizable stage is only ~5.7% of all time (287M Ir scalar -> 106M SSE2), so the ceiling is ~3.6%, and the
  split costs ~390M Ir. P-M/P4 run 128-bit SSE2 integer ops as 2x64, so the real gain would be even smaller.
- The rest of PCM_Update (address generator, ROM reads, per-voice variable shifts) does not map to SSE2.
- DOS note for any future SIMD: the SC-55 renders in the sound ISR with fnsave (vmpu_sc55.c), which does not save
  XMM; SSE needs a CR4.OSFXSR check (e.g. probe under SIGILL) and fxsave/fxrstor around the render.
## USB disks, session 1 (2026-11-11, DOS session, not released)
- Plan and status: docs/USB-STORAGE.md (section 8). Code in sbemu/usbpad.c ("USB disks" block), API in usbpad.h
  (USBPAD_F_DISK, USBPAD_Disks/DiskInfo/DiskRead). SBEMU does not use it yet (no flag passed); USBTEST does.
- Other changes for it: OHCI reset now puts 4 skipped bulk EDs on the bulk list and sets BLE; UHCI u_run_tds
  can stop at a short packet (UShort, bulk only); EHCI e_ch sets the control-endpoint flag only for endpoint 0.
- Test: devkit 86Box (user's sc55-devkit tar: djgpp + 86Box with usb_msd), testrig/86box/msdtest.sh.
  UHCI and OHCI pass (CRC of the first MB). Real hardware not tried: needs USBTEST on the D810 (UHCI) and the
  P5PE-VM NEC card (OHCI) with a stick, plus a check that the pads still work there.
- Session 2 start (same day): pads keep working during disk requests (MsdBusy -> light step from the interrupt,
  untested); OHCI data + CSW in one run and no fixed per-transfer wait (86Box OHCI 1 MB 2.6 s -> 1.8 s).

## SC-55 PCM_Update C speed-up + new PGO profiles (2026-11-11)
- pcm.cpp (bit-exact, sc55bench mk1-v2.00 19.mid 30 s hash 1317ac59): address generator steps 3-4 (samp3, nibble_cmp5,
  4th address step) only run when sub_phase_of >= 3; calc_tv's addlow/write come from pcm.tv_addlow/tv_write[type & 7],
  filled once per sample from tv_counter (4-bit reverse table); addclip20b / plain products where the operand provably
  fits 20 bits (DPCM deltas, interpolation steps, eram_unpack results). Tried and reverted: a 512-entry table for
  calc_tv's type (more instructions).
- Host callgrind 4 s: 5.06 G -> 4.85 G Ir (-4.1%).
- sc55/pgo/*.gcda regenerated (pgo.sh steps 1-3) in 86Box P2B-LS PII-400 booting MS-DOS 7.1 (IO.SYS/MSDOS.SYS/COMMAND.COM
  from the user's MS-DOS 7.1 CD, BOOT/DOS71INS.IMG; testrig/mkdisk.py MKDISK_BOOT=msdos, needs testrig/rufus).
- DOS SC55WAV same VM, old vs new, alternating x2: 19475/19509 -> 20457/20457 frames/s (+4.9%), WAV md5 8cf65a57 all four.
  (This host's 86Box is not comparable with the earlier session's figures.)
- Next ideas (other half of the time is the H8 MCU): TIMER_AdvanceSel 10%, MCU_Step 9%, MCU_Read 7% (page switch per
  read -> table). Redo the profile after any core change.

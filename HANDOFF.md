## WIP beta.120 (2026-11-14, DOS session, force-stopped): P5PE-VM freeze = ICH5 SATA "Enhanced" mode (user)
- User: the P5PE-VM froze with SC55 in any case while the BIOS had SATA on "Enhanced"; "Compatible" fixed it.
- Likely mechanism: Enhanced = SATA in native PCI mode (PCI IRQ shared with the Audigy/USB). The EMU10K irq_routine
  (snd_emu_10kx_isr / snd_p16v_isr) returned non-zero for ANY IPR bit (MIDI bits nearly always set), so SBEMU
  claimed every shared IRQ, sent EOI and never chained -> the BIOS disk wait hung.
- Done on wip/sata-shared-irq-2026-11-14 (built, NOT tested): sc_sbliv.c both ISRs ack/return only
  IPR_INTERVALTIMER|IPR_SAMPLERATETRACKER (the INTE bits SBEMU enables); main.c MAIN_ScanDiskIrqs + SBDETAIL disk
  controller lines + yellow warning when a native-mode IDE/SATA controller shares the card IRQ; makefile beta.120.
- Next 3 steps: 1) QEMU pm.img (testrig/menustat/mkimg.sh) plain and with `-- -device ich9-ahci`: SBDETAIL
  "disk controller" line, warning when shared, MPUTEST + DIR still work; menustat matrix. 2) README.TXT troubleshooting
  (SATA Compatible mode, warning text) + README.md news; rezip.py beta.119 zip -> beta.120, release. 3) Ask the user to
  retest Enhanced mode on beta.120 (should no longer freeze) and send SBDETAIL.TXT.
- Resume: git pull; git checkout wip/sata-shared-irq-2026-11-14; DJGPP: curl build-djgpp v3.4 linux64 gcc1220 to
  /tmp/dl; sh testrig/common/restore.sh dos71; make in sc55-pack.

## beta.119 (2026-11-14, DOS session): beta.118 froze the ASUS P5PE-VM + Audigy (Win98 -> MS-DOS mode); beta.112 fine
- Suspect: beta.115 MAIN_CardWatchdog (main.c) calls aui.card_handler->irq_routine from SB port traps and INT 1Ch,
  outside the interrupt, for every card; proven only on ES1371 (86Box). Not reproducible here (no emulator has an
  EMU10K); root cause on the board unconfirmed until P5TEST results come back.
- irq_routine audit (outside the ISR, called with CLIS, MAIN_InINT guard): ES1371 safe (status read + toggle of the
  DAC int enable). ES1370/VIA82xx: ack-only, probably safe, unproven. EMU10K Live/Audigy (snd_emu_10kx_isr /
  snd_p16v_isr): acks ALL IPR/IPR2 bits incl. MIDI/UART and returns non-zero for any -> "pending" almost always, can
  eat the channel-loop IRQ the ISR then sees as foreign. ICH AC'97: advances LVI (DMA state). CMI: reads the MPU data
  port (eats MIDI IN) and rewrites HLDCLR. HDA: acks RIRBSTS under a running codec verb. -> unsafe.
- Fix: /CARDWD:0|1|2 (default 2 = auto: on only for "Ensoniq/SBPCI"); off = no trap work and NO INT 1Ch hook (the
  beta.115 RM->PM callback from IRQ0 was the other new start-up/IRQ-time change). SBDETAIL: "Sound card watchdog:
  ACTIVE/off (card X, /CARDWD:n ...)", trace event. New [step] lines: card start, CD-Audio link + INT 28h, flight
  recorder, watchdog INT 1Ch hook (finer start-up step log, each fsync'd).
- Other beta.113-118 SBEMU start-up/IRQ changes checked: uCDD -install + /VCDA:100 by default (beta.115: CDLINK,
  INT 28h hook, MAIN_CDService after every card IRQ) = suspect 2; SC55RUN.BAT honours SET SBNOCDA=1 to skip it.
  beta.114 USB-MIDI watchdog runs only with a USB MIDI device; beta.116 packver constructor (argv scan) and beta.118
  AMIS fn 04h run only when asked: not suspects. beta.113 = MENU only.
- Bisect kit in the zip: C:\SC55\P5TEST.BAT 1-4 (1 shipped/auto, 2 /CARDWD:1, 3 /CARDWD:1 + SBNOCDA, 4 /CARDWD:0 +
  SBNOCDA = beta.112 start-up); saves SBDETAIL+SBTRACE of the previous run to P5T<N>.LOG.
- Tested (QEMU store-dos71 HIMEM-only, AC97): menustat matrix none/pm/pmhand/rm/rmonly PASS (both SBEMU modes);
  default -> "off (ICH AC97, auto: not proven)", /CARDWD:1 -> ACTIVE + INT 1Ch, /CARDWD:0 off; P5TEST 2/3 logs OK.
  Not run: 86Box ES1371 (auto ACTIVE path; same code as beta.118 there) and the PII-450 speed check (change only
  removes work). testrig/common/rezip.py now also swaps/adds pack/FILES/SC55 files.
- OPEN: P5PE-VM results of P5TEST 1-4 (where it froze, P5T*.LOG). If 1 still freezes -> not the watchdog: check
  SBNOCDA (test 4), then MENU (beta.113).
- Test downloads: DJGPP v3.4 gcc12.2 (/tmp/dl/djgpp).

## beta.118 (2026-11-14, DOS session): MENU said "Not loaded" with SBEMU loaded (user, beta.116 personal pack, P5PE-VM)
- Cause class: MENU knew SBEMU only by an MPU-401 reset probe, which answers only when SBEMU's port trap reaches the
  MENU process (HDPMI trap for PM, QPIEMU for RM). Any setup where it doesn't (Protected mode off = /PM0, a different
  DPMI host, a port/trap problem on the real board) showed "Not loaded". Reproduced: SC55JEM /PM0 -> MPU no answer.
  NOT reproduced on the P5PE-VM's own path (MS-DOS 7.1 HIMEM only + SC55.BAT): QEMU AC97 answers at 330. Root cause on
  that board still open: get BUGREP.TXT "MENU status" line from it. beta.113-116 changes (uCDD -install, INT 1Ch
  watchdog, /VCDA, 3D menus) checked: none touch the MPU trap or the probe.
- Fix: SBEMU AMIS (INT 2Dh) function 04h = "VER= MODE=RM+PM MPU= TRAP= SB=" (main.c); tools/sbemuchk.h: AMIS
  signature + 04h, then the MPU probe; Loaded if either, "Unknown" if the AMIS scan failed or a Windows DOS box,
  else Not loaded. MENU text + 3D status, "MPU ?" when found by AMIS only, `SC55MENU [home] /STATUS` (exit 0/1/2),
  BUGREP.BAT "MENU status" block, README.TXT note. No runtime cost (AMIS handler runs only when called).
- Matrix: sc55-pack/testrig/menustat/run.sh ZIP [EXE..] (QEMU, store-dos71): none / pm / pmhand / rm / rmonly, all
  PASS on the beta.118 zip. Not run: Win98 DOS box (store-w98qi-qemu has no automation for a DOS box yet), FreeDOS.
- Real hardware: P5PE-VM + D810: SC55 then MENU -> status panel "Loaded"; run BUGREP, send the "MENU status" line.

## beta.117 (2026-11-14, DOS session): uCDD disc hotkey "stopped after a re-mount" = SHSUCDX 7 s media check; MENU Panels reason box
- Root cause of the beta.115 OPEN item: not uCDD and not the mount order. SHSUCDX asked the driver for a media change
  only after 128 ticks (~7 s) without an access to M:; the matrix swap step did DIR, Ctrl+Alt+PgDn, DIR within ~5 s,
  so the second DIR showed the old disc (the disc2 step had a longer gap and passed). uCDD had switched (cur 2 in a
  memory dump). Fix: third_party/shsucdx/shsucdx.nsm threshold 2 ticks (~0.1 s); README.TXT note. Real effect for
  users: a game's "insert disc 2" prompt answered right after the hotkey read the old disc.
- New fast rig sc55-pack/testrig/menumx/hotkey.py (QEMU, no MENU, ~40 s): GAMES.MDM -> hotkey -> RAP.CUE -> GAMES.MDM
  -> hotkey x2 -> GOT.CUE -> GAMES.MDM -> hotkey; DIR after each. Old SHSUCDX: every swap FAIL; new: PASS (HIMEMX and
  JEMMEX). QEMU needs `-global i8042.kbd-throttle=on` (else QEMU pops port 60h on read and a peeking INT 9 hook such
  as uCDD's sees every other scancode). menumx.py swap step is a real PASS/FAIL now (matrix case).
- MENU 3D Panels: a grey chosen row fits its label before the reason box (menu3d.h); tests/m3host m3menu.c 1b checks
  it at 640x480 (fails on the old code, M3SHOT=x.ppm dumps the frame).
- Not run: 86Box matrix (the store-86rig 86Box has no automation socket; rebuild per testrig/86box/README.md).
  SBEMU unchanged -> no speed measurement needed. Zip = beta.116 zip, rezip.py + SHSUCDX.COM swapped.
- Real hardware: on the D810, mount an .MDM in MENU, DIR M:, Ctrl+Alt+PgDn, DIR M: at once -> disc 2 shows.
- Test downloads: DJGPP v3.4 (/tmp/dl), HIMEMX from store-freedos (ibiblio blocked), rufus (testrig/rufus).

## beta.116 (2026-11-14, DOS session): /VER in every EXE (ported from wip/release-rc-2026-11-13)
- sc55-pack/tools/packver.c: a constructor in every EXE answers `NAME /VER` ("NAME.EXE: NUKED SB-EMU <VERSION>") and
  exits before main(); the makefile links output/packver.o into SBEMU and all 15 tools (packver.h/NSBVER stays for
  SBDIAG DOCTOR). BUGREP.BAT: "Program versions" block (each EXE /VER, CDGUI from C:\CDSET). README.TXT note.
- Tested (QEMU, store-qemu c.img, HDPMI32I -r): all 16 EXEs print 1.0.0-beta.116-usb-sc55. No runtime cost (argv
  scan once at start). Zip = beta.115 zip with the 16 EXEs + BUGREP.BAT + README.TXT swapped
  (sc55-pack/testrig/common/rezip.py OLD NEW, run from the repo root after make).
- Not ported (stale): the branch's HANDOFF/mkzip/menu-walk/UNINSTAL/CLEANUP changes. wip/release-rc-2026-11-13 and
  wip/menumatrix-2026-11-13 deleted.
- Real hardware: run BUGREP on the D810, check the "Program versions" block lists every EXE.
- Test downloads: DJGPP v3.4 gcc12.2 (/tmp/dl/djgpp).

## beta.115 (2026-11-14, DOS session): MENU matrix - CD music without HYBRID, sound-card IRQ watchdog, 24-run test matrix
- CD music: SC55RUN.BAT loads `UCDD.EXE -install` before SBEMU (no drive letter) and passes /VCDA:100 before @SBOPTS;
  SETTINGS "CD audio volume" default 100%, Off = /VCDA:0. Before, CD-Audio only worked with HYBRID.BAT; a disc mounted
  later from MENU was silent (SBEMU links to uCDD only at load). MENU/CDGUI/README.TXT texts updated.
- SBEMU watchdog (main.c MAIN_CardWatchdog, SB port traps + INT 1Ch hook): no card IRQ for 250 ms -> ack the card's
  pending interrupt. Found in 86Box (ES1371): a lost IRQ edge left STATUS FFFFFEC4 (DAC1 pending), PIC clean, and
  SBEMU silent until reboot, ~1 boot in 5 - God of Thunder then said "SoundBlaster ... BLASTER". Trace event "sound
  card interrupt was stuck". Speed (testrig/midi/perf.sh, PII-450): 344 int/s, 0.02 ms, 1% = beta.112; same with uCDD
  + /VCDA:100.
- MENU: the CD image / program picker shows its title + folder line again (header animation drew over it).
- Matrix (sc55-pack/testrig/menumx, 86Box P2B-LS PII-450 ES1371, FreeDOS + JEMMEX): FAT12/16/32 x ISO/CUE/MDM/long
  names x SC55JEM (real) / SC55 (protected): boot, mount, DIR, install (GOT INSTALL.BAT, Raptor Apogee INSTALL.EXE),
  play (sound level from 86Box), CD music, unmount, disc 2, disc swap. Table below. `python3 menumx.py all --jobs 2`.
- Findings that are not pack bugs: Apogee INSTALL.EXE renames RAPSW12.SHR -> .EXE in its own folder (fails from any
  read-only CD; the test disc ships the .EXE). uCDD applies a disc hotkey only while DOS is idle (fine in games).
- FIXED beta.117 (SHSUCDX 7 s media check): uCDD MDM hotkey stops switching after a single image and the set were mounted again (rig repro: mount
  GAMES.MDM, mount RAP.CUE, mount GAMES.MDM, Ctrl+Alt+PgDn + WAIT1 -> still disc 1). First swap after a mount works.
- Real hardware to check: CD music in a CUE game started from MENU (D810, P5PE-VM); SBTRACE.TXT "was stuck" lines.
- Matrix result (86Box, release candidate = beta.114 zip + these files; logs /home/claude/mx/all, not kept):
| case | real (SC55JEM) | protected (SC55) |
|---|---|---|
| fat12-cue | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat12-iso | PASS (n/a: cdmusic, swap) | PASS (n/a: game, cdmusic, swap) |
| fat12-lfn | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat12-mdm | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat16-cue | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat16-iso | PASS (n/a: cdmusic, swap) | PASS (n/a: game, cdmusic, swap) |
| fat16-lfn | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat16-mdm | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat32-cue | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat32-iso | PASS (n/a: cdmusic, swap) | PASS (n/a: game, cdmusic, swap) |
| fat32-lfn | PASS (n/a: swap) | PASS (n/a: game, swap) |
| fat32-mdm | PASS (n/a: swap) | PASS (n/a: game, swap) |
  swap n/a = the OPEN uCDD item above (disc 2 via Ctrl+Alt+PgDn passes in all 6 MDM runs); game n/a under SC55 =
  God of Thunder is real mode (no QPIEMU in protected-only mode, by design); cdmusic n/a = ISO has no audio tracks.
- Rig notes: 2 VMs on 2 cores drop/repeat keys -> verified typing (mxvm cmd, type_checked), sel() steers to the row,
  lost-Enter retries. QEMU (--vm qemu) fails menu navigation (SeaBIOS doubles grey arrows). ~11 min per pair.
- Probes (testrig/menumx/probes): SBIRQ (DSP 14 -> IRQ time), PIC (IMR/ISR), ESSTAT (ES1371 regs), DTRACE (INT 21h
  log; breaks MSC programs), OPENT/SEEKT (file API on M:), WAIT1, unlz.py (LZ91 unpacker), loop*.sh.
- Test downloads: HIMEMX (ibiblio FreeDOS 1.3 base/himemx.zip, mxdisk env HIMEMX), nasm (apt) for the probes.
- Resume (if anything is left): rebuild per docs/SESSION-2026-11-13-DOS.md, then `cd sc55-pack/testrig/menumx &&
  MX_ZIP=<zip> python3 menumx.py all --jobs 2 --resume --out /home/claude/mx/all`.

## beta.114 (2026-11-13, DOS session): M-Audio Uno froze the P5PE-VM - USB MIDI watchdog, [midi] step logs, real-Uno fake
- Report (user, beta.113): the Uno froze the ASUS P5PE-VM (first said D810) on every port, with any or no other USB
  hardware; boot = Win98 > MS-DOS 7.1, HIMEM only (no QEMM/JEMM), HDPMI32I + SC55.BAT. Which program / when: not given.
- Real Uno 0763:0150 rev 1.25 (usb-devices dump, xed.ch/help/midi.html): bcdUSB 1.00, ep0 64, Atr C0 0 mA, no serial,
  bulk IN 81h before bulk OUT 02h, 64 bytes; no Linux quirk except IN running-status CIN 4. Old MidiSport 0763:1010 etc.
  (EZ-USB firmware, vendor protocol) now refused by name. testrig/fakemidi.py default = that shape + UART pacing.
- QEMU does NOT reproduce (uhci, ohci, ehcicc, xhci; MS-DOS 7.1 HIMEM-only and FreeDOS JEMM). Audit found: USBTEST's
  note-off flush waited for ever (`while (USBPAD_MidiPending()) USBPAD_Poll();`) - a device that never takes OUT hung
  USBTEST; no OUT watchdog (a NAKing OUT stayed busy for ever, CLEAR_FEATURE retried without limit, all in SBEMU's
  sound interrupt); Shutdown flush read P[-1] when the device went away; trace not on disk until USBTEST's init ended.
  Descriptor walk, MidiByte, MPU trap, interrupts (all off), FIFO: already bounded.
- Fix (usbpad.c): OUT watchdog - not taken in 0.5 s: taken back (UHCI QH emptied, OHCI ED / EHCI QH restarted, toggle
  0), CLEAR_FEATURE; 3 in a row or 8 stalls in a row: both MIDI pipes stopped, "[midi] ... not used any more" - the PC
  keeps running, unplug/replug retries. [midi] lines: device/config summary, every descriptor, pipes, first OUT +
  done, timeouts, stalls, CLEAR_FEATURE, drop, shutdown. USBTEST: steps on disk per line (fsync) from the start,
  USBTEST.TXT without a file name too, M = raw descriptors, flush bounded 1 s. MIDPLAY /USB: USBMIDI.TXT the same way.
  SBEMU: start-up lines in SBDETAIL (committed), runtime ones via the flight recorder (SBTRACE.TXT).
- Tests: testrig/midi/unomatrix.sh (rig.py --dos71 = store-dos71 MS-DOS 7.1 HIMEM only): normal Uno USBTEST / SBEMU
  /MUSB + MIDPLAY with unplug at 15 s / MIDPLAY /USB, hostile OUT nak / stall, bLength 0, long wTotalLength, no
  endpoints, MidiSport 1010, beta.108 fake: 37 PASS + 2 QEMU-LIMIT (MIDPLAY /USB on QEMU OHCI, see below) on
  the release binaries; every hostile case reaches DONE with the reason logged.
- QEMU limits: its OHCI keeps one packet in flight and stops a multi-packet OUT when the answer is late (matrix: pacing
  off on ohci); it also loses appended OUT TDs like the NEC (lost-TD check must stay for OUT: removing it = 0 events);
  ED rewrites with an async usbredir packet in flight sometimes abort QEMU (usb_cancel_packet assertion) - QEMU bug.
  86Box: no USB pass-through device, the fake cannot run there.
- MENU matrix: run.sh MIDI=1 (fake on port 3, /MUSB:1, MPUTEST plays to it, MIDIEV >= 50; not uhci): 71 x32 (HIMEM only) +
  j32 on ohci and ehcic, before: all 4 PASS (disks A/B, 7 plays, 424 MIDI events each).
- Speed (86Box P2B-LS PII-450 ES1371, midi/perf.sh, /JOY:1 /VMPU:1): beta.113 = beta.114 344 int/s, avg 0.02 ms, 1%.
  SC55LOG.EXE byte-identical (md5 8cc1f00a..). 86Box rebuilt @ 0935207 + diffs (SDL, automation) in /tmp/dl/86box.
- Test downloads: DJGPP v3.4 gcc12.2, rufus (FreeDOS kernel), TimGM6mb (apt), 86Box 0935207, Linux sound/usb sources.
- Zip: beta.113 zip + all 17 EXE/COM rebuilt (VERSION beta.114), README.TXT, HWTEST.TXT. Personal pack: not built.
- Open (P5PE-VM, ask once): HWTEST.TXT beta.114 item (USBTEST first with SBEMU unloaded, MIDPLAY /USB, SC55 /MUSB game,
  then the NEC card): the last [midi]/[usb] line of USBTEST.TXT / USBMIDI.TXT / SBDETAIL.TXT names the step if it
  still freezes; USBTEST M dump = the real descriptors (compare with fakemidi.py).

## beta.113 (DOS session, 2026-11-13): MENU 3D everywhere + MENU bug hunt fixes - RELEASED from wip/menu3d-2026-11-13
Tests: host menurun.sh (menuasan + m3menu, ASan/UBSan) PASS; 30-min soaks (soak.py, ~3000 inputs, no CRASH.TXT):
QEMU hd, QEMU jem, 86Box PII-450 hd, 86Box jem PASS; Win98 SE (QI stock) DOS box via D:\LOOP.BAT, soak.py w 15 min.
CRASHKEY build: '!' in the text menu -> CRASH.TXT with registers/3D state/keys PASS (0xFFFFFFF0 write did not fault;
now null write + ud2). Bench 86Box PII-450 old (beta.110) vs new (soaked as beta.111 on beta.110; menu code unchanged in 112) fps: Carousel 19.3/19.4, Module 18.9/18.7, Panels
11.8/13.7; SBDIAG 3% CPU / 8% render both (SBEMU unchanged - SC55LOG not re-measured, SBEMU code not touched).
Rigs: testrig/menusoak (mkfolder.py jem = JEMMEX alone; BOOTLOG says LoadFailed for JEMMEX.EXE - false), runq.sh.
Win98 QI VM: boot without -usb; first boot hangs with a fresh D: (LESSONS) - add D: after the desktop is up.
Open (hardware, ask user once): 3D menu on the D810 (Radeon VESA modes/refresh list, F4 keep-mode), P5PE-VM (GeForce
6800), Win98 DOS box full screen. Cosmetic: Panels at 640x480 puts the grey reason box over the end of a long label.
Test downloads: DJGPP andrewwutw v3.4 gcc12.2 (/tmp/dl/djgpp), win98qi_v1.1.0_stock.iso, TimGM6mb.sf2, rufus sparse,
86Box 0935207 + roms, apt qemu-system-x86 mtools dosfstools zstd gcc-multilib.

## Personal pack beta.112 (2026-11-13): NUKED-SB-EMU-1.0.0-beta.112-usb-personal.zip
- Public beta.112 zip (sha256 1bfa7af2.. = release asset) + VM store store-personal files at the paths SETUP.BAT copies from:
  FILES\ROMS\* (41 ROMs + ROMHASH.CAC), FILES\XCDROM\MSCDEX.EXE, FILES\VIDECD\VIDE-CDD.SYS+VIDECD.TXT, FILES\GSCDROM\*,
  FILES\MOUSE\MOUSE.COM+MSMOUSE.TXT. Nothing else changed. Copy: priavte branch store-personal-1.0.0-beta.112-usb (not public).
- Test: fresh FreeDOS (mkdisk.py, KERNEL/COMMAND from store-qemu c.img), SETUP C, CONFIG JEMMEX NOEMS + VIDE-CDD /D:MSCD001,
  testrig/common/personal.bat via run.sh -r (+ -cdrom): CDROM.BAT -> D: lists the ISO; SC55.BAT: 41 ROMs hashed, set mk1-v2.00
  boots; SBDIAG: Synth SC-55, MIDI 19 B/s, 0 dropped (QEMU output 0 Hz = audiodev none quirk). No code change: no perf run.

## beta.112 (2026-11-13, DOS session): USB disks on xHCI, plug-after letters, UCDD from exFAT/NTFS letters (menumatrix)
- Built from branch wip/menumatrix-2026-11-13 (rebased on beta.111). Zip = beta.111 zip + 17 EXEs rebuilt (beta.112) +
  README.TXT HWTEST.TXT CDSET\UCDD.EXE CDSET\UCDDSRC.ZIP (mdm_helper.asm) (testrig/rc/mkzip.py with src = beta.111).
- Code: usbpad.c xHCI bulk (x_disk_eps: OUT/IN rings per disk at XO_MSD, one Configure Endpoint; x_bulk: Normal TRBs
  split at 64 KB, IN ISP, IOC last, events matched to the TRBs pushed; msd_clear = Stop EP + drop/add = new ring +
  DATA0; XEvBusy guard; light polls drain xHCI events only when that HC has pipes). usbdisk.c / usbredir.c: spare
  letters adopted per partition, 2 exFAT/NTFS spare letters (network, CDS +49h "SBUR"), USBDRV states 3/4.
  UCDD mdm_helper.asm: images on "SBUR" network letters allowed.
- Tested: testrig/menumatrix (QEMU; FreeDOS + MS-DOS 7.1 x JEMMEX / HIMEM only / 16-bit x UHCI OHCI EHCI EHCI+companion
  xHCI x sticks before/after SBEMU): logs/matrix-2026-11-13.txt 60/60 PASS (per ISO: mount from stick, install, bytes,
  MPUTEST play, unmount; stick swap). 86Box P2B-LS PII-450, /VSF TimGM6mb /JOY:1 /USBDISK:1, beta.110 vs beta.112:
  idle 345 IRQ/s avg 0.02 ms both; STRESS.MID ISR avg 1.77 ms / longest 4.73 vs 4.74, render 237 vs 238%, 0 lost ticks.
- OPEN (pre-existing, not from this work): QEMU xHCI with USB sticks attached, even /USBDISK:0: the SoundFont often
  never renders (SBDIAG "Synth: SC-55, output 0 Hz", MIDI arrives, AC97 runs). Comes and goes with code layout
  (a debug build with counters in main.c/vmpu.c always played): smells like a stray write. No sticks = plays.
  Repro: `SBX=/USBDISK:0 testrig/menumatrix/run.sh fd j32 xhci before` (WAV loud 0.0). Real xHCI PCs untested.
- Rig notes: xhci p2=8,p3=0 (QEMU usb-storage otherwise lands on a USB 3 port that SBEMU leaves off); audiodev
  timer-period=2000 (else ~8 IRQ/s); PASS = 7x PLAY OK, loud = info; FreeDOS x32 = HimemX (MS HIMEM hangs the FreeDOS
  kernel; build: Baron-von-Riedesel/HimemX + JWasm `make -f GccUnix.mak`, `jwasm -mz HimemX.asm`); HASISO.COM skips
  empty letters (MS-DOS 7.1 asks Abort/Retry/Fail there - normal DOS, documented in README.TXT).
- Resume rig: DJGPP + apt (qemu-system-x86 socat mtools exfatprogs exfat-fuse ntfs-3g gdisk fdisk dosfstools
  genisoimage nasm); FULL.866 = `isoinfo -J -i win98qi_v1.1.0_ALL.iso -x /osroots/1/FULL.866` (github.com/oerg866/
  win98-quickinstall v1.1.0), mercy.py -> /tmp/dl/w98qi/x (IO.SYS MSDOS.SYS COMMAND.COM WINDOWS\HIMEM.SYS);
  testrig/rufus/res/freedos + src from github.com/pbatard/rufus; TimGM6mb (apt-get download timgm6mb-soundfont)
  -> /tmp/dl/timgm6mb; release zip -> /tmp/rel; `make; sh tools/ucdd/mkcdset.sh; mkstick.sh /home/claude/vm/st`;
  `J=2 testrig/menumatrix/all.sh OUT`. 86Box perf: 86Box @0935207 + 86box-xpad.diff, VM = mkdisk build of
  KERNEL/COMMAND/JEMM/HDPMI + SC55\A|B\SBEMU.EXE + PERF.BAT (SBDIAG 3x DELAY idle, MIDPLAY STRESS.MID /S /Q:30),
  cfg p2bls-uhci.cfg with cpu_speed 450000000 / multi 4.5, F1 at the CMOS message.
- Real-PC checks: HWTEST.TXT beta.112 lines (plug-after partitions + exFAT/NTFS, CD image on such a stick, USB 3 port).

## beta.111 (2026-11-13, DOS session): release candidate - menu walk, SETUP/UNINSTAL/CLEANUP, Win98 test, USB review
- Branch wip/release-rc-2026-11-13 rebased on beta.110 (its own /VER packver.c + BUGREP block dropped: main's packver.h +
  SBDIAG DOCTOR already do it). Zip = beta.110 zip + all 17 EXEs rebuilt (VERSION beta.111) + SETUP UNINSTAL CLEANUP
  SC55RUN README.TXT (testrig/rc/mkzip.py; rig: testrig/rc/t86.py v un.py cl.py, menuwalk-2026-11-13.log).
- Tested: 86Box FreeDOS: every MENU entry with and without SBEMU (all launch + return; USBTEST and Automatic setup
  greyed while SBEMU is loaded, by design), CD image mount M:, SETUP; UNINSTAL K and D (D = disk identical to before
  SETUP), CLEANUP with planted leftovers and with none. Win98 (QEMU QuickInstall): DOS-box SETUP/CDINST/MENU (Start
  refuses: "Windows is running"), SETUP98 + restart, SC55W tray + MPUTEST answers at 330h in a DOS box, Restart in
  MS-DOS mode + SC55 (SBEMU runs, no false Windows message; QEMU's ISA SB16 = "no supported soundcard", expected).
  beta.111 zip smoke test in 86Box: SETUP, SC55, MENU header beta.111.
- Fixes: UNINSTAL D left C:\HDPMI\HDPMI16.TXT + C:\GSCDROM\README.TXT; K saves C:\SC55\*.SF2; CLEANUP "nothing to
  delete" + TYPE|FIND (no header noise); SETUP MSCDEX copy guarded; SC55RUN no CTMOUSE when %windir% is set (Win98 box).
- Known issues (not fixed): UNINSTAL / CLEANUP need MS-DOS's MOVE FC ATTRIB FIND CHOICE (a bare FreeDOS boot without
  them stops at once, "NOTHING was removed"); Sound > JEMM / QEMM entries without JEMM/QEMM loaded from CONFIG.SYS only
  show SBEMU's "QEMM or QPIEMU not installed" line; Win98 binaries (SC55W*) carry no pack version; one QEMU hang at
  "C:\>" on SETUP98's restart (a DOS box was open; did not repeat - watch on real PCs); README counter.gif still 000109.
- Rig lesson: never mcopy into a disk image while Windows runs on it (Windows reused the clusters: the BAT became
  IOS.LOG text, "Bad command or file name" x20). Copy with the VM off.
- USB code review (beta.108 + beta.110, usbpad.c; not changed here - owned by the USB sessions):
  1) hc_quiesce UHCI: "stop (HCHalted)" timeout now SKIPS the controller. UHCI spec: USBSTS default 0000h and HCHalted
     is only set after Run/Stop goes 1->0, so a UHCI the BIOS never started can read HCH=0 while stopped -> its ports
     dropped (beta.109 went on). QEMU sets HCH at reset, so usbmulti.py cannot show it. Linux only warns here.
     Suggest: fail only if USBCMD.RS still reads 1, else log and go on.
  2) A /USBSKIP'd or failed EHCI keeps whatever CONFIGFLAG the BIOS left (usually 1 = all ports on the EHCI), so its
     companions see no devices; only the "[usb]" line says why. Worth a sentence in README.TXT /USBSKIP.
  3) OHCI phase 1 only turns interrupts off; one left running by a BIOS (IR=0, HCFS operational) keeps running its
     schedule until phase 2 resets it (same as before beta.110).
  4) pci_d0: D3hot->D0 without No_Soft_Reset may clear the BARs; mem_bar() is read after it, so a lost BAR shows as
     an unusable controller rather than a hang - fine, maybe log it.
  5) USB MIDI packer (USBPAD_MidiByte): running status, SysEx CIN 4/5/6/7, system common CIN 2/3/5, real time CIN F
     all per USB-MIDI 1.0. Minor: SysEx cut by a status byte right after a full 3-byte piece sends no end packet.

## Fast test harness (sc55-pack/testrig/common, 2026-11-13)
- `restore.sh NAME` pulls store-NAME from deon81997/priavte and unpacks it (qemu -> /tmp/mq, 86rig -> /home/claude/86rig
  + 86box; win98 = both halves; skips if already restored, -f forces). Needs `apt-get install zstd qemu-system-x86 mtools
  xvfb librtmidi6 imagemagick`.
- `run.sh [-e qemu|86box] [-t SEC] [-r] [-p FILE] [-s OUT.png] IMAGE BATCH` copies IMAGE (original untouched), hooks
  BATCH into AUTOEXEC.BAT (-r: AUTOEXEC = only the batch), boots headless, waits for C:\RESULT.LOG with a PASS/FAIL
  line LAST, prints only that log, kills the VM. Exit 0 PASS / 1 FAIL / 124 timeout. `shot.sh OUT.png` = screenshot,
  only when asked (also `run.sh -s`).
- Tested: FreeDOS /tmp/mq/c.img and MS-DOS 7.1 /home/claude/86rig/dos.img, PASS and FAIL batches: QEMU ~4-5 s,
  86Box (Celeron 366, 86rig cfg; disk geometry set from the image size) ~13 s.

## beta.110 (2026-11-13, DOS session): freeze with two USB 2.0 hosts (P5PE-VM: ICH5 + NEC uPD720101 card)
- Report: hard freeze while SC55.BAT starts SBEMU's USB with the ICH5 (4 UHCI + EHCI 24DD) and a Belkin F5U219 (NEC
  EHCI 1033:00E0 + 2 OHCI 1033:0035, no option ROM) both on; Win98 fine; board alone worked; card alone untested.
- QEMU repro rig: testrig/usbmulti.py (ICH9 EHCI + 3 UHCI companions, ich9-usb-ehci2 + 2 pci-ohci companions -
  QEMU's usb-ehci takes no companions; USB keyboard + stick on one, mouse + stick on the other; pm = HIMEM.SYS from
  the 86Box rig's Win98 image, jem = JEMMEX + JLOAD QPIEMU from JemmB_v586). beta.109 does NOT freeze there (QEMU has
  no BIOS SMI code), both modes, with and without /USBDISK:1. So the fix is from the audit:
- Audit (usbpad.c): every register poll already had a bound (tsc or a counter) - no software endless loop. Found:
  (1) each controller was taken from the BIOS only when its own setup ran, in type order (EHCI before its UHCI
  companions): the BIOS's SMI code could still run the companions while CONFIGFLAG / port owner changed on the EHCI,
  and with the card in, a second EHCI did the same; (2) a timeout (BIOS handoff, HCHalted, HCRESET, OHCI HCR) did not
  drop the controller, it just went on with it; (3) no D3 -> D0; (4) EHCI USBSTS / OHCI status left pending (the NEC
  chip has no INTx-disable bit, so a shared IRQ line could be held); (5) the trace stopped going to SBDETAIL once its
  4 KB buffer was full; (6) a 17th controller was dropped silently. No USB IRQ is used (polled), controller list 16.
- Fix: phase 1 for ALL controllers in PCI order (as Linux's early quirks; on Intel the UHCIs come before their EHCI):
  power D0, UHCI LEGSUP 8F00 + stop + INTx off, OHCI ownership change + all interrupts off + status cleared, EHCI
  USBLEGSUP handoff (BIOS not letting go in 1 s: taken anyway, as Linux, logged) + stop + USBINTR 0 + USBSTS cleared.
  Phase 2 = the old setup order. Any wait that times out logs "[usb] bb:dd.f TYPE: <step> timed out, controller
  skipped" and that controller is not used. Every step is logged BEFORE it runs ("[usb] ..." lines in SBDETAIL.TXT,
  committed per line; trace buffer 8 KB, full buffer still feeds SBDETAIL).
- Switches: /USB:0 (SBEMU touches no USB controller; also turns /MUSB off), /USBSKIP:<list> ("vvvv:dddd" or "bb:dd.f",
  joined with + because a batch %1 splits at commas); skipped = not touched at all (no bus master). USBTEST / MIDPLAY
  /USB: SET USBSKIP=... . README.TXT, HWTEST.TXT (P5PE-VM item first).
- Tests (QEMU usbmulti.py): pm and jem, default and /USBDISK:1 (4 drive letters), /USBSKIP:8086:293c+00:06.0 (both
  "skipped", nothing else touched), /USB:0: all reach DONE, same devices as beta.109. USB start-up step time the
  same within QEMU noise (fsync per line, about 14 more lines).
- Speed (86Box P2B-LS PII-450 ES1371, midi/perf.sh, SBEMU /JOY:1 /VMPU:1, no ROMs): beta.109 344 int/s avg 0.02 ms
  ISR 1% = beta.110 identical. SC55LOG.EXE md5 8cc1f00a.. identical (no USB code in it). Runtime path unchanged
  (the change is start-up only). 86Box with automation rebuilt from testrig/86box/README.md into /home/claude/86box/86box.
- Not tested (needs the P5PE-VM): whether phase 1 is the fix; HWTEST.TXT has the steps (SBDETAIL.TXT, then
  /USBSKIP:1033:00E0, /USB:0). If it still freezes, the last "[usb]" line names the step.
- Zip: beta.109 + all built tools (version), README.TXT, HWTEST.TXT. Personal pack: not in this session.

## beta.108 (2026-11-13, DOS session): USB MIDI (class compliant, e.g. M-Audio Uno) - SBEMU /MUSB, MIDPLAY /USB, USBTEST
- usbpad.c: audio class / MIDIStreaming interface (alt 0) -> two pipes, PT_MOUT (bulk OUT) and PT_MIDI (bulk IN), in the
  periodic schedule like the pads (UHCI frame list QH, OHCI interrupt ED (D=OUT for the OUT pipe, oslow path shared),
  EHCI periodic QH (split for FS behind a TT), xHCI bulk endpoint contexts type 2/6). Flag USBPAD_F_MIDI; one device.
  USBPAD_MidiByte: MPU byte stream -> 4-byte USB-MIDI packets (running status, SysEx 3-byte pieces CIN 4/5/6/7, real time
  CIN F anywhere, system common CIN 2/3) into a 1024-packet FIFO, no USB access (safe in a port trap); dropped while no
  device. mout_poll never waits: a transfer of up to 256 bytes (64 events; UHCI: one TD per packet, depth first, 16 max)
  started by one poll, collected by the next. Stall: CLEAR_FEATURE on the next full poll. IN: packets -> USBPAD_SetMidiIn
  callback. OUT pipe obuf = its ibuf area (256 bytes up to the xHCI ring). ILEN(): IN transfers 64 bytes even with
  512-byte HS endpoints. Shutdown flushes the FIFO (1 s max). QEMU's OHCI (106B): MIDI IN pipe not used (one transfer in
  flight per controller there + shared buffer: OUT bytes came back as IN).
- SBEMU: /MUSB:1 (needs /JOY:1): MAIN_MPU_330 out -> USBPAD_MidiByte; MPU trap installed for it; turns /VMPU (launchers)
  and the SC-55 auto-load off. SETTINGS: MIDI synth "USB MIDI device" (writes /MUSB:1). MENU status: "USB MIDI device".
- MIDPLAY /USB: links usbpad.c (makefile MIDPLAY_USB_SRC, -DMIDPLAY_USB), drives the controllers itself (no SBEMU, PS/2
  keyboard: no USB keyboard there); no LCD queries then. USBTEST: lists it, test note (C4) when found, N again, MIDI IN shown.
- Tests (testrig/midi/README.md, testrig/fakemidi.py = usbredir USB-MIDI device, QEMU 8.2 from Ubuntu): UHCI, OHCI, EHCI +
  UHCI companions, EHCI HS (no companions), xHCI FS and HS: USBTEST note + MIDI IN; SBEMU /MUSB + MIDPLAY 35 s with
  device_del/device_add at 30 s: event streams identical on all six, music resumes after the re-plug; MIDPLAY /USB; 360
  pad + MIDI on one bus (UHCI, xHCI) both work. Parser host test: 190000 random bytes round-trip. MENU menurun.sh quick PASS.
- Speed (86Box P2B-LS PII-450 ES1371, testrig/midi/perf.sh, SBEMU /JOY:1 /VMPU:1 without ROMs, MIDPLAY 40 s): SBDIAG
  beta.107 344 int/s, avg 0.02 ms, ISR 1% = beta.108 identical. SC55LOG.EXE byte-identical to beta.107 (md5 2cd851ff..):
  speed unchanged. No ROMs in this session: SC-55 render load not measured. /MUSB itself is off by default (SBDIAG cannot
  measure without the VMPU side channel).
- Not tested (needs the real PCs): the Uno on the D810 (ICH6) and on the NEC card (oslow OUT path: QEMU asserts with
  -DOSLOWFORCE + usbredir async bulk); real MIDI IN. HWTEST.TXT has the items.
- Zip: beta.107 + SBEMU MIDPLAY USBTEST SBSET SC55MENU GAMETEST JOYTEST KMTEST MPUTEST SBDIAG TIMETEST USBDRV PADCONV
  SC55WAV SC55LOG CDGUI (version / tui.h / USB), README.TXT, HWTEST.TXT. Includes the MENU bug-hunt fixes below (tree
  builds, menurun.sh quick PASS; its menuwalk / 3D / Win98 checks are still open). Personal pack: not in this session.

## MENU bug hunt (2026-11-13, DOS session) - fixes released in beta.108 (see above); the open checks below stay open
Stopped by the user mid-task. Tree builds; tests/m3host/menurun.sh and run.sh pass.
- Done (tools/sc55menu.c, menu3d.h, tui.h):
  - New host test tests/m3host/menuasan.c + menurun.sh [quick|fit|ed|shots|seed N]: whole MENU on the fake DJGPP (fake disk
    /tmp/menufs/C = C:\, fake MPU-401 for "SBEMU loaded", scripted keys + INT 33h mouse, DUMP/SHOT keys), ASan+UBSan:
    every entry x text/Panels/Carousel/Module x SBEMU off/on (MENUGO.BAT checked), game editor + browser, first start,
    MENUVIEW, safe mode, no VESA, no mouse, every F4 mode + F7 fonts, 60 random key/mouse runs. dosstub.h: HS_HOOKS.
  - Bugs fixed: (1) file browser had no Home/End; (2) Tab drive from A:/B: computed a bad letter; (3) game editor showed
    only 14 of up to 24 games (scrolls now, "full (24 games)"); (4) tui_yesno/tui_message sized lines by length, so word
    wrap cut text ("..."), e.g. the first-start question - now tui_wrap_lines; (5) 3D: descriptions cut (Carousel/Module 3
    lines, Panels 4): now 4 / 5 lines, CD images + game texts shortened (menurun.sh "fit" checks every item); (6) 3D: grey
    items gave no reason (text menu does) - m3_reason (overlay / in the Panels row); (7) 3D had no F1 help / F5 MIDPLAY;
    (8) 3D start without a VESA mode: no message, MENU3D.CFG kept; (9) 3D MENUGO.BAT write error silent (g_goerr);
    (10) Module LCD cache stale after the game editor (g_menugen). Proof: sc55-pack/testrig/menuq/3d-reasons-4lines.png.
  - New QEMU rig: testrig/menuq.py (MS-DOS 7.1 or FreeDOS disk, pack installed by its own SETUP.BAT, SoundFont, OHCI USB
    kbd/mouse; text screen via pmemsave) + testrig/menuwalk.py (starts every entry, gets back). MS-DOS 7.1 boot + MENU +
    Start SC-55 (SoundFont, USB kbd/mouse on OHCI) + return: OK (testrig/menuq/msdos71-menu.png). Full walk was running.
- Session 2 (f3a7f28+): menuwalk fixed (menuq.text() decoded CP437 00-1F as control chars, so ► never matched; print
  format bug) and run on MS-DOS 7.1 + FreeDOS: every entry OK; grey ones show their reason; JEMM/QEMM/Auto setup OK
  from a fresh boot. Fixed: BUGREP.BAT without MEM/FIND ("Bad command"): finds them on PATH/C:\DOS/WINDOWS\COMMAND/
  FDOS, else notes it. USBTEST: after it a BIOS-driven USB kbd/mouse is dead until restart (user stuck in MENU's
  USBTEST.TXT view) -> on exit with a USB kbd/mouse it asks "Restart the PC now? Y/N" (Y: SMARTDRV commit, KBC reset;
  QEMU: back in MENU, keys work). usb3d soak 600 s with new MENU/MIDPLAY/SBEMU: 23 clean exits, no CRASH.
- NOT done: Win98 DOS box (QI VM installs fine from win98qi stock ISO; first boot from disk sat >10 min on the splash
  - retry without -usb, alone on the CPU; /home/claude/w98vm qi0.img = fresh install), 86Box PII-450 perf, release
  beta.108 (zip: SC55MENU, USBTEST, BUGREP.BAT + tui.h users), README news (BUGREP, USBTEST restart, MENU fixes).
- Closed (session 3): all of the above shipped in beta.110/111; beta.111 did the Win98 DOS-box MENU test and the
  full walk. Re-checked on beta.110's USB init: USBTEST restart prompt still works (QEMU, Y -> MENU, keys OK). No perf
  run needed (SBEMU runtime untouched). Win98 QI VM note: a mkdisk FAT16 D: disk at first boot hangs IO.SYS (black
  cursor) - attach D: only after hardware setup; the 2nd boot after setup also sat at "continuing to load Windows".
- Resume: DJGPP + build per docs/SESSION-2026-11-13-DOS.md; `sh sc55-pack/tests/m3host/menurun.sh quick`;
  `apt-get install qemu-system-x86`; unzip release zip to /home/claude/rel; MS-DOS files: see Test downloads;
  `python3 -I sc55-pack/testrig/menuq.py /home/claude/mq/ms build msdos`, then `from menuq import Q; Q(dir).start(usb='ohci')`.
- Open: none confirmed beyond the list above; Win98 DOS box untested (LFN: menu sets LFN=n, browser uses 8.3 names).

### Test downloads (kept outside the repo, /tmp/dl/<name>)
- rufus `src/ms-sys/inc` (mkdisk.py boot records): `git clone --depth 1 --filter=blob:none --sparse https://github.com/pbatard/rufus /tmp/dl/rufus` + `sparse-checkout set src/ms-sys/inc`, symlink testrig/rufus.
- Windows 98 QuickInstall v1.1.0 `win98qi_v1.1.0_stock.iso`: https://github.com/oerg866/win98-quickinstall/releases/download/v1.1.0/win98qi_v1.1.0_stock.iso
  (curl works; gh release download is blocked). MS-DOS 7.1: `isols.py ISO OSROOTS/1/FULL.866 FULL.866`, then
  `mercy.py FULL.866 ms71 IO.SYS MSDOS.SYS COMMAND.COM` (menuq.py writes its own MSDOS.SYS, BootGUI=0).
- TimGM6mb.sf2 (GPL, 6 MB test SoundFont): https://raw.githubusercontent.com/craffel/pretty-midi/main/pretty_midi/TimGM6mb.sf2
- FreeDOS KERNEL.SYS/COMMAND.COM: git clone --depth 1 https://github.com/pbatard/rufus (res/freedos) -> /home/claude/fd
- DJGPP gcc 12.2: andrewwutw/build-djgpp v3.4; 86Box @0935207 + 86Box/roms (built OK with testrig/86box/README.md cmake line).
- User uploads this session (not redistributable, not in repo): sc55.sf2, SC-55.sf2, CT2MGM/CT4MGM/CT8MGM, DOGOS2, Pipalun,
  Saron, Tabla .SF2 - for SoundFont tests in the next session.

## beta.107 (2026-11-13, DOS session): Win98 DOS box + MS-DOS 7.1 USB letters tested; 320x200 3D title fix
- Source of Win98 / MS-DOS 7.1: public QuickInstall reference ISO (github.com/oerg866/win98-quickinstall releases v1.1.0,
  win98qi_v1.1.0_ALL.iso, 1.3 GB; OSROOTS/1/FULL.866 -> testrig/padkeys/isols.py + mercy.py for IO.SYS/MSDOS.SYS/COMMAND.COM).
  Win98 VM: testrig/mkfat32.py empty 1024 MB disk, boot the ISO with Enter spam, it installs 98SE (98Lite Micro) and
  reboots; then boot from disk (QEMU pc, pentium3, 128 MB, std-vga, sb16). Rufus headers needed: br_fat16_0x3e.h,
  br_fat32_0x0.h, br_fat32_0x52.h, br_fat32_0x3f0.h (testrig/rufus is gitignored).
- Win98 DOS box (MIDPLAY /X from a FAT16 D: disk via Start > Run): opens full screen; channel table, SC-55 panel, 3D (mode 13h)
  solid/vector/voxel all draw; Alt+Enter -> window: 3D, text, panel shown in the window; S back to 3D from a window switches
  Windows to full screen (Windows' choice); Esc quits to the desktop. VESA modes in a DOS box not tried.
- Bug found + fixed: 3D view at 320x200 - "name - title" ran into "Song 1/1" (midplay3d.h mp3_bar: title cut to the room left).
- MS-DOS 7.1 (MKDISK_BOOT=msdos, BootGUI=0) + beta.104 SBEMU /USBDISK:1, 3 sticks at boot: FAT16 D: (read, COPY exact,
  write CFG.TXT landed on the image), spare letters E:/F: for the exFAT/NTFS sticks ("Not ready" if used - expected),
  redirector G: NTFS, H:/I: exFAT (MBR / GPT): DIR, TYPE nested, 40-entry dir, COPY byte-exact x3, COPY/MD refused.
  MS-DOS 6.22: not available (Win98 ISO has 7.1 only) - stays on HWTEST.TXT.
- Zip: beta.106 + every exe carrying the version (GAMETEST JOYTEST KMTEST MIDPLAY MPUTEST SBDIAG SBEMU SC55MENU TIMETEST
  USBDRV CDGUI), HWTEST.TXT header. QEMU usb3d smoke (2 min) fine. Personal pack built from the user's beta.98 personal zip.

## beta.106 (2026-11-13): flight recorder (SBTRACE.TXT), per-program SB/MIDI command counts, BUGREP doctor
- sbemu/trace.c/.h: two RAM banks (ISR fills one); 64-event ring (same event within 2 s counts up), 6 program slots
  (current PSP from the DOS SDA +10h, name from its MCB; matched by PSP *and* name - DOS reuses PSPs). Hooks: sbemu.c
  command start (TRACE_Dsp), 22E/22F reads with INT_STS set (TRACE_IrqAck), MAIN_InvokeIRQ (TRACE_Irq: 16 raised, 0 acked
  -> event), render DMA addr 0 (TRACE_DmaCheck), MPU wrapper MAIN_MPU_T0/T1 around whatever handler is set (VMPU or
  hardware; MAIN_MPU_Wrap at both setups), vdiag.c DIAG_Midi/Fifo/TimerReplayed, usbpad.c plug/unplug, cdlink io_open.
- Written by TRACE_Idle from INT 28h (now always hooked) when InDOS=1, no critical error and the current PSP is the
  shell (own parent / COMMAND / 4DOS / NDOS); or TRACE_Flush on MPU command A9h when InDOS=0 (SBDIAG FLUSH; BUGREP
  does it first, so the MENU workflow is covered). Appends to SBTRACE.TXT next to SBEMULOG (64 KB cap, then new);
  SBEMUTRACE=OFF / =path. Path is only set when SBEMULOG is set (SC55.BAT does).
- Bug found by the trace: unknown DSP commands left SBEMU_DSPCMD set, so every later byte was a parameter until a
  DSP reset. Now SBEMU_DSP_Known() + parameter-byte table (24h 2, 0Eh 2, 0Fh/38h 1, Bxh/Cxh record 3, others 0)
  skip them; 80h = 2 bytes then IRQ (SBEMU_DSPCMD_SILENCE); B2h/C2h map to B0h/C0h.
- BUGREP doctor: tools/doctor.h in SBDIAG (`SBDIAG DOCTOR folder body out`): <=5 plain-English lines from SBLOG
  (no IRQs, Error/Failed, VMPU warning, Windows), ROMS empty, BLASTER unset, SBTRACE (IRQ never acked, DMA not set up,
  NOT SUPPORTED DSP, intelligent MPU, MIDI dropped, late renders), GAMETEST FAIL; then versions of every EXE/COM/SYS
  in the folder ("NSBVER <ver>" marker: makefile PACKVER forces tools/packver.h into 11 tool links; SBEMU/MENU have
  the version string). BUGREP.BAT writes the body to BUGREP.$$$, doctor puts summary on top.
- Tests: TRTEST (a small DJGPP program: E1, D1, 40, 80, 24, 20x F2 without ack, 14 without DMA, MPU FF/3F/AC, notes,
  SysEx, pitch bend). QEMU (FreeDOS + JEMMEX + QPIEMU + HDPMI32I + SBEMU on AC97, usb-kbd hot-plug via device_add) and
  86Box P2B-LS ES1371: SBTRACE + BUGREP summary as expected. Host doctor test with sample files (/tmp/dr).
- Zip: SBEMU, SC55MENU, SBDIAG, MIDPLAY, GAMETEST, MPUTEST, TIMETEST, JOYTEST, KMTEST, USBTEST, USBDRV, PADCONV,
  SC55WAV, SC55LOG, CDSET/CDGUI, BUGREP.BAT, README.TXT. SBSET.EXE still not swapped (see beta.100).
- Real hardware to check: SBTRACE.TXT after a few games on the D810 and P5PE-VM; BUGREP from MENU.

## beta.105 (2026-11-13, DOS session): MIDPLAY Options > "MPU-401 port"
- tools/midplay.c: g_mpuport (0 = Auto: mpu_find = /P, BLASTER P=, search), Options row 13 cycles Auto / 300..360,
  only ports that answer (mpu_probe from mpuport.h, instant on an empty port) - silent ones are skipped, wraps to Auto.
  Switch is live: silence() on the old port, UART on the new. Saved as MPU=<hex> in MIDPLAY.CFG; /P: on the command
  line still wins for that run. 86Box: SBEMU at 330 -> Right = 330h (300-320 skipped), Right = Auto, Left = 330h.
- Zip = beta.104 zip + FILES/SC55/MIDPLAY.EXE + README.TXT.

## beta.104 (2026-11-13, DOS session): emulator-coverable "NOT tested" items closed; HWTEST.TXT for the real PCs
- exFAT / NTFS through the release build (beta.103 SBEMU, FreeDOS, QEMU EHCI, testrig/qemu-usbdrive.sh + new testrig/mkxn.sh
  images): stick 1 MBR NTFS + exFAT (F: G:), stick 2 GPT exFAT (H:); DIR, long-name aliases, nested TYPE, 40-entry dir,
  300 KB COPY from all three byte-exact, COPY to / DEL / MD refused and volumes unchanged. Spare FAT letters D:/E: listed
  as "no FAT partition". PASS. Note: NTFS times show UTC (Windows stores UTC; DOS has no zone) - left as is.
- 86Box (testrig/86box/run86.py VMDIR OUT): Pentium 133 (P/I-P55TP4XE) and Celeron (Mendocino) 300 (Acorp 6VIA90AP), ViRGE/DX,
  ES1371, 64 MB: SBEMU loads; MIDPLAY text, panel, 3D, 3 looks x 4 players, themes cycled, quits; MENU text, 3D, styles
  (Panels/Carousel/Module), back to text; no CRASH.TXT. 30/30 PASS each. "Few frames" hint shown in 3D on both (expected).
- SC-55 ROM sets + MT-32 in notetest: done by the other session (see its 063ee1f note: 75 runs, 0 dropped).
- NOT coverable here (need the user's files): MIDPLAY in a Win98 DOS box (no Win98 image), MS-DOS 6.22 / 7.1 with spare
  USB letters (no IO.SYS/MSDOS.SYS). Both are in HWTEST.TXT; with the user's QuickInstall ISO (testrig/padkeys/isols.py)
  a later session can do them in QEMU.
- MENU shows "SBEMU not loaded" when no SC-55 ROMs / SoundFont are present (mpu_find needs SBEMU's synth) - by design?
- HWTEST.TXT (pack/HWTEST.TXT -> NUKEDSB\HWTEST.TXT): one checklist per PC (D810, P5PE-VM) + both.
- Zip: beta.103 + SBEMU.EXE, SC55MENU.EXE (version) swapped, HWTEST.TXT added.


## beta.103 (2026-11-13): MIDPLAY "out of memory" for songs on 8-12 MB PCs fixed
- Cause: at start MIDPLAY prepared the 3D frame + depth buffers (640x480: 2.4 MB, more with DJGPP malloc rounding)
  even in the text views; at 8 MB only ~0.8 MB DPMI memory was left, the 140 KB stress song (35570 events) failed.
- Fix: mp3_prepare only when the 3D view is on at start; start_index: song "out of memory" -> to_text(),
  mp3_free_buffers(), load again; the 3D view then comes back through the low-memory fallback (smaller mode).
  ui_log records failed songs and "3D memory freed"; CRASH.TXT also shows DPMI free memory.
- Tested 8 MB (QEMU and 86Box P2B-LS/ViRGE): text view plays (1.7 MB left); 3D view plays, 3D at 320x200.
  ASan host test still clean. Zip: MIDPLAY, SBEMU, SC55MENU (version) swapped.

## beta.102 (2026-11-13, DOS session): USB keyboard/mouse bytes enter the 8042 at the END of the sound interrupt (vkbc.c, main.c)
- Was "Known issue": P5PE-VM, Logitech Unifying 046d:c52b on the NEC card's OHCI ("ED rewrite per transfer" mode): MENU 3D GPF
  on F2/F3, MIDPLAY 3D "Stack Fault", both at harmless instructions; PS/2 keyboard fine.
- NOT reproduced in QEMU: testrig/usb3d (pci-ohci, and NEC-like EHCI + 2 OHCI companions with SBEMU built -DOSLOWFORCE),
  30 min each cycling every look/theme/player and MENU style (on beta.99 sources + this fix), 71 normal program exits each,
  no crash (also none before the fix). 5 min again on beta.101 sources: fine.
- Audit: every async entry already keeps the program's FPU (sound IRQ PM/RM: MAIN_FPU_ENTER; PM traps hdpmipt.c; RM traps
  qemm.c); no x87 before those saves (objdump of non-LTO objects). Stack (gcc -fcallgraph-info=su): sound IRQ ~7.8 KB of 32 KB
  (go32 wrapper / RMCB stacks), trap handlers < 2.5 KB of 16 KB. Instrumented run (counters at 0040:00E0-00FF): no nested PM
  trap, IF never 1 inside PM/RM trap handlers. Note: our HDPMI32I keeps a trapped frame in ONE host buffer (?USE_HOST_MEM=1
  in I2FHDPMI.ASM), so a nested PM trap would return to the wrong CS:EIP/SS:ESP - exactly this crash pattern; not seen here.
- Change: VKBC_Tick (inside MAIN_Interrupt, interrupts ON) no longer puts the byte into the 8042; VKBC_Deliver() does, at the
  end of MAIN_InterruptPM/RM after EOI + CD service with interrupts off. IRQ 1/12 now come when the CPU is back in the
  program (like a PS/2 keyboard) instead of running the program's/BIOS's/mouse driver's handlers nested inside SBEMU.
  Keys/mouse checked over USB in QEMU (trace usb_ohci_td_pkt_short shows the reports).
- Release: beta.101 zip with SBEMU.EXE and SC55MENU.EXE (version) swapped. Personal pack not rebuilt (user's personal zip
  not in this session).
- Needs the real P5PE-VM: Unifying receiver on the NEC OHCI, MENU 3D F2/F3 and MIDPLAY 3D V/C/J (MIDPLAY's beta.100 crash
  catcher writes CRASH.TXT). If it still crashes: CRASH.TXT / the HDPMI exception dump (CS:EIP, SS:ESP, error code) and the
  SBEMU log; next step would be an HDPMI32I with ?USE_HOST_MEM=0.

## README screenshots + art (2026-11-13, PARTLY DONE - user paused at usage limit)
- Retaken from beta.109 in QEMU (rig: /home/claude/shots: c/ = pack installed like SETUP + ROMs + test MIDI/MUS/HMP/XMI/KAR
  + CD images; start.sh = QEMU pentium3, AC97, pci-ohci + testrig/fakepad.py live pad; shot.py = monitor sendkey/screendump.
  NOTE: SeaBIOS doubles grey arrow keys): joytest, main-menu (Sunset), menu-3d-panels/carousel/module, settings,
  settings-3d, automatic-setup, midplay-channels (key+2, A-B, sleep shown)/panel/playlist/files/options/3d/3d-looks.
- STILL OLD: cd-images-groovy/classic/midnight, cd-images-3d, cd-files, gametest + gametest-3d, sbdiag, timetest,
  usbdrv (ok, 11-12), win98-settings.png + w98 shots (need the Win98 VM), padkeys game shots (fine).
- All version-stamped tools in the beta.109 zip rebuilt (same source, now say beta.109; SBSET also gets the low-pass).
  SC55WAV/SC55LOG not swapped. sc55synth.o's PGO profile is stale (core profiles still used).
- TODO from the user: a section listing everything brand new (now "brand new" only links to What's NEW), and new
  90s art (make90s.py icons) for each new feature: USB MIDI row reuses icon-sc55; game music, A-B/key/sleep, low-pass,
  test bench have no art yet. Repo About description/topics: user pastes them (proxy refuses settings writes).

## beta.109 (2026-11-13): SC-55 anti-alias low-pass (/VSCLPF), offline renderer, ST "hum" explained
- sc55/sc55engine.hpp: 3 biquads (6th order Butterworth, fc = 0.42 x card rate) on the native frames before the linear
  resampler, only when the card rate < native; integer DF1, Q28 coefficients, 8 extra fraction bits (no x87 denormal
  slowdowns). set_lowpass(); SC55_SetLowpass; VMPU_SetSC55Lowpass; SBEMU option /VSCLPF:0|1 (OPT_VSC_LPF, default 1,
  also changeable on a resident SBEMU). NOT in SBSET (SETTINGS) yet.
- tests/engine_test.cpp: test_antialias_lowpass (both native rates x 11025..48000): 1.5x Nyquist -32..-84 dB, 2x Nyquist
  -52..-inf dB, in band -0.15..0 dB, chunk invariance; the exact-arithmetic tests run with set_lowpass(false).
  Cost: ~4% on top of the SC-55 (offline render 6.3 -> 6.6 s); QEMU "needs" 218 -> 224%.
- tests/sc55render.cpp (make -f tests/Makefile.host out/sc55render): MIDPLAY host byte list -> WAV through SBEMU's real
  SC-55 path, any ROM set / rate / filter, as fast as the host. Use THIS to judge sound, not QEMU recordings.
- CORRECTION to the recording section below: the ST "hum" in QEMU was NOT aliasing. (1) QEMU TCG renders every SC-55
  set too slowly (needs 106-265%, only CM-300 < 100%), so SBEMU's render-ahead runs dry and pads silence every 128-frame
  block; (2) the SC-55ST (st-v1.01) idles with a real 517 Hz tone + harmonics at -36 dBFS (also in upstream
  nuked-sc55-render, so it is the core or the ST ROM, not SBEMU). Chopped by (1) it became the out/128 Hz hum. The
  filter does not change (2) (in band). Open: report the ST idle tone upstream / compare with a real SC-55ST.
- Zip = beta.108 zip + SBEMU.EXE (1.0.0-beta.109) + README.TXT (/VSCLPF in the option list). To test on real PCs:
  SC-55 at 22050 with /VSCLPF:1 vs :0 (whistles on high notes / cymbals), CPU headroom on the D810 / Celeron.

## MIDPLAY sound recordings per ROM set x rate (2026-11-13, done)
- notetest.sh: WAV=file (QEMU HDA; AC97 records only silence), SND, WARM / TAIL (silent songs: in QEMU the SC-55 starts
  up and renders late), SCREEN=1 (SBEMU's synth / ROM-set line from VGA text memory via the QEMU monitor). Test songs
  testrig/midplay/METRO.MID (20 piano notes 1 s apart) and PAD.MID (string chord held 10 s), both on channel 1.
- wavcheck.py [--metro N]: sound, level, peak, clipping, dropouts (= sound cut off loud -> nothing within 10 ms, before
  the last loud part), clicks, beats (10 ms resolution, 0.5 s debounce, +-30 ms).
- Matrix (75 runs, 2 QEMUs at once): every run 0 bytes dropped and the ROM set confirmed on screen. Findings:
  1. SC-55ST (st-v1.01): a constant tone under everything. Upstream nuked-sc55-render (built in /tmp/nb, needs
     librtmidi-dev libsdl2-dev) does the same at its native 66207 Hz: the tone is ~17 kHz / 32 kHz there (core / ROM).
     SBEMU's LINEAR resampler (sc55/sc55engine.hpp) folds it to an AUDIBLE 170-600 Hz hum (pitch changes with /K).
     => SBEMU needs an anti-alias low-pass before decimating (also helps every set's highs). NOT done.
  2. MT-32 / CM-32L silent: test songs on channel 1 (MT-32 parts 2-9, rhythm 10). On channel 2 (METRO2/PAD2): 20/20
     beats 0.99-1.01 s, no dropouts. Not a bug.
  3. JV-880 silent: upstream renderer silent with METRO.MID too (JV-880 is not GM/GS). Not ours; untested further.
  4. SC-55 family: beats 20/20 everywhere, jitter up to +-50 ms (SoundFont +-40, MT-32 +-10: probably /VSCAHEAD 50 ms
     render blocks); 0-20 short gaps (10-20 ms silence) with 2 QEMUs, 1 when mk1-v1.20 /K11025 ran alone -> mostly QEMU
     load, not decided for real PCs.
- Listening MP3s (44100, +20 dB): sent to the user as recordings-beta101.zip (not in the repo).

## beta.101 (2026-11-13): MIDPLAY - MUS/HMP/XMI, transpose, A-B loop, sleep timer, Ctrl+F search
User asked for: karaoke lyrics (DROPPED by the user mid-task - not needed), mute/solo, tempo + pitch, A-B loop,
MUS/HMP/XMI/RMI, M3U, search, sleep timer. Already there before: mute/solo (M/O/U), tempo ([ ]), M3U load/save,
type-to-jump. Files: tools/midplay.c only (+ pack/README.TXT section 8, testrig/midplay/fmtcheck.py, hostsweep.sh).
- Formats (song_load sniffs the contents, not the name): MUS (load_mus: 140 Hz = div 70 @ 500000; MUS ch 15 -> 10,
  others in order of first use skipping 10, CC123 on first use like Doom's mus2mid), HMP (load_hmp: tracks at 308h /
  388h for "013195", count at 30h, division at 38h, quarter = 1 s, HMP varlen = low group first, last byte bit 7 set,
  also for meta/SysEx lengths - layout as ZMusic midisource_hmi.cpp), XMI (load_xmi: first EVNT via iff_find, delays =
  summed bytes < 80h, note-on + VLQ length -> generated note-off (min 1 tick), div 60 @ 500000 = 120 Hz, tempo meta
  ignored), RMI (riff_midi walks the chunks to "data"). CC 110-119 (AIL/HMI loop+lock marks) dropped for HMP/XMI.
  song_info uses song_load for MUS/HMP/XMI. Extensions .smf .mus .hmp .xmi added; .m3u8; M3U skips a UTF-8 BOM and
  trailing blanks. KAR titles: "@T" text, track names "Soft Karaoke"/"Words" no longer become the title (meta_title).
  NOT supported: HMI ("HMI-MIDISONG"), XMI files with several songs (first only), MIDI format 2.
- Transpose: g_pitch -12..+12 ({ } \, PITCH= in MIDPLAY.CFG), channel 10 untouched; g_tmap[ch][note] remembers the note
  sent so a key change while notes sound ends the right ones (silence() clears it).
- A-B loop (A key, ab_key/ab_check): mp_pump sends nothing at/after B; the main loop seeks to A. Cleared by a new song.
- Sleep timer (Z, sleep_key): off, end of song (next_index not taken), 15/30/45/60/90/120 min -> mp_pause (SPACE plays on;
  /S quits). Shown as "Zz:end" / "Zz14m" in the right-aligned mode flags on row 3 (with key+n, A-B, speed...).
- Search: Ctrl+F on Playlist / Files (F or Ctrl+F on the Player page opens the Playlist search): substring of file name
  or title, "k of n", Ctrl+F / F6 next, ENTER plays, ESC closes (srch_key runs first in handle_key, so SPACE types).
- Tests: testrig/midplay/hostsweep.sh [FUZZ] = ASan+UBSan host build: --queue, fmtcheck.py (one tune as MID/KAR/RMI/
  MUS/HMP v1+v2/XMI -> identical note-on/off lists and times; transpose incl. changes mid-song = no stuck notes; A-B
  counts; sleep), busy genmid song with options, FUZZ damaged copies of every format (0 problems in 2400). Host main
  takes --pitch n --pitchat ms n --ab a b loops --sleep s --info. Also run: tests/m3host/run.sh (4101 frames, 0
  failures) and notetest.sh "/VSF:C:\SC55\GM.SF2 /VSFVOICES:24" on all formats + busy song: 0 bytes dropped (note:
  plain /VSF without a path = SC-55 without ROMs = everything "dropped").
- ROM x rate matrix (user's personal zip ROMs, notetest, 10 s each of T.MID/T.MUS/T.HMP/T.XMI/BUSY.MID): every complete
  set found by hash - mk1 v1.00/1.10/1.20/1.21/2.00, mk2 v1.01, st v1.01, sc155 rev1, sc155mk2 v1.01, cm300 v1.30, jv880
  v1.0.0/1.0.1 (/VSCSET:name) + /VMT32:MT32 + /VMT32:CM32L + SoundFont, each at /K11025, 22050, 32000, 44100, 48000:
  75 runs, 0 bytes dropped (SoundFont runs show "busy" = MIDPLAY waited, nothing lost). Incomplete in the zip: cm300
  v1.10/1.20, scb55, rlp3237. SBEMU's ROM set confirmed on screen (QEMU screendump) for mk2. QEMU's wav audiodev
  recorded only silence, so the sound itself was not checked; SBDIAG's synth section reads 0 in QEMU.
- Not tested: the new keys interactively in QEMU/86Box (only through the host build), real game files (made-up test
  files only - try Doom .MUS, Descent .HMP, Ultima/Dune II .XMI), real hardware (D810, P5PE).
- Zip = beta.100 zip + FILES/SC55/MIDPLAY.EXE + README.TXT.

## beta.100 (2026-11-13): MIDPLAY 3D made stable (crash catcher, ASan sweep, low-memory fallback)
- Crash catcher (tools/midplay.c, crash_handler): SIGSEGV/SIGFPE/SIGILL/SIGABRT/SIGNOFP -> text mode 3, CRASH.TXT in
  MIDPLAY's folder (signal, CPU exception number, registers from __djgpp_exception_state_ptr, text/3D mode, look/theme/
  player, song, last 50 UI actions from ui_log: keys, 3D on/off/mode, songs), silence, _exit(3). EIP is from the stripped
  exe: rebuild the same source without -s and use addr2line. Test build: `-DCRASHKEY` makes '!' do a GPF.
- Host sanitizer test: `sh sc55-pack/tests/m3host/run.sh [quick|crash]` (needs gcc-13-multilib). Builds the whole
  midplay.c 32-bit against a fake DJGPP (tests/m3host/inc/dosstub.h: fake DOS memory, fake VBE BIOS with 44 modes
  320x200..1600x1200 incl. 321x201, 1366x768, 1599x1199 in 8/15/16/24/32 bit, fake LFB/bank window that aborts on any
  write past the screen), ASan+UBSan+float-cast-overflow. Covers every look x theme x player, F4 half, pause, mode
  switches, styles, long clock, long titles, OOM limits. ~4100 frames, 0 findings now.
- Found/fixed: m3_tri_raster float->int overflow near the camera (U/V/light/fog 16.16 and ys/xs) -> m3_fi() clamps
  (m3core.h, so MENU 3D, CDGUI, GAMETEST, SBSET too). Textures (MIDPLAY panel/keys/face, m3 floor) are static now.
- Out of memory: mp3_buffers(w,h) allocates BEFORE the mode is set (frees old first); mp3_try_smaller picks the
  biggest smaller mode (same depth first), message "Not enough memory..."; the OOM mode is not saved as last good.
- QEMU (FreeDOS, std-vga, HDPMI32I, MIDPLAY /X): 1600x1200x32 + 130 random V/C/J/F4/SPACE/seek/S keys, no crash;
  -m 12 -> fell back to 1280x800; CRASHKEY build wrote C:\CRASH.TXT (page fault, registers, actions). MENU 3D styles OK.
- Zip: MIDPLAY, SC55MENU, SBEMU (version), GAMETEST, CDSET/CDGUI.EXE, README.TXT swapped. SBSET.EXE NOT swapped (zip's
  copy is 130 KB smaller than a fresh build - built differently; it gets the m3_fi fix next time it is rebuilt).
- 86Box (built from source @0935207 + testrig diffs, P2B-LS PII-400, S3 ViRGE/DX 4 MB, VBE 1.2 BIOS, FreeDOS +
  HDPMI32I): auto 640x480, 1600x1200x8 (banked), 1280x1024x16, 1024x768x32, 320x200 with 25-60 random V/C/J/F4/SPACE
  keys each: no crash; 800x600x24 not offered -> auto. 8 MB RAM + 1600x1200 wanted -> 800x600 "(low memory)";
  CRASHKEY '!' -> CRASH.TXT (GPF 0Dh, banked mode, actions). Seen: at 8 MB (and QEMU 12 MB) the 140 KB genmid stress
  song does not load even in the text view - not looked into (not a 3D issue).
- Real hardware still to test: MIDPLAY 3D on the D810 and the P5PE-VM (big modes, CRASH.TXT if anything happens).

## SSF2T demo investigation (2026-11-10, unfinished, no release)
- **Cause 1, found and fixed (not shipped yet):** the pack's HDPMI32I (crazii fork, based on HDPMI 3.20) gives a GPF when Borland 32RTM runs `mov cr0,eax` in ring 3. HDPMI 3.21+ (Baron) emulates it. The exception text is invisible (direct video output), so HDPMI just waits for 'c'/'s' → it looks like a hang at MAKESWP.
  - Patch: `sc55-pack/third_party/hdpmi32fix/mov-cr0.diff` (against crazii/HX 43182b4).
  - Build: `build32.sh HXDIR OUTDIR` (jwasm + OpenWatcom wlink, no JWlink needed). Debug build: `VIO=0 build32.sh HX OUT -D?DOSOUTPUT=1` prints exceptions through DOS.
  - With the patch, MAKESWP runs OK when only HDPMI is loaded.
- **Cause 2, open:** with SBEMU resident (even plain `SBEMU.EXE`), MAKESWP prints "Swapfile required 0K" and then hangs on exit. The CPU ends up in real mode with TF set, running freed 32-bit code (32RTM teardown). Things tried without success: ignoring the cr0 write (same hang), `HDPMI=32` (crash). Next ideas:
  - 32RTM's INT 31h 0202/0203 exception handlers or IRQ-route cleanup in crazii's I2FHDPMI on client exit.
  - Compare with Baron v3.24 client-exit code.
- Official HDPMI32i 3.24 runs SSF2T fine but crashes our SBEMU (different trap API).
- Test roots: ~/mptest/rsf (SBEMU+game), rsfB (HDPMI only); tools ~/tools/lin.py (page-walk memory reader via 86Box automation).

## MIDPLAY with thousands of MIDI files (2026-11-10, sc55-pack/tools/midplay.c only)
Review for 2,000-10,000 files, deep folders, long names. No fixed-size song arrays were found (playlist, folder scan and browser already grew with realloc). Fixed:
- **Long names cut off:** browser entries were `char name[80]`, so a longer LFN name became a path that could not be opened (the file showed but did nothing). Names are now strdup'd at full length.
- **Slow folders:** scan_dir (/D, MIDPLAY's folder) and the file browser used readdir()+stat() per file (several DOS calls and a directory search each). They now use findfirst/findnext (name, attributes and size in one call).
- **O(n^2) "already in the playlist":** pl_find was a linear stricmp scan. It was called for every visible browser row on every frame, and once per file by ^A/^P (10k x 10k compares). It is now a case-insensitive hash, rebuilt lazily after remove/sort/move/clear (pl_dirty). Browser: getcwd() is called once per folder load (fl_cwd), not per row or file.
- **M3U lines over 299 chars** were split into two bad entries. The buffer is now 1024 and the rest of a longer line is skipped. Save buffer is 600.
- **Playlist number column:** `%3d` ran into the play mark at 10,000+ songs. It is now `%5d` at column 1.
- **Shuffle with many songs:** shuffle picked any song except the current one, so songs repeated long before every song had played. It now picks only songs not played yet (PE.played, combining two rand() calls for >32k), then refills; Repeat off stops when all songs have played.
- Not changed: F5 sort by title or length reads every unread song first (synchronously, music keeps playing via mp_pump). With 10k files that takes a while and shows no progress. The 3D view shows only "Song n/N" (no file list), so it was not affected.
- Built clean with DJGPP gcc 12.2 (`make output/midplay.exe`). **Not yet run in 86Box:** this workspace had no 86Box build or VM images. Next: make a FAT16 image with 10k generated .MID files in nested folders with long names, then check /D load time, browser scrolling, ^A, F5, and shuffle.
## CD image browser: any drive, last folder, home folder (2026-11-11, committed, not released yet)
- CDGUI (MENU > CD-ROM > CD images): opens any folder on any drive. D = next drive, Backspace = up,
  G = go home, F2 = make this folder home. CDGUI.CFG now keeps CUR= (last folder, any drive) and HOME=.
  The old DIR= line is still read. An image outside C:\CDIMAGES is mounted and stored in MOUNTED.FLG by its full path.
- MENU > Games > edit > CD image: the same kind of browser (Tab drive, G home, N none). GAMES.CFG cd field is now
  96 characters wide (a full path); MENUGO.BAT mounts full paths as they are.
- tools/crit24.h: crit_dir_ok() checks a drive or folder. CD drives are asked through INT 2Fh 150Bh/1505h first.
- **Bug found:** a DJGPP program doing access("M:\") on the EMPTY uCDD drive locks up HDPMI32I (host halt, IF=0).
  Also, a DJGPP program started straight after CDON.BAT in AUTOEXEC hung at startup in one test, while it worked
  when started through GUI.BAT. Not explained yet; it may be the cause of the "read error / init error" reports.
  Do not set the real-mode INT 24h vector under HDPMI32I: that locked it up too.
- Tested in 86Box (cdtest/rootN): D skips the empty M:; GAME.ISO mounted from C:\OTHER and its files listed on M:;
  F2 home and the last folder (CUR=M:) were saved.

## beta.92 (2026-11-11): empty CD drive freeze fixed in HDPMI32I; CD browser released
- **Cause:** DOS raises INT 24h ("General failure", e.g. reading the EMPTY uCDD drive M:). HDPMI's real-mode INT 24h
  callback calls segm2sel -> allocxsel, which does `push ds`/`pop ds`. At that point DS still held a REAL-MODE segment,
  so `pop ds` in ring 0 gave a GPF. The exception text is invisible (?VIODIROUT), so HDPMI just waits for a key.
  Found with a debug build (`VIO=0 build32.sh ... -D?DOSOUTPUT=1`) and the map file (fault at end of allocxsel,
  return address in segm2sel).
- **Fix:** load DS=_FLATSEL_ before the INT 24h translation (sc55-pack/third_party/hdpmi32fix/hdpmi32i-fixes.diff,
  together with the mov cr0 fix). Binary: third_party/hdpmi32fix/HDPMI32I.EXE (crazii/HX 43182b4 + both fixes,
  jwasm + OW wlink). It is also in the release zip.
- Tested in 86Box: a DJGPP program doing access("M:\") on the empty drive returns -1 (before: freeze), both without
  and with SBEMU /VMPU + SC-55. CD read test (CDCHK2) passes with CD music playing. MIDPLAY STRESS.MID runs in all 3 views.
- Note: our HDPMI is built from crazii HEAD, not the exact old v0.1-beta4fix2 binary (38646 bytes). Watch for regressions.
- Still open: SSF2T MAKESWP hangs on exit with SBEMU resident (see the SSF2T section).

## beta.99 (2026-11-13): MIDPLAY 3D looks, themes, players (midplay3d.h)
- Keys in the 3D view: V look (g_scene: 0 solid, 1 vector, 2 voxel), C theme (g_ctheme 0..10), J player (g_model 0..3);
  saved in MIDPLAY.CFG (SCENE3D, THEME3D, PLAYER3D). Themes table T[] in mp3_theme_apply: hue range, sky, grid, and the
  module colours (body, face, LCD, ink, accent); 7..10 = SC-55 / SC-88 / MT-32 / CM-32L styles (colours only, no logos).
- Players are box lists (mp3_model: kind 1 screen = LCD texture, 2 speaker/knob, 3 light strip, 4 platter); drawn solid
  (m3_box), as lines (mp3_wbox, Wu lines, afterglow = fb * 5/8 each frame, always full resolution) or as blocks.
- Shared: mp3_notes_scan (new notes from g_viz.nl jumps) -> note river (mp3_river), voxel drops, drum pulse
  (mp3_drum: camera nudge, vector grid); mp3_labels: the 3 loudest instruments (640+ wide only). Voxel: mp3_lod halves
  the block count when a frame takes > 90 ms.
- Tested in QEMU (FreeDOS, std-vga, SBEMU, a generated 90 s song with drums): every look x player x several themes, 640x480
  32-bit and 320x200 256 colours. Not tested on real hardware yet (speed of voxel on the Celeron).
- Ideas left: .KAR lyrics in 3D, a showcase mode (player/theme/look change per song), pitch bend in the river, camera
  choice, per-player LCD layouts. Open items: USB keyboard on the NEC OHCI crash (beta.102, real-PC test), 2-partition stick after
  Win98 MS-DOS mode on the real PC (beta.98 moves blocked letters), stale screenshots (GAMETEST, TIMETEST, 3D menus),
  rendertest branch to delete, a plain-HTTP mirror for Win98 downloads, modules (docs/MODULES.md not written yet).

## beta.98 (2026-11-12): USB letters refused after Win98 "Restart in MS-DOS mode"
- Symptom (user's P5PE-VM, NEC EHCI): D: E: "Invalid function", stub 0 calls, INT 21h 7302h CF ax 000Fh, 36h FFFF.
  F8 clean boot fine. Reproduced in QEMU (/home/claude/w98, stock.iso as CD): only when the stick was mounted in
  Windows first (device_add on the desktop), then Restart in MS-DOS mode: the letters Windows gave the stick are
  refused by the DOS kernel (WIN.COM's INT 21h hook is a plain jmp to DOS; cause inside IO.SYS, not found).
- Fix (usbdisk.c skip_blocked): after install, INT 21h 7302h (32h on DOS < 7) per unit with a temporary INT 24h
  "fail" stub; a letter whose call never reaches the stub (counter at 28h) moves to the next free letter (LoL+20h
  raised). QEMU: E: H: I: J:, both partitions read. CDS tail now written clean (cds_set), not copied from C:.
- USBDRV /L also prints the DPB chain, our DPBs + device header, INT 13/21/25/2F owners, DOS answers for drive 1.

## Win98 -> MS-DOS mode with SweetLow's USB 2.0 stack (QuickInstall 1.1.0), tested in QEMU (2026-11-11)
- The user's QuickInstall v1.1.0 stock ISO (= our Win98 test image) carries SweetLow's USB 2.0 + USB storage stack
  in OSROOTS/1/DRIVER.866 (USB2.INF, usbehci/usbport/usbhub20/USBSTOR.SYS). Windows installs it for a real EHCI.
  86Box has no EHCI, so our 86Box Win98 tests never loaded it; QEMU does.
- QEMU setup (/home/claude/w98vm, ehci.img = base98.img + EHCI drivers installed): ich9-usb-ehci1 + 2 pci-ohci
  companions (masterbus=ehci.0; the NEC card is OHCI+EHCI too) + usb-storage + usb-kbd (hot-plug it with
  device_add after boot: QEMU sends monitor keys to the newest keyboard). Registry check: PCI\VEN_8086&DEV_293A =
  "Standard PCI to USB Enhanced Host Controller" (SweetLow's stack active in Windows).
- Results after Start > Shut Down > Restart in MS-DOS mode:
  - A USB keyboard is DEAD at the DOS prompt (no BIOS legacy USB after Windows had the controllers) until our driver
    runs. So on PCs with only a USB keyboard, SC55 (SBEMU /USBKB) must start by itself in MS-DOS mode
    (DOSSTART.BAT), or the user cannot type.
  - USBTEST (same code as SBEMU /JOY): EHCI ports handed to companions; OHCI "[try1: win] re-reset" (it sees the
    state Windows left and resets again); the USB stick reads (CRC OK) and the hot-plugged keyboard works (keys seen).
    Same with ICH9 UHCI companions. So the Windows-left controller state is handled; not the cause of the
    "lost a TD" on the user's NEC card (that log was a cold DOS boot anyway: "DOS version 7.10, no Windows").

## beta.95 (2026-11-11): USB disk fixes since beta.94 built in
- SBEMU, USBTEST, SBSET, SC55LOG rebuilt from main (usbdisk.c: drives follow their stick when usbpad renumbers disks,
  a card swapped in a card reader is seen after 2 s idle). Everything else in the zip is beta.94's.
- fsread (exFAT/NTFS reader) is still a separate library, not built into SBEMU yet.

## Shareware test games: General MIDI set (2026-11-11)

- `testgames/` now holds ten untouched shareware archives. Added: `3dduke13.zip` (MD5 04e4ca70b8a2d59ed56c451c5c1d5d39, dosgames.com = archive.org 3dduke13SW), `htic_v12.zip` (MD5 420b23b3d8f2cbd164c121369eaa2b09, idgames mirror), `1rott13.zip`, `1rap12.zip` (user uploads), `desc14sw.exe` (dosgames.com original; a ZIP SFX holding ARJ .SOW parts).
- Licences read from inside each archive: Duke LICENSE.TXT (free WWW/FTP sites OK), ROTT/Raptor VENDOR.DOC (free copying encouraged, unmodified), Heretic LICENSE.DOC (electronic distribution, compressed only), Descent README.TXT section 10 (freely distributable unmodified, no fees; read from the user's DOSBox copy since the ARJ parts were not unpacked here).
- Doom 1.9 (`doom19s.zip`) has no licence file inside, so it stays a link only.
- Tip: Doom/Heretic `*.1` + `*.2` joined with `cat` are a plain ZIP; Apogee `.SHR` files are ZIP SFX (`unzip` works).
- README: new "★ Free Shareware Games!! ★" section after Download Zone with direct download links; Download Zone points to it.
- Not done yet: playing each game's MIDI music through the SC-55 in 86Box.

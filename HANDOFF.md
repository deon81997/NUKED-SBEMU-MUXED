
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

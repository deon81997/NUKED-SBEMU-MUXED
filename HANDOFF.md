
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
  choice, per-player LCD layouts. Open items: USB keyboard on the NEC OHCI crash (see below), 2-partition stick after
  Win98 MS-DOS mode on the real PC (beta.98 moves blocked letters), stale screenshots (GAMETEST, TIMETEST, 3D menus),
  rendertest branch to delete, a plain-HTTP mirror for Win98 downloads, modules (docs/MODULES.md not written yet).

## Known issue (2026-11-13, not fixed): crashes in 3D programs with a USB keyboard/mouse on the NEC OHCI
- P5PE-VM, Logitech Unifying receiver (046d:c52b) on the NEC card's OHCI (log: "OHCI 2 lost a TD: ED rewrite per
  transfer from now"): MENU 3D GPF on F2/F3 (all styles), MIDPLAY 3D "Stack Fault" - both at harmless instructions
  (fault really during an interrupt). PS/2 keyboard: fine. Keys arrive correctly (F1-F12 = 3A-45, no media mode).
- QEMU (UHCI, usb-kbd + usb-mouse, beta.98): 3.5 min of F2/F3/F4/F6/arrows + mouse in all 3D styles, no crash.
- Workaround: keyboard/mouse on the motherboard's USB, NEC card for sticks (its EHCI). Not pursued further (can't
  test the NEC quirk here). Idea if it comes back: keep HID off the NEC OHCI, or save the FPU in the RMCB callbacks.

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

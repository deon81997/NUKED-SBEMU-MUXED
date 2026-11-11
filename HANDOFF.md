
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

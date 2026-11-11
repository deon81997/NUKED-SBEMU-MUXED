
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
- Built clean with DJGPP gcc 12.2 (`make output/midplay.exe`).
- **Tested in 86Box** (user's Win98 VM w86, P2B-LS PII-450, DOS box full screen, `/X` dry mode). A third disk e.img was made with scripts outside the repo: an MBR+FAT16 writer with LFN entries, plus 12,020 generated .MID files. E:\BIG has 5,000 8.3 names and 5,000 names of about 95 characters (~50,000 directory entries, under the FAT 65,536 limit). E:\TREE has 50 folders x 40 files; E:\DEEP is 12 levels deep (path 235 characters).
  - `/D:E:\BIG` (10,000 files): old build about 13-14 min, new build about 8 s including 1 s of play.
  - Playlist: 10,000 rows, End shows `10000` cleanly, all song infos are read in the background within a few minutes, and F5 (name/title/length/shuffle) takes under 1 s once they are read.
  - Browser: long-named files open and play (side panel shows info); ^A over 10,000 files already in the list takes under 1 s ("No new songs here").
  - 3D view: "Song 8086/10000" OK.
  - Not finished: shuffle each-once (/H /T log on TREE\Folder 00) and the DEEP folder. Seen but not changed: the browser shows only the first 34 characters of a name, so very long names that start the same look alike (the side panel shows the title). MIDPLAY leaves DOS in the last browsed folder.

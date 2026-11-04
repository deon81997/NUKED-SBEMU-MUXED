# SC-55 pack — handoff

## Current state (SBEMU 1.0.0-beta.35-sc55)
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

## Not verified on the D810 yet
- MT-32 sound, Ctrl+Alt+F12 overlay, VESA graphics modes, automatic-setup numbers, beta.33 timer give-back change (choppy music).

## Restoring a new session
- Source from this repo. Attach: SC55PACK-HYBRID.zip (pack + ROMs + third-party DOS programs) and sc55-restore-1/2/3.zip (DJGPP in two parts, QEMU, FreeDOS boot files, JEMMEX; RESTORE.md inside).

## Next steps
1. User: run Automatic setup, MIDPLAY S panel, Ctrl+Alt+F12, MT-32 game, GAMETEST Duke run; report.
2. If music still choppy: SBDIAG reports from SC55.BAT vs SC55OLD.BAT.

## Files
- sbemu/vdiag.c (diagnostics, key player, overlay), sbemu/vmpu_sc55.c (MPU, panel 0xB0), sbemu/vmpu_mt32.cpp (Munt), sc55/sc55synth.cpp (SC55_GetLCD), main.c (/VMT32)
- tools/: sc55menu.c, sbset.c, gametest.c, midplay.c, cdgui.c, sbdiag.c, tui.h, theme.h, fontdata.h, safewr.h
- third_party/mt32emu (Munt 2.8.3), upstream/Nuked-SC55
- dosimg/ (test rig): mkdisk.py (FreeDOS FAT16 image), mkiso.py (ISO9660 + Duke IDF), vm.py (QEMU driver); QEMU built in work/qemu/b7 (configure: --python=/usr/bin/python3.12, tests/fp skipped, dtc/keycodemapdb from GitHub mirrors)
- Build: `export PATH=/home/claude/work/deps/djgpp-x/djgpp/bin:$PATH; make -j2` (delete output/*.o after header changes)

## Resume prompt
"Continue the SC-55 pack from HANDOFF.md (beta.35): clone github.com/whosdeon/Test1, branch sc55-pack, folder sc55-pack/. Rebuild with DJGPP, then continue from Next steps."

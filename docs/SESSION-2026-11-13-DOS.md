# DOS session notes, 2026-11-13 (condensed; read with HANDOFF.md)

## User rules
- Brief replies, no recaps or narration; one task per chat; report in 5 lines when asked.
- Read only HANDOFF top rules + needed sections (grep). Small command output. Ask for real-hardware results once, all in one message.
- Release per sc55-pack/HANDOFF.md "Release recipe" + "VERSION NAMES": DOS session = next `beta.NN`; pull first
  (a Win98/other session also releases: beta.101 and beta.102 came from it today); release/ keeps one zip, built
  from the newest zip with changed entries swapped (python zipfile); README.md news = 5 newest, oldest moves into
  "Older news"; root HANDOFF.md newest section on top; bump `VERSION` in sc55-pack/makefile (SBEMU.EXE + SC55MENU.EXE
  carry it, swap them too); push to main (prints "protected ref ... bypassed" but works) -> workflow tags release.

## Environment rebuild (nothing survives a session)
- Clone: add_repo deon81997/nuked-sbemu-muxed (push), `git clone --depth 1` to /home/claude/nuked-sbemu-muxed.
- DJGPP: github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2 -> /home/claude/djgpp;
  `PATH=/home/claude/djgpp/bin:$PATH make -j16` in sc55-pack (~40 s).
- Host ASan: `apt-get install gcc-13-multilib`; `sh sc55-pack/tests/m3host/run.sh [quick|crash]`.
- QEMU: `apt-get install qemu-system-x86`; FreeDOS KERNEL.SYS/COMMAND.COM from `git clone --depth 1
  https://github.com/pbatard/rufus` (res/freedos) into sc55-pack/testrig/rufus (mkdisk.py needs its ms-sys inc);
  HDPMI32I.EXE from the release zip (NUKEDSB/FILES/HDPMI). Disk: `mkdisk.py build <folder> c.img 60`.
  Driver used: /tmp/q/v.py (wraps testrig/vm.py with system qemu-system-i386, `-m` selectable, std-vga).
- 86Box: deps (cmake ninja libsdl2 openal slirp sndfile freetype png zstd xvfb mtools), clone 86Box/86Box @0935207
  to /home/claude/86box/86box, `git apply testrig/86box/86box-xpad.diff`, copy usb_xpad.c to src/, cmake per
  testrig/86box/README.md, ninja; ROMs: clone 86Box/roms to /home/claude/86box/roms. VM dir /home/claude/86vm/u1 =
  86box.cfg (testrig/86box/p2bls-uhci.cfg: P2B-LS PII-400, ViRGE/DX 4 MB VBE 1.2, ES1371) + hd.img (mkdisk 100 MB).
  Drive it with `from regress import VM` (boot/run/key/shot/screen/put/get; disk changed only while VM is off).
  Careful: `( ... ) &` subshells don't inherit a `cd` - clone into explicit paths.
- Test MIDI: `python3 testrig/midplay/genmid.py TEST.MID 120` (140 KB, 35570 events). MIDPLAY /X = no MPU, /S = plain.

## Done today
- beta.100: MIDPLAY 3D stable. Crash catcher (CRASH.TXT: signal, CPU exception, registers, modes, look/theme/player,
  song, last 50 ui_log actions; `-DCRASHKEY` build: '!' = GPF). tests/m3host: whole midplay.c on a fake DJGPP
  (fake VBE BIOS, 44 modes 320x200..1600x1200, bounds-checked LFB/bank window), ASan+UBSan+float-cast-overflow, every
  look x theme x player, F4, pause, mode switches, OOM limits; found m3_tri_raster float->int overflow -> m3_fi clamp
  (m3core.h). Textures static. mp3_buffers(w,h) before mode set; mp3_try_smaller on OOM.
  Tested QEMU (1600x1200x32, -m 12) and 86Box (ViRGE: 1600x1200x8 banked, 1280x1024x16, 1024x768x32, 320x200, 8 MB).
- beta.103: songs failed "out of memory" at 8-12 MB: 3D buffers were prepared at start even in text views. Now
  mp3_prepare only if the 3D view is on; start_index on song OOM -> to_text + mp3_free_buffers + retry; 3D comes back
  smaller. CRASH.TXT shows DPMI free memory. Tested 8 MB in QEMU and 86Box.
- Q&A: using video RAM for buffers -> not worth it (VRAM reads 10x+ slower, renderer reads fb constantly, format
  mismatch). Cheaper: 16-bit z-buffer (-25% memory) - offered, not done.
- Not done/open: SBSET.EXE in zip not rebuilt (zip copy 130 KB smaller than a fresh build; built differently).
  86Box rufus clone lives untracked in testrig/rufus.

## Task in progress (not started in code)
"Debugging aids": 1) SBEMU flight recorder: RAM ring buffer of runtime events (USB plug/unplug, dropped MIDI, late
renders, timer replays, DMA/IRQ problems, CD mounts) written to SBTRACE.TXT when DOS is idle at the prompt.
2) Per-game trace: count every SB DSP and MIDI command, flag unsupported ones. 3) BUGREP "doctor": 5-line
plain-English summary of likely problems at the top of BUGREP.TXT, plus all tool versions. Test 86Box/QEMU, release,
update README.TXT (sc55-pack/pack/README.TXT) + HANDOFF. Report in 5 lines.
- Found so far: BUGREP is sc55-pack/pack/FILES/SC55/BUGREP.BAT (67 lines, plain batch: VER, SET, MEM /C,
  CONFIG/AUTOEXEC, SBOPTS, SBLOG.TXT, SBDETAIL.TXT, SC55LOG, SBDIAG, GAMETEST, USBTEST, Win98 log, CD flags, DIR);
  MENU calls it (tools/sc55menu.c:419 `CALL %s\BUGREP.BAT %s`). The doctor needs a small EXE (e.g. extend SBDIAG or a
  new tool) that reads those files and writes the summary first. Next: look at SBEMU's resident side (main.c,
  idle/INT 28h or INT 21h hooks) for where to flush SBTRACE.TXT.

# Pad layout per game (beta.79) - test rig

Tests for padmap.c + the INT 21h EXEC hook (sbemu/int21.asm) in plain DOS.

- `keylog.asm` (NASM) - KEYLOG.COM: every keyboard byte (port 60h via INT 9) in hex for N seconds.
  `../../win98/test/keys/mkkeen.py KEYLOG.COM KEEN4E.EXE` wraps it as a fake KEEN4E.EXE (id B408B7B3).
- Test disk (`../mkdisk.py build`): `KEEN4\KEEN4E.EXE` (own id), `WOLF\RUN.EXE` + a 98402-byte `WOLF3D.EXE`
  (found by a file in its folder), `OTHER\NOGAME.EXE` (Default); `SC55\SBEMU.EXE`, `SC55\PADMAP\KEYB2JOY.CPP`
  (the user's keyb2joypad.cpp, not in the repo), optional `SC55\PADUSER.TXT`. AUTOEXEC writes `C:\STAGE.TXT`
  (KEEN / WOLF / DEF / DONE) before each program and `SBEMU /PADLOG` output at the end.
- MS-DOS 7.1 instead of FreeDOS: `mercy.py FULL.866 OUT IO.SYS MSDOS.SYS COMMAND.COM 'WINDOWS\HIMEM.SYS'` takes the
  files from the Windows 98 QuickInstall ISO (`isols.py ISO OSROOTS/1/FULL.866 full.866`; MercyPak v2 is a plain
  packer), MSDOS.SYS BootGUI=0, then `MKDISK_BOOT=msdos ../mkdisk.py build ...` (MS-DOS FAT16 boot record,
  IO.SYS first in the root). Needs `../rufus` (sparse clone of pbatard/rufus: res/freedos, src/ms-sys/inc).
- `drive.py IMG` - QEMU 8.2 (built with usbredir, pixman) + `../fakepad.py live:` pad; presses the buttons of each
  stage when C:\STAGE.TXT changes. `drive2.py IMG STEPS` - same with a step file and screenshots
  (`keen.steps`, `doom.steps`: the real games, stage REAL). `drive86.py VMDIR` - 86Box (automation, stepped time)
  with the emulated 360 pad (XPAD_STATE file), VM = `../86box/p2bls-uhci.cfg` with hdd_01_parameters 63,16,243.

Expected (Keen 4 layout): A 1D 9D, B 39 B9, X 38 B8, Y 1C 9C, stick left E0 4B / E0 CB, D-pad up E0 48 / E0 C8,
Start 1C 9C, RT 3B BB (F1). Wolf3D (folder): RB 1D, X 39, stick left 38 E0 4B ... B8 E0 CB (LAlt+Left), Y 01, LT 36.
Default: A nothing (joystick button 1), D-pad arrows, Start 1C, Back 01.

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""mkfolder.py OUT REL MSDOS SF2 MODE [MENU.EXE] - a C:\\ folder for the MENU soak / speed tests (then mkdisk.py build).
REL = extracted release zip (NUKEDSB/), MSDOS = folder with IO.SYS MSDOS.SYS COMMAND.COM WINDOWS/HIMEM.SYS (mercy.py),
SF2 = a General MIDI SoundFont (GM.SF2), MODE = hd (HDPMI32I only) or jem (JEMMEX + QPIEMU + HDPMI32I: the real-mode
SBEMU mode), MENU.EXE = the SC55MENU.EXE to test (default: the release's).
AUTOEXEC: SBEMU with the SoundFont, CTMOUSE; then C:\\BENCH.FLG -> SBDIAG START, MPUTEST, SBDIAG (C:\\SC55\\SBDIAG.TXT),
MENU 3D with M3PROF (C:\\M3PROF.TXT); else the soak loop: SC55MENU again and again until C:\\SC55\\CRASH.TXT exists."""
import os, shutil, sys
out, rel, msd, sf2, mode = sys.argv[1:6]
menu = sys.argv[6] if len(sys.argv) > 6 else None
F = os.path.join(rel, 'FILES')
if os.path.exists(out): shutil.rmtree(out)
for d in ('SC55', 'HDPMI', 'MOUSE', 'SC55/RM/JEMM'): os.makedirs(os.path.join(out, d))
for n in ('IO.SYS', 'MSDOS.SYS', 'COMMAND.COM'): shutil.copy(os.path.join(msd, n), out)
shutil.copy(os.path.join(msd, 'WINDOWS', 'HIMEM.SYS'), out)
m = open(os.path.join(out, 'MSDOS.SYS'), 'rb').read().replace(b'BootGUI=1', b'BootGUI=0')
if b'BootGUI' not in m: m = m.replace(b'[Options]', b'[Options]\r\nBootGUI=0')
m = m.replace(b'Logo=1', b'Logo=0')
open(os.path.join(out, 'MSDOS.SYS'), 'wb').write(m)
for n in os.listdir(os.path.join(F, 'SC55')): shutil.copy(os.path.join(F, 'SC55', n), os.path.join(out, 'SC55'))
for n in os.listdir(os.path.join(F, 'JEMM')): shutil.copy(os.path.join(F, 'JEMM', n), os.path.join(out, 'SC55/RM/JEMM'))
shutil.copy(os.path.join(F, 'HDPMI', 'HDPMI32I.EXE'), os.path.join(out, 'HDPMI'))
shutil.copy(os.path.join(F, 'MOUSE', 'CTMOUSE.EXE'), os.path.join(out, 'MOUSE'))
if menu: shutil.copy(menu, os.path.join(out, 'SC55', 'SC55MENU.EXE'))
shutil.copy(sf2, os.path.join(out, 'SC55', 'GM.SF2'))
def w(p, t): open(os.path.join(out, p), 'wb').write(t.replace('\n', '\r\n').encode())
w('SC55/SBOPTS.TXT', '/A220 /I5 /D1 /P330 /VSF:C:\\SC55\\GM.SF2\n')
w('SC55/TUNED.FLG', 'declined\n')
w('SC55/MENU3D.CFG', 'ON=1\nSTYLE=1\nQUALITY=0\nLOOK=0\nTHEME=0\nMODE=0 0 0\nHZ=0\n')
cfg = 'DEVICE=C:\\HIMEM.SYS\n'
if mode == 'jem': cfg = 'DEVICE=C:\\HIMEM.SYS\nDEVICE=C:\\SC55\\RM\\JEMM\\JEMMEX.EXE X2MAX=8192 NOEMS\n'
w('CONFIG.SYS', cfg + 'FILES=40\nBUFFERS=20\nDOS=HIGH,UMB\n')
jl = 'C:\\SC55\\RM\\JEMM\\JLOAD.EXE C:\\SC55\\RM\\JEMM\\QPIEMU.DLL\n' if mode == 'jem' else ''
w('AUTOEXEC.BAT', '''@ECHO OFF
PATH C:\\;C:\\SC55
%sC:\\HDPMI\\HDPMI32I.EXE -r -x
C:
CD \\SC55
SBEMU.EXE /VMPU @SBOPTS.TXT > C:\\SBLOG.TXT
C:\\MOUSE\\CTMOUSE.EXE > NUL
IF EXIST C:\\BENCH.FLG GOTO BENCH
:L
C:\\SC55\\SC55MENU.EXE C:\\SC55
IF EXIST C:\\SC55\\CRASH.TXT GOTO END
IF EXIST C:\\SC55\\MENUGO.BAT DEL C:\\SC55\\MENUGO.BAT
ECHO menu exit>> C:\\EXITS.TXT
GOTO L
:BENCH
SBDIAG START
MPUTEST /P:330
MPUTEST /P:330
SBDIAG
SET M3PROF=1
C:\\SC55\\SC55MENU.EXE C:\\SC55
SET M3PROF=
ECHO BENCHDONE> C:\\BENCH.OK
:END
''' % jl)

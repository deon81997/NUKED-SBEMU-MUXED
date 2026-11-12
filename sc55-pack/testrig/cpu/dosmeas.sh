#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# dosmeas.sh LABEL SBEMU.EXE "SBEMU options" : one 86Box run, appends to /tmp/dosmeas.txt
L=$1; X=$2; O=$3; R=/home/claude/padtest/cpu/root-$L; V=/home/claude/padtest/cpu/vm-$L
rm -rf $R; cp -r /home/claude/padtest/cpu/root $R; cp $X $R/SC55/SBEMU.EXE
python3 - "$R" "$L" "$O" <<'PY'
import sys
R,L,O=sys.argv[1:4]
A=[r'@ECHO OFF', r'SET BLASTER=A220 I5 D1 T6 P330', r'C:\HDPMI\HDPMI32I.EXE -r -x']
if L!='none': A.append(r'C:\SC55\SBEMU.EXE %s > C:\SB.TXT'%O)
for i in range(2): A.append(r'C:\IDLEMTR.EXE 8 C:\M.LOG %s%d'%(L,i))
A.append(r'ECHO DONE> C:\STAGE.TXT')
open(R+'/AUTOEXEC.BAT','w',newline='').write('\r\n'.join(A)+'\r\n')
PY
mkdir -p $V/nvr; cp /home/claude/padtest/86vm/nvr/* $V/nvr/; echo "export XPAD=1 XPAD_STATE=/tmp/xpad1.st" > $V/env
"$(dirname "$0")/cpurun.sh" $R $V 1500 | grep -v elapsed >> /tmp/dosmeas.txt

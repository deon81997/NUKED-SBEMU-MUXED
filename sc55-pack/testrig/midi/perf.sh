#!/bin/bash
# perf.sh NAME SBEMU.EXE "SBEMU options" [seconds]: 86Box ASUS P2B-LS, Pentium II 450, ES1371 (testrig/86box): SBEMU,
# SBDIAG START, MIDPLAY of a busy song (genmid.py, 16 channels) for 40 s, SBDIAG report -> perf-NAME.txt here
set -e
H=$(cd $(dirname $0); pwd); TR=$(dirname $H); P=$(dirname $TR); R=/home/claude/rel/NUKEDSB/FILES
V=/tmp/perf-$1; rm -rf $V; mkdir -p $V/c/SC55
cp $TR/rufus/res/freedos/KERNEL.SYS $TR/rufus/res/freedos/COMMAND.COM $V/c/
cp $R/JEMM/JEMMEX.EXE $R/HDPMI/HDPMI32I.EXE $V/c/SC55/
cp $P/output/sbdiag.exe $V/c/SC55/SBDIAG.EXE; cp $P/output/midplay.exe $V/c/SC55/MIDPLAY.EXE; cp $2 $V/c/SC55/SBEMU.EXE
for f in ${PREFILES:-}; do cp $f $V/c/SC55/; done        # PREFILES="a b": extra files into C:\SC55
python3 $TR/midplay/genmid.py $V/c/T.MID 60 2 > /dev/null
printf 'DEVICE=C:\\SC55\\JEMMEX.EXE NOEMS\r\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n' > $V/c/CONFIG.SYS
printf "@ECHO OFF\r\nC:\\SC55\\HDPMI32I -r\r\nCD \\SC55\r\n${PRECMD:-REM}\r\nSBEMU $3 > C:\\SB.TXT\r\nSBDIAG START > NUL\r\nMIDPLAY /S /Q:${4:-40} C:\\T.MID > NUL\r\nSBDIAG > C:\\DIAG.TXT\r\nECHO DONE > C:\\DONE.TXT\r\n" > $V/c/AUTOEXEC.BAT
python3 $TR/mkdisk.py build $V/c $V/hd.img 100 > /dev/null
sed -e 's/^cpu_speed = .*/cpu_speed = 450000000/' -e 's/^cpu_multi = .*/cpu_multi = 4.5/' $TR/86box/p2bls-uhci.cfg > $V/86box.cfg
bash $TR/86box/run.sh $V > /dev/null
for i in $(seq 40); do sleep 2; python3 $TR/86box/a.py $V/a.sock @screen 2>/dev/null | grep -q 'Press F1' && { python3 $TR/86box/a.py $V/a.sock 'key down 0x3b' 'key up 0x3b' > /dev/null; break; }; done
for i in $(seq 140); do sleep 2; python3 $TR/mkdisk.py get $V/hd.img DONE.TXT /dev/null >/dev/null 2>&1 && break; done
pkill -f "86Box.*$V" || true; sleep 1
python3 $TR/mkdisk.py get $V/hd.img DIAG.TXT $H/perf-$1.txt > /dev/null && grep -a "interrupts (\|uses .*CPU\|needs .*real time\|bytes per second\|Measured" $H/perf-$1.txt

#!/bin/sh
N=${1:-X}
/tmp/vmrun.sh "D:\\WJOY.EXE 3 D:\\WJ$N.LOG" 10
/tmp/vmrun.sh "regedit /e D:\\NJ$N.REG \"HKEY_LOCAL_MACHINE\\Software\\NUKED SB-EMU\"" 6
/tmp/dget.sh WJ$N.LOG; grep -v "caps err" /tmp/WJ$N.LOG | sed 's/ | [0-9]*: err 165//g' | head -5
/tmp/dget.sh NJ$N.REG; tr -d '\0\r' < /tmp/NJ$N.REG | grep '"'

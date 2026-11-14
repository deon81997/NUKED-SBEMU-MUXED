#!/bin/sh
# run.sh ZIP [EXE...] - MENU matrix part 3: does MENU see a loaded SBEMU? (beta.117; user report: "Not loaded" after SC55)
# QEMU MS-DOS 7.1 (store-dos71), cases: none (expect Not loaded), pm = SC55.BAT + MENU.BAT's HDPMI line,
# pmhand = SC55.BAT + SC55MENU by hand, rm = JEMMEX + SC55JEM.BAT, rmonly = SC55JEM.BAT /PM0
# (no protected-mode trap: the MPU probe cannot see SBEMU, AMIS must). Checks SC55MENU /STATUS (exit 0 loaded, 1 not, 2 unknown).
# Example (repo sc55-pack, after make): sh testrig/menustat/run.sh ../release/NUKED-SB-EMU-*.zip output/sbemu.exe output/sc55menu.exe
set -u
H=$(cd "$(dirname "$0")" && pwd); W=/tmp/menustat
ZIP=$1; shift
sh "$H/mkimg.sh" "$ZIP" "$W" "$@" || exit 2
F=0
for c in none pm pmhand rm rmonly; do
  img=pm; case $c in rm*) img=rm ;; esac
  printf '@ECHO OFF\r\nCALL C:\\MSTAT.BAT %s\r\n' $c > "$W/T_$c.BAT"
  out=$("$H/../common/run.sh" -t 150 "$W/$img.img" "$W/T_$c.BAT" 2>&1); r=$?
  echo "$c: $(echo "$out" | grep "MENU SBEMU" | head -1) -> $([ $r = 0 ] && echo PASS || { F=1; echo FAIL; })"
done
exit $F

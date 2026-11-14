#!/bin/sh
# run.sh ZIP [-e 86box -c CFG] CASES... - WCSET tests (beta.120): WCSET list/bench/on/off, MENU 3D (M3PROF) and
# MIDPLAY 3D (/T stats) fps with WCSET=OFF vs AUTO, SBDETAIL [wc] lines. CASES: pm rm. Run from sc55-pack after make.
# Example: sh testrig/wcset/run.sh ../release/NUKED-SB-EMU-*.zip pm rm ; EMU="-e 86box -c /home/claude/vms/dos71/86box/86box.cfg" ...
set -u
H=$(cd "$(dirname "$0")" && pwd); W=/tmp/wcsetrig
ZIP=$1; shift
sh "$H/../menustat/mkimg.sh" "$ZIP" "$W" output/sc55menu.exe output/midplay.exe output/wcset.exe || exit 2
T=$(mktemp -d); mkdir -p "$T/WCT"; cp output/wcset.exe "$T/WCT/WCSET.EXE"; cp "$H/../midplay/METRO.MID" "$T/WCT/"
for m in pm rm; do mcopy -o -s -i "$W/$m.img@@32256" "$T/WCT" ::/; mcopy -o -i "$W/$m.img@@32256" "$H/WCT.BAT" ::/; done
rm -rf "$T"
for c in "$@"; do
  printf '@ECHO OFF\r\nCALL C:\\WCT.BAT %s\r\n' $c > "$W/T_$c.BAT"
  "$H/../common/run.sh" ${EMU:-} -t ${TMO:-400} "$W/$c.img" "$W/T_$c.BAT" > "$W/$c.log" 2>&1; echo "$c: exit $?"
done

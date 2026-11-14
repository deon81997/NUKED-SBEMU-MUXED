#!/bin/sh
# run.sh ZIP [EXE...] - beta.120 shared disk IRQ (P5PE-VM SATA "Enhanced") check in QEMU MS-DOS 7.1 (store-dos71):
# cases plain (PIIX IDE legacy: no warning) and ahci at PCI addr 2..5 (one shares the AC97 IRQ: expect the warning).
# Example (sc55-pack, after make): sh testrig/sata/run.sh ../release/NUKED-SB-EMU-*.zip output/sbemu.exe
set -u
H=$(cd "$(dirname "$0")" && pwd); W=/tmp/sata
ZIP=$1; shift
sh "$H/../menustat/mkimg.sh" "$ZIP" "$W" "$@" || exit 2
F=0
for c in plain 6 7 8 9; do
  x=""; [ $c != plain ] && x="-- -device ich9-ahci,addr=$c"
  out=$("$H/../common/run.sh" -t 150 "$W/pm.img" "$H/SATA.BAT" $x 2>&1); r=$?
  echo "== $c ($r)"; echo "$out" | grep -v "^ *$\|^---" | tr -d '\r' | cut -c1-150
  [ $r = 0 ] || F=1
done
exit $F

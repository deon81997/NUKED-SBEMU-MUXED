#!/bin/bash
# notetest.sh "SBEMU OPTIONS" MIDI-FILE... : dropped-note test in QEMU (FreeDOS + JEMMEX/QPIEMU + HDPMI32I + SBEMU).
# For each file: SBDIAG START, MIDPLAY /S /T (bytes + note-ons it sent), SBDIAG (MIDI bytes SBEMU got / dropped).
# Needs: PACK=folder with JEMMEX.EXE JLOAD.EXE QPIEMU.DLL HDPMI32I.EXE SBEMU.EXE SBDIAG.EXE MIDPLAY.EXE [GM.SF2 ROMS\]
# Env: QSECS (per song, default 45), CPU (qemu -cpu, default pentium2), ICOUNT (e.g. 3 = slow PC: -icount shift=3),
#      OUT (results folder, default ./nt-out). Result: $OUT/RESULT.TXT (all logs) and a summary from notetest.py.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); RIG=$(dirname "$HERE")
OPTS=$1; shift
OUT=${OUT:-$PWD/nt-out}; QSECS=${QSECS:-45}
W=$(mktemp -d); mkdir -p "$W/c/SC55" "$W/c/MID" "$OUT"
cp "$RIG"/rufus/res/freedos/KERNEL.SYS "$RIG"/rufus/res/freedos/COMMAND.COM "$W/c/"
cp -r "${PACK:?set PACK}"/* "$W/c/SC55/"
printf '\xb0\x00\xe6\xf4\xcd\x20' > "$W/c/QUIT.COM"          # isa-debug-exit: QEMU stops
printf 'DEVICE=C:\\SC55\\JEMMEX.EXE NOEMS\r\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n' > "$W/c/CONFIG.SYS"
{
  printf '@ECHO OFF\r\nC:\\SC55\\JLOAD C:\\SC55\\QPIEMU.DLL\r\nC:\\SC55\\HDPMI32I -r\r\nCD \\SC55\r\nSBEMU /VMPU %s > C:\\SBEMU.TXT\r\n' "$OPTS"
  i=0
  for f in "$@"; do i=$((i+1)); n=$(printf 'T%02d.MID' $i); cp "$f" "$W/c/MID/$n"
    printf 'ECHO ==== %s %s >> C:\\RESULT.TXT\r\nSBDIAG START >> C:\\RESULT.TXT\r\nMIDPLAY /S /T:C:\\RESULT.TXT /Q:%s C:\\MID\\%s > NUL\r\nSBDIAG >> C:\\RESULT.TXT\r\nTYPE SBDIAG.TXT >> C:\\RESULT.TXT\r\n' "$n" "$(basename "$f")" "$QSECS" "$n"
  done
  printf 'C:\\QUIT.COM\r\n'
} > "$W/c/AUTOEXEC.BAT"
python3 "$RIG/mkdisk.py" build "$W/c" "$W/c.img" 120 > /dev/null
IC=""; [ -n "$ICOUNT" ] && IC="-icount shift=$ICOUNT,sleep=on"
timeout $(( ($# + 1) * (QSECS + 60) + 120 )) qemu-system-i386 -machine pc -cpu ${CPU:-pentium2} -m 64 $IC \
  -drive file="$W/c.img",format=raw,if=ide,index=0 -boot c -audiodev none,id=snd -device AC97,audiodev=snd \
  -display none -vga std -device isa-debug-exit,iobase=0xf4,iosize=1 > "$W/qemu.log" 2>&1 || true
python3 "$RIG/mkdisk.py" get "$W/c.img" 'RESULT.TXT' "$OUT/RESULT.TXT" || echo "no RESULT.TXT (did it boot?)"
python3 "$RIG/mkdisk.py" get "$W/c.img" 'SBEMU.TXT' "$OUT/SBEMU.TXT" || true
rm -rf "$W"
python3 "$HERE/notetest.py" "$OUT/RESULT.TXT"

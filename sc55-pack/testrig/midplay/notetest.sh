#!/bin/bash
# notetest.sh "SBEMU OPTIONS" MIDI-FILE... : dropped-note test in QEMU (FreeDOS + JEMMEX/QPIEMU + HDPMI32I + SBEMU).
# For each file: SBDIAG START, MIDPLAY /S /T (bytes + note-ons it sent), SBDIAG (MIDI bytes SBEMU got / dropped).
# Needs: PACK=folder with JEMMEX.EXE JLOAD.EXE QPIEMU.DLL HDPMI32I.EXE SBEMU.EXE SBDIAG.EXE MIDPLAY.EXE [GM.SF2 ROMS\]
# Env: QSECS (per song, default 45), CPU (qemu -cpu, default pentium2), ICOUNT (e.g. 3 = slow PC: -icount shift=3),
#      OUT (results folder, default ./nt-out), WAV (file: record the sound card; uses QEMU's HDA, because QEMU's AC97
#      gave only silence in the recording), SND (QEMU sound device options), SCREEN=1 (SBEMU's synth / ROM set lines -> $OUT/SCREEN.TXT),
#      TAIL / WARM (seconds of silence
#      after the last song / before the first: in QEMU the SC-55 starts up and renders late, so recordings need both). Result: $OUT/RESULT.TXT (all logs) and a summary from notetest.py.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); RIG=$(dirname "$HERE")
OPTS=$1; shift
OUT=${OUT:-$PWD/nt-out}; QSECS=${QSECS:-45}
W=$(mktemp -d); mkdir -p "$W/c/SC55" "$W/c/MID" "$OUT"
cp "$RIG"/rufus/res/freedos/KERNEL.SYS "$RIG"/rufus/res/freedos/COMMAND.COM "$W/c/"
cp -r "${PACK:?set PACK}"/* "$W/c/SC55/"
printf '\xb0\x00\xe6\xf4\xcd\x20' > "$W/c/QUIT.COM"          # isa-debug-exit: QEMU stops
printf 'DEVICE=C:\\SC55\\JEMMEX.EXE NOEMS\r\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n' > "$W/c/CONFIG.SYS"
sil() { python3 -c "import struct,sys;t=int(sys.argv[1])*96*2;v=bytes([t>>21&127|128,t>>14&127|128,t>>7&127|128,t&127]);d=v+b'\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00';open(sys.argv[2],'wb').write(b'MThd'+struct.pack('>IHHH',6,0,1,96)+b'MTrk'+struct.pack('>I',len(d))+d)" "$1" "$2"; }
[ -n "$TAIL" ] && sil "$TAIL" "$W/c/MID/TAIL.MID"
[ -n "$WARM" ] && sil "$WARM" "$W/c/MID/WARM.MID"
{
  printf '@ECHO OFF\r\nC:\\SC55\\JLOAD C:\\SC55\\QPIEMU.DLL\r\nC:\\SC55\\HDPMI32I -r\r\nCD \\SC55\r\nSBEMU /VMPU %s > C:\\SBEMU.TXT\r\n' "$OPTS"
  [ -n "$WARM" ] && printf 'MIDPLAY /S /N /Q:%s C:\\MID\\WARM.MID > NUL\r\n' "$WARM"   # the synth starts up (slow in QEMU) before the songs
  i=0
  for f in "$@"; do i=$((i+1)); n=$(printf 'T%02d.MID' $i); cp "$f" "$W/c/MID/$n"
    printf 'ECHO ==== %s %s >> C:\\RESULT.TXT\r\nSBDIAG START >> C:\\RESULT.TXT\r\nMIDPLAY /S /T:C:\\RESULT.TXT /Q:%s C:\\MID\\%s > NUL\r\nSBDIAG >> C:\\RESULT.TXT\r\nTYPE SBDIAG.TXT >> C:\\RESULT.TXT\r\n' "$n" "$(basename "$f")" "$QSECS" "$n"
  done
  [ -n "$TAIL" ] && printf 'MIDPLAY /S /N /Q:%s C:\\MID\\TAIL.MID > NUL\r\n' "$TAIL"   # silence before QEMU stops: a synth that lags in QEMU is still recorded to the end
  printf 'C:\\QUIT.COM\r\n'
} > "$W/c/AUTOEXEC.BAT"
python3 "$RIG/mkdisk.py" build "$W/c" "$W/c.img" 120 > /dev/null
AD="none,id=snd"; SND=${SND:--device AC97,audiodev=snd}
[ -n "$WAV" ] && { AD="wav,path=$WAV,id=snd"; [ "$SND" = "-device AC97,audiodev=snd" ] && SND="-device intel-hda -device hda-output,audiodev=snd"; }
IC=""; [ -n "$ICOUNT" ] && IC="-icount shift=$ICOUNT,sleep=on"
MON=""; [ -n "$SCREEN" ] && MON="-monitor unix:$W/mon,server,nowait"
if [ -n "$SCREEN" ]; then   # SBEMU's start-up lines (which synth / ROM set it really loaded) from the text screen
  python3 - "$W/mon" "$OUT/SCREEN.TXT" <<'PYEOF' &
import socket, sys, time, re
seen = []; t0 = time.time()
while time.time() - t0 < 900:
    time.sleep(2)
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(3); time.sleep(0.2); s.recv(65536)
        s.sendall(b'xp /4000xb 0xb8000\n'); buf = b''; end = time.time() + 3
        while time.time() < end:
            try: c = s.recv(65536)
            except socket.timeout: break
            if not c: break
            buf += c
            if buf.count(b'\n') > 500: break
        s.close()
    except OSError:
        if seen: break
        continue
    b = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', buf.decode(errors='replace'))]
    txt = bytes(b[0::2])
    for r in range(len(txt) // 80):
        ln = txt[r * 80:r * 80 + 80].decode('cp437').rstrip()
        if ('MPU' in ln or 'rror' in ln or 'SoundFont' in ln or 'MT-32' in ln or 'ROM' in ln) and ln not in seen:
            seen.append(ln); open(sys.argv[2], 'a').write(ln + '\n')
PYEOF
fi
timeout $(( ($# + 1) * (QSECS + 60) + 120 + ${TAIL:-0} + ${WARM:-0} )) qemu-system-i386 -machine pc -cpu ${CPU:-pentium2} -m 64 $IC \
  -drive file="$W/c.img",format=raw,if=ide,index=0 -boot c -audiodev $AD $SND \
  -display none -vga std $MON -device isa-debug-exit,iobase=0xf4,iosize=1 > "$W/qemu.log" 2>&1 || true
python3 "$RIG/mkdisk.py" get "$W/c.img" 'RESULT.TXT' "$OUT/RESULT.TXT" || echo "no RESULT.TXT (did it boot?)"
python3 "$RIG/mkdisk.py" get "$W/c.img" 'SBEMU.TXT' "$OUT/SBEMU.TXT" || true
wait 2>/dev/null; rm -rf "$W"
python3 "$HERE/notetest.py" "$OUT/RESULT.TXT"

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# hostsweep.sh [FUZZ]: MIDPLAY's host build (-DHOSTTEST) under AddressSanitizer + UBSan (any report = fail):
#   1. --queue (play order)   2. fmtcheck.py (MID/KAR/RMI/MUS/HMP/XMI, transpose, A-B, sleep)
#   3. busy generated songs with speed / transpose / A-B   4. FUZZ (default 300) damaged copies of every format
# Run from anywhere; needs gcc and python3. Exit code 0 = clean.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC=$(cd "$HERE/../.." && pwd)/tools/midplay.c
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
gcc -O1 -g -Wall -Wno-unused -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -DHOSTTEST -o "$W/mphost" "$SRC" -lm
"$W/mphost" --queue
python3 "$HERE/fmtcheck.py" "$W/mphost" "$W/fmt" | tail -1
python3 "$HERE/genmid.py" "$W/busy.mid" 30 3 > /dev/null
for a in "" "150" "--pitch 7" "--pitch -12 --pitchat 5000 3" "--ab 4000 9000 3" "--sleep 12" "--info"; do
  "$W/mphost" "$W/busy.mid" $a > "$W/out.txt" 2> "$W/err.txt" || { echo "FAIL busy.mid $a"; tail -5 "$W/err.txt"; exit 1; }
done
echo "busy song with options: clean"
python3 - "$W" "${1:-300}" <<'EOF'
import os, random, subprocess, sys
w, n = sys.argv[1], int(sys.argv[2]); r = random.Random(7); bad = 0; runs = 0
src = [os.path.join(w, 'fmt', f) for f in sorted(os.listdir(os.path.join(w, 'fmt')))] + [os.path.join(w, 'busy.mid')]
for f in src:
    d = open(f, 'rb').read()
    for i in range(n):
        b = bytearray(d)
        for _ in range(r.randint(1, 12)):
            k = r.randrange(len(b)); m = r.randrange(4)
            if m == 0: b[k] = r.randrange(256)
            elif m == 1: b[k] ^= 0x80
            elif m == 2: del b[k:k + r.randint(1, 40)]
            else: b[k:k] = bytes(r.randrange(256) for _ in range(r.randint(1, 8)))
        if r.random() < 0.1: b = b[:r.randrange(max(1, len(b)))]
        g = os.path.join(w, 'fz' + os.path.splitext(f)[1]); open(g, 'wb').write(b)
        try: p = subprocess.run([os.path.join(w, 'mphost'), g, '--info', '--pitch', '3'], capture_output=True, timeout=60)
        except subprocess.TimeoutExpired: print('HANG', f, i); bad += 1; continue
        runs += 1
        if p.returncode not in (0, 1) or b'Sanitizer' in p.stderr or b'runtime error' in p.stderr:
            bad += 1; open(os.path.join(w, '..', f'fuzzfail{bad}' + os.path.splitext(f)[1]), 'wb').write(b)
            print('FAIL', f, i, p.returncode, p.stderr.decode(errors='replace')[-400:])
print(f'fuzz: {runs} damaged files, {bad} problems'); sys.exit(1 if bad else 0)
EOF
echo "hostsweep: all clean"

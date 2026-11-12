#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""mercy.py PACK.866 OUTDIR [name ...] - extract files (all, or the names given, case-insensitive) from a MercyPak v2 pack"""
import struct, sys, os
f = open(sys.argv[1], 'rb'); out = sys.argv[2]; want = {n.upper() for n in sys.argv[3:]}
assert f.read(4) == b'MRCY'
nd, nf = struct.unpack('<II', f.read(8))
for _ in range(nd):
    attr, l = f.read(2); f.read(l)
done = 0
while done < nf:
    hdr = f.read(1)
    if not hdr: break
    names = []
    for _ in range(hdr[0]):
        l = f.read(1)[0]; n = f.read(l).decode('latin-1'); f.read(1 + 4); names.append(n)
    size = struct.unpack('<I', f.read(4))[0]
    hit = [n for n in names if not want or n.upper() in want]
    if hit:
        data = f.read(size)
        for n in hit:
            p = os.path.join(out, *n.split('\\')); os.makedirs(os.path.dirname(p) or out, exist_ok=True)
            open(p, 'wb').write(data); print(n, size)
    else: f.seek(size, 1)
    done += len(names)

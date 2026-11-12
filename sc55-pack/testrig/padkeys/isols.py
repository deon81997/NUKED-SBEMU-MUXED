#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""isols.py ISO [path] [out] - list an ISO9660 directory (recursively with -r) or extract a file"""
import struct, sys
f = open(sys.argv[1], 'rb')
def sec(n, k=1): f.seek(n * 2048); return f.read(2048 * k)
pvd = sec(16); root = pvd[156:190]
def ents(rec):
    lba, size = struct.unpack('<I', rec[2:6])[0], struct.unpack('<I', rec[10:14])[0]
    d = sec(lba, (size + 2047) // 2048); i = 0; out = []
    while i < size:
        l = d[i]
        if l == 0: i = (i // 2048 + 1) * 2048; continue
        r = d[i:i + l]; nl = r[32]; name = r[33:33 + nl].decode('latin-1').split(';')[0]
        if name not in ('\x00', '\x01'): out.append((name, r))
        i += l
    return out
def walk(rec, pre, rec_all):
    for n, r in ents(rec):
        isdir = r[25] & 2; sz = struct.unpack('<I', r[10:14])[0]
        print('%s%s%s %d' % (pre, n, '/' if isdir else '', sz))
        if isdir and rec_all: walk(r, pre + n + '/', rec_all)
def find(path):
    rec = root
    for part in [p for p in path.split('/') if p]:
        rec = [r for n, r in ents(rec) if n.upper() == part.upper()][0]
    return rec
if len(sys.argv) == 2 or sys.argv[2] == '-r': walk(root, '', len(sys.argv) > 2)
elif len(sys.argv) == 3: walk(find(sys.argv[2]), sys.argv[2] + '/', False)
else:
    r = find(sys.argv[2]); lba, sz = struct.unpack('<I', r[2:6])[0], struct.unpack('<I', r[10:14])[0]
    f.seek(lba * 2048); open(sys.argv[3], 'wb').write(f.read(sz))

# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
import os, sys
# expected "L path size fnv64" / "D path" lines for a mounted tree (matches fstest)
root = sys.argv[1]
def fnv(p):
    h = 1469598103934665603
    with open(p, 'rb') as f:
        for b in f.read():
            h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h
for d, ds, fs in os.walk(root):
    rel = d[len(root):]
    for n in ds: print('D %s/%s' % (rel, n))
    for n in fs:
        p = os.path.join(d, n)
        print('L %s/%s %d %016x' % (rel, n, os.path.getsize(p), fnv(p)))

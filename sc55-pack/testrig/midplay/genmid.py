#!/usr/bin/env python3
"""genmid.py OUT.MID [seconds] [density]: busy format-1 test song: 16 channels, 16th notes (density x),
program changes, volume/pan/expression sweeps, pitch bends, a tempo change."""
import struct, sys, random
out = sys.argv[1]; secs = int(sys.argv[2]) if len(sys.argv) > 2 else 90; dens = int(sys.argv[3]) if len(sys.argv) > 3 else 1
DIV = 480; r = random.Random(3)
def vl(v):
    b = [v & 0x7F]; v >>= 7
    while v: b.insert(0, 0x80 | (v & 0x7F)); v >>= 7
    return bytes(b)
def track(evs):
    evs.sort(key=lambda e: e[0]); d = b''; last = 0
    for t, data in evs: d += vl(t - last) + data; last = t
    d += vl(0) + b'\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(d)) + d
beats = secs * 2
t0 = [(0, b'\xff\x51\x03' + (500000).to_bytes(3, 'big')), (0, b'\xff\x03\x0cNUKED STRESS'), (beats // 2 * DIV, b'\xff\x51\x03' + (400000).to_bytes(3, 'big'))]
trks = [track(t0)]
for ch in range(16):
    e = [(0, bytes([0xC0 | ch, r.randrange(128)])), (0, bytes([0xB0 | ch, 7, 100])), (0, bytes([0xB0 | ch, 10, ch * 8]))]
    step = DIV // (4 * dens); t = 0
    while t < beats * DIV:
        n = (36 if ch == 9 else 40) + r.randrange(40)
        e.append((t, bytes([0x90 | ch, n, 60 + r.randrange(60)]))); e.append((t + step - 10, bytes([0x80 | ch, n, 0])))
        if t % DIV == 0: e.append((t, bytes([0xB0 | ch, 11, 70 + r.randrange(57)])))
        if t % (DIV * 4) == 0: e.append((t, bytes([0xE0 | ch, 0, 0x30 + r.randrange(32)])))
        t += step
    trks.append(track(e))
open(out, 'wb').write(b'MThd' + struct.pack('>IHHH', 6, 1, len(trks), DIV) + b''.join(trks))
print(out, sum(len(x) for x in trks))

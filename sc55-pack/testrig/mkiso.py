#!/usr/bin/env python3
"""mkiso.py - minimal ISO9660 image: one folder SUPPORT with one file (for Duke 3D Atomic's CD check).
   mkiso.py out.iso [VOLUME]"""
import struct, sys

S = 2048
def both16(v): return struct.pack('<H', v) + struct.pack('>H', v)
def both32(v): return struct.pack('<I', v) + struct.pack('>I', v)
DATE7 = bytes([96, 12, 11, 12, 0, 0, 0])          # 1996-12-11 12:00
DATE17 = b'1996121112000000' + b'\x00'

def rec(name, lba, size, isdir):
    n = len(name)
    r = bytes([0]) + bytes([0]) + both32(lba) + both32(size) + DATE7 + bytes([2 if isdir else 0, 0, 0]) + both16(1) + bytes([n]) + name
    if len(r) % 2: r += b'\x00'
    return bytes([len(r)]) + r[1:]

def main():
    out = sys.argv[1]; vol = (sys.argv[2] if len(sys.argv) > 2 else 'DUKE3D').encode()
    data = b'Duke Nukem 3D Atomic Edition test CD\r\n'
    ROOT, SUP, FILE, LPT, MPT = 20, 21, 22, 18, 19
    FILE2 = 23
    IDFSIZE = 479985668                      # what Duke 3D Atomic 1.5 checks (testcd() in GAME.C)
    total = 23 + (IDFSIZE + S - 1) // S
    root = rec(b'\x00', ROOT, S, 1) + rec(b'\x01', ROOT, S, 1) + rec(b'SUPPORT', SUP, S, 1)
    sup = rec(b'\x00', SUP, S, 1) + rec(b'\x01', ROOT, S, 1) + rec(b'DUKE3D.IDF;1', FILE2, IDFSIZE, 0) + rec(b'README.TXT;1', FILE, len(data), 0)
    def pt(big):
        p = lambda v: struct.pack('>I' if big else '<I', v)
        h = lambda v: struct.pack('>H' if big else '<H', v)
        return bytes([1, 0]) + p(ROOT) + h(1) + b'\x00\x00' + bytes([7, 0]) + p(SUP) + h(1) + b'SUPPORT\x00'
    ptl = pt(False)
    pvd = bytearray(S)
    pvd[0] = 1; pvd[1:6] = b'CD001'; pvd[6] = 1
    pvd[8:40] = b' ' * 32; pvd[40:72] = vol.ljust(32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1); pvd[124:128] = both16(1); pvd[128:132] = both16(S)
    pvd[132:140] = both32(len(ptl))
    pvd[140:144] = struct.pack('<I', LPT); pvd[148:152] = struct.pack('>I', MPT)
    pvd[156:190] = rec(b'\x00', ROOT, S, 1)
    for a, b in ((190, 318), (318, 446), (446, 574), (574, 702)): pvd[a:b] = b' ' * (b - a)
    pvd[702:739] = b' ' * 37; pvd[739:776] = b' ' * 37; pvd[776:813] = b' ' * 37
    for i in range(4): pvd[813 + 17 * i:830 + 17 * i] = DATE17 if i < 2 else b'0' * 16 + b'\x00'
    pvd[881] = 1
    term = bytearray(S); term[0] = 255; term[1:6] = b'CD001'; term[6] = 1
    def sec(b): return bytes(b) + bytes(S - len(b))
    img = bytes(16 * S) + sec(pvd) + sec(term) + sec(ptl) + sec(pt(True)) + sec(root) + sec(sup) + sec(data)
    with open(out, 'wb') as f:
        f.write(img); f.truncate(total * S)       # the big IDF file: zeros, sparse
    print(out, total * S)

main()

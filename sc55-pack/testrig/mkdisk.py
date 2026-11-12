#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""mkdisk.py - bootable FreeDOS FAT16 hard-disk image from a host folder, and file extraction.
   mkdisk.py build <folder> <image> [MB]      folder becomes C:\\ (KERNEL.SYS must be in it)
                                              MKDISK_BOOT=msdos: MS-DOS 7.1 boot record (IO.SYS, MSDOS.SYS in the
                                              folder, e.g. from a Windows 98 QuickInstall FULL.866)
   mkdisk.py get <image> <DOS path> <out>     copy a file out of the image (e.g. SC55\\GAMETEST.TXT)
   mkdisk.py ls <image> [DOS dir]"""
import os, re, struct, sys

INC = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rufus/src/ms-sys/inc')
def carray(fn):
    s = open(os.path.join(INC, fn)).read()
    s = s[s.index('{') + 1:s.rindex('}')]
    return bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]{2}', s))

SPT, HEADS, SEC = 63, 16, 512
SPC, RES, NFAT, ROOTN = 4, 1, 2, 512
import os as _os
SPC = int(_os.environ.get("MKDISK_SPC", SPC))   # sectors per cluster (MKDISK_SPC=32: disks up to 1 GB)

def dosname(n):
    n = n.upper()
    b, e = (n.rsplit('.', 1) + [''])[:2] if '.' in n else (n, '')
    return (b[:8].ljust(8) + e[:3].ljust(3)).encode('latin-1')

def build(folder, image, mb=120):
    cyl = mb * 1024 * 1024 // (HEADS * SPT * SEC)
    total = cyl * HEADS * SPT
    psec = total - SPT
    clusters_est = psec // SPC
    fatsz = (clusters_est * 2 + SEC - 1) // SEC
    rootsec = ROOTN * 32 // SEC
    data0 = RES + NFAT * fatsz + rootsec
    nclus = (psec - data0) // SPC
    assert 4085 <= nclus < 65525, nclus
    csize = SPC * SEC
    fat = [0] * (nclus + 2); fat[0] = 0xFFF8; fat[1] = 0xFFFF
    data = bytearray(nclus * csize)
    nxt = [2]

    def alloc(nbytes):
        n = max(1, (nbytes + csize - 1) // csize)
        first = nxt[0]
        for i in range(n): fat[first + i] = first + i + 1 if i < n - 1 else 0xFFFF
        nxt[0] += n
        assert nxt[0] <= nclus + 2, 'disk full'
        return first

    def write(clus, payload):
        o = (clus - 2) * csize
        data[o:o + len(payload)] = payload

    def entry(name, attr, clus, size):
        return dosname(name) + bytes([attr]) + bytes(10) + struct.pack('<HHHHI', 0, 0x5A21, clus, 0, size)[0:4] + struct.pack('<HI', clus, size) if False else \
            dosname(name) + bytes([attr]) + bytes(10) + struct.pack('<HH', 0, 0x5A21) + struct.pack('<HI', clus, size)

    def dodir(path, parent_clus):
        names = sorted(os.listdir(path))
        if parent_clus == 0 and os.environ.get('MKDISK_BOOT') == 'msdos':   # IO.SYS first: the boot record loads it
            names = [n for n in ('IO.SYS', 'MSDOS.SYS') if n in names] + [n for n in names if n not in ('IO.SYS', 'MSDOS.SYS')]
        ents = []
        sub = []
        for n in names:
            p = os.path.join(path, n)
            if os.path.isdir(p): sub.append((n, p))
            else:
                d = open(p, 'rb').read()
                c = alloc(len(d)) if d else 0
                if d: write(c, d)
                ents.append(entry(n, 0x20, c, len(d)))
        for n, p in sub:
            nent = len(os.listdir(p)) + 2
            c = alloc(nent * 32)
            ents.append(entry(n, 0x10, c, 0))
            body = entry('.', 0x10, c, 0)[:0] + b'.          ' + bytes([0x10]) + bytes(10) + struct.pack('<HH', 0, 0x5A21) + struct.pack('<HI', c, 0)
            body += b'..         ' + bytes([0x10]) + bytes(10) + struct.pack('<HH', 0, 0x5A21) + struct.pack('<HI', parent_clus, 0)
            body += dodir(p, c)
            write(c, body)
        return b''.join(ents)

    root = dodir(folder, 0)
    assert len(root) <= ROOTN * 32, 'root too big'
    # boot sector
    bs = bytearray(SEC)
    ms = os.environ.get('MKDISK_BOOT') == 'msdos'
    bs[0:3] = b'\xEB\x3C\x90'; bs[3:11] = b'MSWIN4.1' if ms else b'FRDOS4.1'
    struct.pack_into('<HBHBHHBHHHII', bs, 11, SEC, SPC, RES, NFAT, ROOTN, 0, 0xF8, fatsz, SPT, HEADS, SPT, psec)
    struct.pack_into('<BBBI11s8s', bs, 36, 0x80, 0, 0x29, 0x1234ABCD, b'SC55TEST   ', b'FAT16   ')
    code = carray('br_fat16_0x3e.h' if ms else 'br_fat16fd_0x3e.h')
    bs[0x3E:0x3E + len(code)] = code
    bs[510:512] = b'\x55\xAA'
    # MBR
    mbr = bytearray(SEC)
    mcode = carray('mbr_dos.h')
    mbr[0:min(len(mcode), 446)] = mcode[:446]
    def chs(lba):
        c = lba // (HEADS * SPT); h = (lba // SPT) % HEADS; s = lba % SPT + 1
        return bytes([h, ((c >> 2) & 0xC0) | s, c & 0xFF])
    pe = bytes([0x80]) + chs(SPT) + bytes([0x06]) + chs(total - 1) + struct.pack('<II', SPT, psec)
    mbr[446:462] = pe; mbr[510:512] = b'\x55\xAA'
    fatb = struct.pack('<%dH' % len(fat), *fat)
    fatb += bytes(fatsz * SEC - len(fatb))
    with open(image, 'wb') as f:
        f.write(mbr); f.write(bytes((SPT - 1) * SEC))
        f.write(bs); f.write(bytes((RES - 1) * SEC))
        for _ in range(NFAT): f.write(fatb)
        f.write(root + bytes(rootsec * SEC - len(root)))
        f.write(data)
        f.write(bytes(total * SEC - f.tell()))
    print('%s: %d MB, %d clusters, %d used' % (image, mb, nclus, nxt[0] - 2))

class Img:
    def __init__(s, image):
        s.d = open(image, 'rb').read()
        b = SPT * SEC
        s.bps, s.spc, s.res, s.nf, s.rootn = struct.unpack_from('<HBHBH', s.d, b + 11)
        s.fatsz = struct.unpack_from('<H', s.d, b + 22)[0]
        s.fat = b + s.res * SEC
        s.root = s.fat + s.nf * s.fatsz * SEC
        s.data = s.root + s.rootn * 32
    def chain(s, c):
        out = []
        while 2 <= c < 0xFFF8:
            out.append(c); c = struct.unpack_from('<H', s.d, s.fat + c * 2)[0]
        return out
    def clusdata(s, c):
        cs = s.spc * SEC
        return b''.join(s.d[s.data + (x - 2) * cs: s.data + (x - 1) * cs] for x in s.chain(c))
    def entries(s, blob):
        for i in range(0, len(blob), 32):
            e = blob[i:i + 32]
            if e[0] == 0: break
            if e[0] == 0xE5 or e[11] == 0x0F: continue
            name = e[:8].decode('latin-1').strip() + ('.' + e[8:11].decode('latin-1').strip() if e[8:11].strip() else '')
            yield name, e[11], struct.unpack_from('<H', e, 26)[0], struct.unpack_from('<I', e, 28)[0]
    def find(s, path):
        blob = s.d[s.root:s.data]
        parts = [p for p in path.upper().replace('/', '\\').split('\\') if p]
        ent = None
        for i, p in enumerate(parts):
            ent = next((e for e in s.entries(blob) if e[0] == p), None)
            if not ent: return None
            if ent[1] & 0x10: blob = s.clusdata(ent[2])
        return ent, blob
    def ls(s, path=''):
        if not path: blob = s.d[s.root:s.data]
        else: blob = s.find(path)[1]
        for e in s.entries(blob): print('%-12s %s %8d' % (e[0], 'DIR' if e[1] & 0x10 else '   ', e[3]))
    def get(s, path):
        r = s.find(path)
        if not r: return None
        ent = r[0]
        return s.clusdata(ent[2])[:ent[3]]

if __name__ == '__main__':
    if sys.argv[1] == 'build': build(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 120)
    elif sys.argv[1] == 'ls': Img(sys.argv[2]).ls(sys.argv[3] if len(sys.argv) > 3 else '')
    elif sys.argv[1] == 'get':
        d = Img(sys.argv[2]).get(sys.argv[3])
        if d is None: sys.exit('not found')
        open(sys.argv[4], 'wb').write(d)

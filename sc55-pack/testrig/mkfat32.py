#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""mkfat32.py - bootable MS-DOS 7.1 / Windows 98 FAT32 hard-disk image from a host folder (long names kept).
   mkfat32.py build <folder> <image> [MB]   folder becomes C:\\ (IO.SYS + MSDOS.SYS of MS-DOS 7.1 in it)
   mkfat32.py ls <image> [path]             list a folder (long names)
   mkfat32.py get <image> <path> <out>      copy a file out (long or short path)
Used for the Windows 98 SE test machine: the user's QuickInstall FULL.866 extracted (testrig/padkeys/mercy.py)."""
import os, re, struct, sys

INC = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rufus/src/ms-sys/inc')
def carray(fn):
    s = open(os.path.join(INC, fn)).read()
    s = s[s.index('{') + 1:s.rindex('}')]
    return bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]{2}', s))

SPT, HEADS, SEC, RES = 63, 255, 512, 32
OK83 = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^_`{}~")

def split83(n):
    if n in ('.', '..'): return None
    if '.' in n[1:]:
        b, e = n.rsplit('.', 1)
    else:
        b, e = n, ''
    return b, e

def is83(n):
    s = split83(n)
    if not s: return True
    b, e = s
    return (0 < len(b) <= 8 and len(e) <= 3 and n == n.upper() and
            all(c in OK83 for c in b) and all(c in OK83 for c in e))

def shortname(n, used):
    if is83(n):
        b, e = split83(n)
        return (b.ljust(8) + e.ljust(3)).encode('latin-1')
    b, e = split83(n)
    clean = lambda s: ''.join(c if c in OK83 else '_' for c in s.upper().replace(' ', '').replace('.', ''))
    b, e = clean(b) or '_', clean(e)[:3]
    for i in range(1, 99999):
        t = '~%d' % i
        cand = (b[:8 - len(t)] + t).ljust(8) + e.ljust(3)
        if cand not in used:
            return cand.encode('latin-1')
    raise RuntimeError('no short name for ' + n)

def lfn_entries(name, sn):
    csum = 0
    for c in sn: csum = (((csum & 1) << 7) + (csum >> 1) + c) & 0xFF
    u = name.encode('utf-16-le') + b'\0\0'
    while len(u) % 26: u += b'\xff\xff'
    parts = [u[i:i + 26] for i in range(0, len(u), 26)]
    out = []
    for i, p in enumerate(parts):
        seq = i + 1 | (0x40 if i == len(parts) - 1 else 0)
        out.append(bytes([seq]) + p[0:10] + bytes([0x0F, 0, csum]) + p[10:22] + b'\0\0' + p[22:26])
    return b''.join(reversed(out))

def build(folder, image, mb=1024):
    total = mb * 1024 * 1024 // SEC
    total -= total % (SPT * HEADS)
    psec = total - SPT
    spc = 8 if mb <= 8192 else 16
    csize = spc * SEC
    nclus = psec // spc
    fatsz = (nclus + 2) * 4 // SEC + 1
    data0 = RES + 2 * fatsz
    nclus = (psec - data0) // spc
    assert nclus >= 65525, nclus
    fat = bytearray((nclus + 2) * 4)
    struct.pack_into('<II', fat, 0, 0x0FFFFFF8, 0x0FFFFFFF)
    f = open(image, 'wb+')
    f.truncate(total * SEC)
    nxt = [2]

    def alloc(nbytes):
        n = max(1, (nbytes + csize - 1) // csize)
        first = nxt[0]
        for i in range(n):
            struct.pack_into('<I', fat, (first + i) * 4, first + i + 1 if i < n - 1 else 0x0FFFFFFF)
        nxt[0] += n
        assert nxt[0] <= nclus + 2, 'disk full'
        return first

    def write(clus, payload):
        f.seek((SPT + data0 + (clus - 2) * spc) * SEC)
        f.write(payload)

    def ent(sn, attr, clus, size):
        return sn + bytes([attr, 0, 0]) + struct.pack('<HHHHHHHI', 0x5A21, 0x5A21, 0x5A21, clus >> 16,
                                                         0x6000, 0x5A21, clus & 0xFFFF, size)

    def dodir(path, my_clus, parent_clus):
        names = sorted(os.listdir(path), key=lambda n: (n.upper() not in ('IO.SYS', 'MSDOS.SYS'), n.upper()))
        used, ents, subs = set(), [], []
        if my_clus != 2:
            ents.append(ent(b'.          ', 0x10, my_clus, 0))
            ents.append(ent(b'..         ', 0x10, 0 if parent_clus == 2 else parent_clus, 0))
        for n in names:
            p = os.path.join(path, n)
            sn = shortname(n, used)
            used.add(sn.decode('latin-1'))
            lfn = b'' if is83(n) else lfn_entries(n, sn)
            if os.path.isdir(p):
                subs.append((p, sn, lfn))
                continue
            d = open(p, 'rb').read()
            attr = 0x27 if n.upper() in ('IO.SYS', 'MSDOS.SYS') else 0x20
            c = alloc(len(d)) if d else 0
            if d: write(c, d)
            ents.append(lfn + ent(sn, attr, c, len(d)))
        # subfolders: entries first (size known after), then recurse
        sub_slots = []
        for p, sn, lfn in subs:
            sub_slots.append(len(ents))
            ents.append(None)
        # this directory's own clusters must be allocated before children use more
        nent = sum(len(e) // 32 if e else 1 + 0 for e in ents) + sum(len(l) // 32 for _, _, l in subs) + 1
        if my_clus == 2:
            mine = 2
            need = (nent * 32 + csize - 1) // csize
            for i in range(need):
                struct.pack_into('<I', fat, (2 + i) * 4, 3 + i if i < need - 1 else 0x0FFFFFFF)
            nxt[0] = max(nxt[0], 2 + need)
        else:
            mine = my_clus
        kids = []
        for (p, sn, lfn), slot in zip(subs, sub_slots):
            # size of the child directory decides its clusters
            cnt = 2 + sum(1 + (0 if is83(x) else (len(x) + 13) // 13) for x in os.listdir(p)) + 1
            c = alloc(cnt * 32)
            ents[slot] = lfn + ent(sn, 0x10, c, 0)
            kids.append((p, c))
        blob = b''.join(ents) + bytes(32)
        if my_clus == 2:
            write(2, blob)
        else:
            write(my_clus, blob)
        for p, c in kids:
            dodir(p, c, mine)

    # root = cluster 2 reserved first
    struct.pack_into('<I', fat, 8, 0x0FFFFFFF)
    nxt[0] = 3
    # root may need more than one cluster: count and reserve consecutively
    rcnt = 1 + sum(1 + (0 if is83(x) else (len(x) + 13) // 13) for x in os.listdir(folder))
    rneed = (rcnt * 32 + csize - 1) // csize
    for i in range(rneed):
        struct.pack_into('<I', fat, (2 + i) * 4, 3 + i if i < rneed - 1 else 0x0FFFFFFF)
    nxt[0] = 2 + rneed
    dodir(folder, 2, 2)

    bs = bytearray(SEC)
    bs[0:3] = b'\xEB\x58\x90'
    bs[3:11] = b'MSWIN4.1'
    struct.pack_into('<HBHBHHBHHHII', bs, 11, SEC, spc, RES, 2, 0, 0, 0xF8, 0, SPT, HEADS, SPT, psec)
    struct.pack_into('<IHHIHH12s', bs, 36, fatsz, 0, 0, 2, 1, 6, bytes(12))
    struct.pack_into('<BBBI11s8s', bs, 64, 0x80, 0, 0x29, 0x1998ABCD, b'WIN98TEST  ', b'FAT32   ')
    code = carray('br_fat32_0x52.h')
    bs[0x52:0x52 + len(code)] = code
    bs[510:512] = b'\x55\xAA'
    three = bytearray(3 * SEC)
    three[0:SEC] = bs
    fsi = bytearray(SEC)
    struct.pack_into('<I', fsi, 0, 0x41615252)
    struct.pack_into('<III', fsi, 484, 0x61417272, nclus + 2 - nxt[0], nxt[0])
    fsi[510:512] = b'\x55\xAA'
    three[SEC:2 * SEC] = fsi
    c3 = carray('br_fat32_0x3f0.h')
    three[0x3F0:0x3F0 + len(c3)] = c3
    three[0x5FE:0x600] = b'\x55\xAA'
    c0 = carray('br_fat32_0x0.h')
    three[0:len(c0)] = c0
    f.seek(SPT * SEC); f.write(three)
    f.seek((SPT + 6) * SEC); f.write(three)
    for i in range(2):
        f.seek((SPT + RES + i * fatsz) * SEC); f.write(fat)
    mbr = bytearray(SEC)
    mcode = carray('mbr_dos.h')
    mbr[0:min(len(mcode), 446)] = mcode[:446]
    def chs(lba):
        c = min(lba // (HEADS * SPT), 1023); h = (lba // SPT) % HEADS; s = lba % SPT + 1
        return bytes([h, ((c >> 2) & 0xC0) | s, c & 0xFF])
    mbr[446:462] = bytes([0x80]) + chs(SPT) + bytes([0x0C]) + chs(total - 1) + struct.pack('<II', SPT, psec)
    mbr[510:512] = b'\x55\xAA'
    f.seek(0); f.write(mbr)
    f.close()
    print('%s: %d MB FAT32, %d clusters of %d KB, %d used' % (image, mb, nclus, csize // 1024, nxt[0] - 2))

class Img:
    def __init__(s, image):
        s.f = open(image, 'rb')
        b = s.rd(SPT * SEC, SEC)
        s.spc = b[13]; s.res = struct.unpack_from('<H', b, 14)[0]
        s.fatsz = struct.unpack_from('<I', b, 36)[0]; s.rootc = struct.unpack_from('<I', b, 44)[0]
        s.fat = SPT * SEC + s.res * SEC
        s.data = s.fat + 2 * s.fatsz * SEC
    def rd(s, o, n):
        s.f.seek(o); return s.f.read(n)
    def chain(s, c):
        out = []
        while 2 <= c < 0x0FFFFFF8:
            out.append(c); c = struct.unpack('<I', s.rd(s.fat + c * 4, 4))[0] & 0x0FFFFFFF
        return out
    def clusdata(s, c):
        cs = s.spc * SEC
        return b''.join(s.rd(s.data + (x - 2) * cs, cs) for x in s.chain(c))
    def entries(s, blob):
        lfn = ''
        for i in range(0, len(blob), 32):
            e = blob[i:i + 32]
            if e[0] == 0: break
            if e[0] == 0xE5: lfn = ''; continue
            if e[11] == 0x0F:
                part = (e[1:11] + e[14:26] + e[28:32]).decode('utf-16-le', 'replace').split('\0')[0]
                lfn = part + lfn if not (e[0] & 0x40) else part
                continue
            sn = e[:8].decode('latin-1').strip() + ('.' + e[8:11].decode('latin-1').strip() if e[8:11].strip() else '')
            clus = struct.unpack_from('<H', e, 26)[0] | struct.unpack_from('<H', e, 20)[0] << 16
            yield (lfn or sn), sn, e[11], clus, struct.unpack_from('<I', e, 28)[0]
            lfn = ''
    def find(s, path):
        blob = s.clusdata(s.rootc)
        parts = [p for p in path.replace('/', '\\').split('\\') if p]
        ent = None
        for p in parts:
            ent = next((e for e in s.entries(blob) if p.upper() in (e[0].upper(), e[1].upper())), None)
            if not ent: return None
            if ent[2] & 0x10: blob = s.clusdata(ent[3])
        return ent, blob
    def ls(s, path=''):
        blob = s.clusdata(s.rootc) if not path else s.find(path)[1]
        for e in s.entries(blob): print('%-12s %s %9d  %s' % (e[1], 'DIR' if e[2] & 0x10 else '   ', e[4], e[0]))
    def get(s, path):
        r = s.find(path)
        if not r: return None
        return s.clusdata(r[0][3])[:r[0][4]]

if __name__ == '__main__':
    if sys.argv[1] == 'build': build(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 1024)
    elif sys.argv[1] == 'ls': Img(sys.argv[2]).ls(sys.argv[3] if len(sys.argv) > 3 else '')
    elif sys.argv[1] == 'get':
        d = Img(sys.argv[2]).get(sys.argv[3])
        if d is None: sys.exit('not found')
        open(sys.argv[4], 'wb').write(d)

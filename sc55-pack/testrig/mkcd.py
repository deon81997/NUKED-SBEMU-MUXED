#!/usr/bin/env python3
"""mkcd.py - test CD images from a folder (ISO 9660, 8.3 names, any depth).

  mkcd.py iso  SRCDIR OUT.ISO [LABEL]                       2048-byte sectors
  mkcd.py cue  SRCDIR OUT.CUE [LABEL] [--audio N] [--raw]   one BIN: data track (MODE1/2352) + N audio tracks
                                                            (tone tracks: L+R, L only, R only, ...)
  --pregap   gives track 2 a 2-second PREGAP and an INDEX 00 (like many real rips)
Names are upper-cased; files must already have 8.3 names. Also writes OUT.LST: path, size, CRC32 per file
(the DOS checker CDCHK2 compares against it).
"""
import os, sys, struct, zlib, math

S = 2048
DATE7 = bytes([96, 12, 11, 12, 0, 0, 0])
DATE17 = b'1996121112000000' + b'\x00'
def both16(v): return struct.pack('<H', v) + struct.pack('>H', v)
def both32(v): return struct.pack('<I', v) + struct.pack('>I', v)

def rec(name, lba, size, isdir):
    r = bytes([0, 0]) + both32(lba) + both32(size) + DATE7 + bytes([2 if isdir else 0, 0, 0]) + both16(1) + bytes([len(name)]) + name
    if len(r) % 2: r += b'\x00'
    return bytes([len(r)]) + r[1:]

class Dir:
    def __init__(s, path, name, parent):
        s.path, s.name, s.parent = path, name, parent
        s.dirs, s.files = [], []
        s.lba = 0; s.size = 0; s.num = 0

def scan(path, name=b'\x00', parent=None):
    d = Dir(path, name, parent)
    for e in sorted(os.listdir(path), key=str.upper):
        p = os.path.join(path, e)
        if os.path.isdir(p): d.dirs.append(scan(p, e.upper().encode(), d))
        else: d.files.append([e.upper().encode(), p, os.path.getsize(p), 0])
    return d

def walk(d):
    yield d
    for c in d.dirs: yield from walk(c)

def dir_records(d):
    out = []
    out.append(rec(b'\x00', d.lba, d.size, 1))
    par = d.parent or d
    out.append(rec(b'\x01', par.lba, par.size, 1))
    ents = [(c.name, c.lba, c.size, 1) for c in d.dirs] + [(f[0] + b';1', f[3], f[2], 0) for f in d.files]
    ents.sort(key=lambda e: e[0])
    for e in ents: out.append(rec(*e))
    return out

def layout_dir_size(d):
    # records may not cross a sector boundary
    n = len(dir_records(d)); used = 0; secs = 1
    for r in dir_records(d):
        if used + len(r) > S: secs += 1; used = 0
        used += len(r)
    return secs * S

def build(src, label):
    root = scan(src)
    dirs = list(walk(root))
    # breadth-first numbering for the path table
    bfs = [root]; i = 0
    while i < len(bfs): bfs.extend(bfs[i].dirs); i += 1
    for n, d in enumerate(bfs): d.num = n + 1
    def ptable(big):
        out = b''
        for d in bfs:
            name = d.name if d is not root else b'\x00'
            pn = (d.parent or root).num
            out += bytes([len(name), 0]) + struct.pack('>I' if big else '<I', d.lba) + struct.pack('>H' if big else '<H', pn) + name
            if len(name) % 2: out += b'\x00'
        return out
    lba = 18
    pt_len = len(ptable(False)); pt_secs = (pt_len + S - 1) // S
    LPT = lba; lba += pt_secs; MPT = lba; lba += pt_secs
    for d in bfs: d.size = layout_dir_size(d)
    for d in bfs: d.lba = lba; lba += d.size // S
    for d in bfs:
        for f in d.files: f[3] = lba; lba += max(1, (f[2] + S - 1) // S)
    total = lba
    img = bytearray(total * S)
    pvd = bytearray(S)
    pvd[0] = 1; pvd[1:6] = b'CD001'; pvd[6] = 1
    pvd[8:40] = b' ' * 32; pvd[40:72] = label.encode().ljust(32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1); pvd[124:128] = both16(1); pvd[128:132] = both16(S)
    pvd[132:140] = both32(pt_len)
    pvd[140:144] = struct.pack('<I', LPT); pvd[148:152] = struct.pack('>I', MPT)
    pvd[156:190] = rec(b'\x00', root.lba, root.size, 1)
    for a, b in ((190, 318), (318, 446), (446, 574), (574, 702)): pvd[a:b] = b' ' * (b - a)
    pvd[702:813] = b' ' * 111
    for i in range(4): pvd[813 + 17 * i:830 + 17 * i] = DATE17 if i < 2 else b'0' * 16 + b'\x00'
    pvd[881] = 1
    img[16 * S:17 * S] = pvd
    term = bytearray(S); term[0] = 255; term[1:6] = b'CD001'; term[6] = 1
    img[17 * S:18 * S] = term
    img[LPT * S:LPT * S + pt_len] = ptable(False)
    img[MPT * S:MPT * S + pt_len] = ptable(True)
    lst = []
    for d in bfs:
        o = d.lba * S; used = 0
        for r in dir_records(d):
            if used + len(r) > S: o += S - used; used = 0
            img[o:o + len(r)] = r; o += len(r); used += len(r)
        for f in d.files:
            data = open(f[1], 'rb').read()
            img[f[3] * S:f[3] * S + len(data)] = data
            rel = os.path.relpath(f[1], src).upper().replace('/', '\\')
            lst.append('%s %d %08X' % (rel, len(data), zlib.crc32(data) & 0xFFFFFFFF))
    return bytes(img), lst

# ---- raw (2352) sectors -----------------------------------------------------------------------
def edc(b):
    t = []
    for i in range(256):
        e = i
        for _ in range(8): e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
        t.append(e)
    c = 0
    for x in b: c = (c >> 8) ^ t[(c ^ x) & 0xFF]
    return c
EDC_T = None
def bcd(v): return ((v // 10) << 4) | (v % 10)
def raw_sector(lba, data):
    a = lba + 150; m, s, f = a // 4500, (a // 75) % 60, a % 75
    hdr = b'\x00' + b'\xff' * 10 + b'\x00' + bytes([bcd(m), bcd(s), bcd(f), 1])
    body = hdr + data
    return body + struct.pack('<I', edc(body)) + bytes(8) + bytes(276)   # ECC left zero (drivers do not check)

def tone(secs, mode):
    n = int(44100 * secs); out = bytearray()
    for i in range(n):
        v = int(12000 * math.sin(2 * math.pi * 440 * i / 44100))
        l, r = (v, v) if mode == 0 else ((v, 0) if mode == 1 else (0, v))
        out += struct.pack('<hh', l, r)
    out += bytes((-len(out)) % 2352)
    return bytes(out)

SECS = 4
def main():
    global SECS
    a = sys.argv[1:]
    kind, src, out = a[0], a[1], a[2]
    label = a[3] if len(a) > 3 and not a[3].startswith('--') else 'NUKEDTST'
    img, lst = build(src, label)
    base = os.path.splitext(out)[0]
    open(base + '.LST', 'w', newline='\r\n').write('\n'.join(lst) + '\n')
    if kind == 'iso':
        open(out, 'wb').write(img); print(out, len(img), len(lst), 'files'); return
    naudio = int(a[a.index('--audio') + 1]) if '--audio' in a else 0
    pregap = '--pregap' in a
    if '--secs' in a: SECS = int(a[a.index('--secs') + 1])
    binname = os.path.basename(base) + '.BIN'
    with open(base + '.BIN', 'wb') as f:
        for i in range(len(img) // S):
            f.write(raw_sector(i, img[i * S:(i + 1) * S]))
        pos = len(img) // S
        cue = ['FILE "%s" BINARY' % binname, '  TRACK 01 MODE1/2352', '    INDEX 01 00:00:00']
        for t in range(naudio):
            if t == 0 and pregap:
                f.write(bytes(2352 * 150))           # pregap silence stored in the file (INDEX 00)
                m, s_, fr = pos // 4500, (pos // 75) % 60, pos % 75
                cue += ['  TRACK %02d AUDIO' % (t + 2), '    INDEX 00 %02d:%02d:%02d' % (m, s_, fr)]
                pos += 150
            else:
                cue += ['  TRACK %02d AUDIO' % (t + 2)]
            m, s_, fr = pos // 4500, (pos // 75) % 60, pos % 75
            cue += ['    INDEX 01 %02d:%02d:%02d' % (m, s_, fr)]
            d = tone(SECS, t % 3); f.write(d); pos += len(d) // 2352
    open(out, 'w', newline='\r\n').write('\n'.join(cue) + '\n')
    print(out, base + '.BIN', len(lst), 'files', naudio, 'audio tracks')

main()

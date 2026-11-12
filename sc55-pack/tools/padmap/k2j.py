#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""k2j.py keyb2joypad.cpp OUT.json : decode DOSBox Pure's Keyb2Joypad tables (data only, nothing executed).
Output: list of {title, year, hashes:[...], binds:[{btn, keys:[...], name}]}"""
import sys, re, zlib, json

src = open(sys.argv[1], encoding='latin-1').read()

def c_bytes(lit):
    out = bytearray(); i = 0
    esc = {'n': 10, 't': 9, 'r': 13, 'a': 7, 'b': 8, 'f': 12, 'v': 11, '\\': 92, '"': 34, "'": 39, '?': 63}
    while i < len(lit):
        c = lit[i]
        if c != '\\': out.append(ord(c)); i += 1; continue
        n = lit[i + 1]
        if n in '01234567':
            j = i + 1; v = ''
            while j < len(lit) and len(v) < 3 and lit[j] in '01234567': v += lit[j]; j += 1
            out.append(int(v, 8)); i = j
        elif n == 'x':
            j = i + 2; v = ''
            while j < len(lit) and lit[j] in '0123456789abcdefABCDEF': v += lit[j]; j += 1
            out.append(int(v, 16) & 255); i = j
        else: out.append(esc[n]); i += 2
    return bytes(out)

TABLE = int(re.search(r'MAP_TABLE_SIZE = (\d+)', open(sys.argv[1].replace('.cpp', '.h')).read()).group(1))
NB = 4
keys = [int(x) for x in re.search(r'map_keys\[MAP_TABLE_SIZE\] = \{(.*?)\};', src, re.S).group(1).replace('\n', '').split(',') if x.strip()]
assert len(keys) == TABLE, len(keys)
body = re.search(r'map_buckets\[MAP_BUCKETS\] =\s*\{(.*)\};', src, re.S).group(1)
buckets = []
for blk in re.findall(r'\{(.*?)\n\t\},', body, re.S):
    parts = []   # alternating string groups and numbers in order
    for m in re.finditer(r'((?:"(?:[^"\\]|\\.)*"\s*)+)|(?<![A-Za-z_\d])(\d+)', blk):
        if m.group(1): parts.append(b''.join(c_bytes(s) for s in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))))
        else: parts.append(int(m.group(2)))
    idc, ics, icu, mpc, mcs, mcu, act = parts
    assert len(idc) == ics and len(mpc) == mcs, (len(idc), ics, len(mpc), mcs)
    ids = zlib.decompress(idc, -15); mps = zlib.decompress(mpc, -15)
    assert len(ids) == icu and len(mps) == mcu
    buckets.append((ids, mps, act))

BTN = ['B', 'Y', 'Select', 'Start', 'Up', 'Down', 'Left', 'Right', 'A', 'X', 'L', 'R', 'L2', 'R2', 'L3', 'R3',
       'LStickX', 'LStickY', 'RStickX', 'RStickY']
KBD = ['None'] + list('1234567890') + list('qwertyuiop') + list('asdfghjklz') + list('xcvbnm') + \
      ['F%d' % i for i in range(1, 13)] + ['Esc', 'Tab', 'Backspace', 'Enter', 'Space', 'LAlt', 'RAlt', 'LCtrl', 'RCtrl',
      'LShift', 'RShift', 'CapsLock', 'ScrollLock', 'NumLock', '`', '-', '=', '\\', '[', ']', ';', "'", '.', ',', '/',
      '<>', 'PrintScreen', 'Pause', 'Insert', 'Home', 'PageUp', 'Delete', 'End', 'PageDown', 'Left', 'Up', 'Down',
      'Right'] + ['KP%d' % i for i in (1, 2, 3, 4, 5, 6, 7, 8, 9, 0)] + ['KP/', 'KP*', 'KP-', 'KP+', 'KPEnter', 'KP.']
SPEC = ['MouseUp', 'MouseDown', 'MouseLeft', 'MouseRight', 'MouseClickL', 'MouseClickR', 'MouseClickM', 'MouseFaster',
        'MouseSlower', 'Joy1Up', 'Joy1Down', 'Joy1Left', 'Joy1Right', 'JoyButton1', 'JoyButton2', 'JoyButton3',
        'JoyButton4', 'HatUp', 'HatDown', 'HatLeft', 'HatRight', 'Joy2Up', 'Joy2Down', 'Joy2Left', 'Joy2Right', 'OSK']
def kname(k):
    if k < len(KBD): return KBD[k]
    if 200 <= k < 200 + len(SPEC): return SPEC[k - 200]
    return '?%d' % k

def cstr(b, o):
    e = b.index(b'\0', o); return b[o:e].decode('latin-1')

games = {}
for idx, h in enumerate(keys):
    if not h: continue
    ids, _, _ = buckets[idx % NB]
    ident = ids[(idx // NB) * 5:(idx // NB) * 5 + 5]
    mb = ident[0] % NB; moff = (ident[1] << 8) | ident[2]
    toff = (TABLE // NB) * 5 + ((ident[3] << 8) | ident[4])
    raw = ids[toff:ids.index(b'\0', toff)]
    year, title = 1970 + raw[0], raw[1:].decode('latin-1')
    key = (title, year, mb, moff)
    g = games.get(key)
    if not g:
        _, mps, act = buckets[mb]
        p = moff; cnt = mps[p]; p += 1; binds = []
        for _ in range(cnt):
            v = mps[p]; p += 1
            bid, analog, named, kc = v & 31, (v & 31) >= 16, bool(v & 32), 1 + (v >> 6)
            name = None
            if named:
                no = 0
                while True:
                    c = mps[p]; p += 1; no = (no << 7) | (c & 127)
                    if not c & 128: break
                name = cstr(mps, act + no)
            ks = []
            for _ in range(kc):
                if analog: ks.append([kname(mps[p]), kname(mps[p + 1])]); p += 2
                else: ks.append(kname(mps[p])); p += 1
            binds.append({'btn': BTN[bid], 'keys': ks, 'name': name})
        g = games[key] = {'title': title, 'year': year, 'hashes': [], 'binds': binds}
    g['hashes'].append(h)
out = sorted(games.values(), key=lambda g: g['title'].lower())
json.dump(out, open(sys.argv[2], 'w'), indent=1)
print(len(out), 'games,', sum(len(g['hashes']) for g in out), 'file ids')

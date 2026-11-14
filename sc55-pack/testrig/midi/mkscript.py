#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""mkscript.py OUTDIR - MPUPLAY scripts for the USB MIDI byte-exact check (beta.121), standing in for the drivers
   the rig has no free game for (each written like that driver writes; seeded, so the same every run):
   MILES.TXT   Miles AIL-style UART: reset/UART handshake, GM reset, per-channel init with running status, a song
               with running status, note-on velocity 0, pitch bend, F8 clocks anywhere (also inside messages), FE
   GSSYSEX.TXT SysEx-heavy GS game start: GS reset, 300 DT1 part settings written in pieces with pauses between the
               pieces and F8 inside, then an 8 KB SysEx burst without pauses (the cable cannot keep up: DRR), notes
   MI2.TXT     iMUSE-like MT-32 / GS set-up: 20 x 256-byte DT1 SysEx, a SysEx cut by a status byte, a SysEx followed
               by F0 without F7, a stray F7, system common F1/F2/F3/F6, data without status
   INTEL.TXT   intelligent mode (Sierra-style): reset only, version / revision requests, tempo parameter, D0 + message,
               D0 + running-status message, DF + SysEx, sequencer track data (not MIDI), then FF + 3F + UART notes"""
import os, random, sys
R = random.Random(121)
def D(b): return 'D%02X' % b
def roland(addr, data):
    s = sum(addr) + sum(data)
    return [0xF0, 0x41, 0x10, 0x42, 0x12] + addr + data + [(128 - s % 128) % 128, 0xF7]
def hand(t):
    t += ['C3F', 'A'] if False else []
    return t
def head(): return ['CFF', 'A', 'C3F', 'A']
def song(n, clock=True, rt_inside=True):
    t, rs = [], None
    for i in range(n):
        ch = R.randrange(16); k = R.random()
        if k < 0.5: st, d = 0x90 | ch, [R.randrange(30, 90), R.choice([0, R.randrange(1, 128)])]
        elif k < 0.65: st, d = 0x80 | ch, [R.randrange(30, 90), 64]
        elif k < 0.8: st, d = 0xB0 | ch, [R.choice([1, 7, 10, 11, 64, 91, 93]), R.randrange(128)]
        elif k < 0.88: st, d = 0xE0 | ch, [R.randrange(128), R.randrange(128)]
        elif k < 0.94: st, d = 0xC0 | ch, [R.randrange(128)]
        else: st, d = 0xD0 | ch, [R.randrange(128)]
        b = ([st] if st != rs or R.random() < 0.2 else []) + d; rs = st
        if clock and rt_inside and R.random() < 0.1: b.insert(R.randrange(1, len(b) + 1), R.choice([0xF8, 0xF8, 0xFE]))
        t += [D(x) for x in b]
        if clock and R.random() < 0.15: t.append(D(0xF8))
        if R.random() < 0.3: t.append('W%d' % R.choice([1, 2, 5, 10]))
    return t
def miles():
    t = head() + [D(x) for x in [0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7]] + ['W50']
    for ch in range(16):
        t += [D(x) for x in [0xB0 | ch, 121, 0, 7, 100, 10, 64, 91, 40, 93, 0, 0xC0 | ch, R.randrange(128), 0xE0 | ch, 0, 64]]
    return t + song(2500)
def gssysex():
    t = head() + [D(x) for x in roland([0x40, 0x00, 0x7F], [0x00])] + ['W60']
    for i in range(300):
        part = i % 16; m = roland([0x40, 0x10 | [1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 10, 11, 12, 13, 14, 15][part], R.randrange(0x00, 0x30)],
                                  [R.randrange(128) for _ in range(R.randrange(1, 24))])
        cut = sorted(R.sample(range(1, len(m)), min(3, len(m) - 1)))
        prev = 0
        for c in cut + [len(m)]:
            seg = [D(x) for x in m[prev:c]]
            if R.random() < 0.3: seg.insert(R.randrange(len(seg) + 1), D(0xF8))
            t += seg + ['W%d' % R.choice([0, 1, 3])]; prev = c
    burst = []
    while len(burst) < 8000: burst += roland([0x40, 0x01, 0x30], [R.randrange(128) for _ in range(30)])
    t += [D(x) for x in burst]
    return t + song(600)
def mi2():
    t = head()
    for i in range(20): t += [D(x) for x in roland([0x05 if i < 8 else 0x08, i * 2, 0x00], [R.randrange(128) for _ in range(256)])] + ['W5']
    t += [D(x) for x in [0xF0, 0x41, 0x10, 0x16, 0x12, 0x20, 0x00, 0x90, 0x3C, 0x40]]          # cut by a note on
    t += [D(x) for x in [0xF0, 0x41, 0x10, 0x16, 0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x00, 0x01, 0x6F, 0xF7]]   # F0 inside
    t += [D(x) for x in [0xF7, 0x91, 0x40, 0x40, 0xF1, 0x23, 0xF2, 0x10, 0x20, 0xF3, 0x05, 0xF6, 0x45, 0x92, 0x30, 0x50, 0x31, 0x50]]
    return t + song(1500)
def intel():
    t = ['CFF', 'A', 'CAC', 'A', 'A', 'CAD', 'A', 'A', 'CE0', 'A', D(100), 'C8A', 'A', 'C94', 'A']
    for i in range(400):
        ch = R.randrange(9); rs = R.random() < 0.4 and i
        t += ['CD0', 'A'] + ([D(0x90 | 0)] if not rs else []) + [D(R.randrange(40, 80)), D(R.choice([0, 100]))]
        if R.random() < 0.1: t += ['CD0', 'A', D(0xC0 | ch), D(R.randrange(128))]
        if R.random() < 0.05: t += ['CDF', 'A'] + [D(x) for x in roland([0x40, 0x01, 0x30], [R.randrange(128) for _ in range(6)])]
        if R.random() < 0.05: t += [D(0xF8), D(0x10), D(0x90), D(0x3C)]          # sequencer track data: not MIDI
        if R.random() < 0.1: t += ['CE7', 'A', D(R.randrange(1, 255))]           # clock-to-host rate parameter
        if R.random() < 0.3: t.append('W2')
    return t + ['CFF', 'A', 'C3F', 'A'] + song(300)
os.makedirs(sys.argv[1], exist_ok=True)
for name, f in (('MILES', miles), ('GSSYSEX', gssysex), ('MI2', mi2), ('INTEL', intel)):
    open(os.path.join(sys.argv[1], name + '.TXT'), 'w').write(' '.join(f()) + '\r\n')

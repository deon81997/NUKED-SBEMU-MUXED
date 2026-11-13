#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""fmtcheck.py MPHOST OUTDIR : MIDPLAY's song formats and player features against the host build (-DHOSTTEST).
Writes one tune as .MID, .KAR (title from @T), .RMI (LIST chunk before data), .MUS, .HMP (both versions) and .XMI, plays each with
MPHOST and checks that every one sends the same notes at the same times. Then: transpose (also changed
while notes sound: no stuck notes), A-B loop, sleep timer. Exit code 0 = all passed."""
import os, struct, subprocess, sys
mph, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
fails = []
def check(c, what):
    if not c: fails.append(what); print('FAIL', what)

# the tune, in 50 ms steps: (start, channel 0 or 9, note, velocity, length)
TUNE = []
for i in range(48):
    TUNE.append((i * 4, 0, 48 + (i * 5) % 24, 70 + i % 50, 3))
    if i % 2 == 0: TUNE.append((i * 4, 9, 36 + (i // 2) % 2 * 2, 100, 1))
    if i % 3 == 0: TUNE.append((i * 4 + 2, 0, 72 + i % 7, 90, 2))
LYR = [(0, '@KMIDI KARAOKE FILE'), (0, '@TTest Song'), (8, '\\Hel'), (12, 'lo '), (16, 'world'), (40, '/Sing '), (48, 'a '), (56, 'long'),
       (80, '/Last '), (88, 'line')]

def vl(v):
    b = [v & 0x7F]; v >>= 7
    while v: b.insert(0, 0x80 | (v & 0x7F)); v >>= 7
    return bytes(b)
def vl_hmp(v):
    b = []
    while True:
        b.append(v & 0x7F); v >>= 7
        if not v: break
    b[-1] |= 0x80; return bytes(b)
def evlist(tpu):        # (tick, order, bytes) with note-offs before note-ons at the same tick
    ev = []
    for t, ch, n, v, d in TUNE:
        ev.append((t * tpu, 1, bytes([0x90 | ch, n, v]))); ev.append(((t + d) * tpu, 0, bytes([0x80 | ch, n, 64])))
    ev.append((0, -1, bytes([0xC0, 19]))); ev.append((0, -1, bytes([0xB0, 7, 110])))
    return sorted(ev, key=lambda e: (e[0], e[1]))
def trk(evs, enc=vl, eot=b'\xff\x2f\x00'):
    d = b''; last = 0
    for t, _, data in evs: d += enc(t - last) + data; last = t
    return d + enc(0) + eot
def smf(tracks, div):
    return b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks), div) + b''.join(b'MTrk' + struct.pack('>I', len(t)) + t for t in tracks)

mid = smf([trk([(0, 0, b'\xff\x51\x03\x07\xa1\x20'), (0, 0, b'\xff\x03\x05Hello')]), trk(evlist(7))], 70)
open(f'{out}/T.MID', 'wb').write(mid)
kar = smf([trk([(0, 0, b'\xff\x51\x03\x07\xa1\x20')]), trk([(0, 0, b'\xff\x03\x0cSoft Karaoke')] + [(0, 0, b'\xff\x01' + vl(len(LYR[0][1])) + LYR[0][1].encode())]),
           trk([(0, 0, b'\xff\x03\x05Words')] + [(t * 7, i, b'\xff\x01' + vl(len(s)) + s.encode()) for i, (t, s) in enumerate(LYR[1:])]), trk(evlist(7))], 70)
open(f'{out}/T.KAR', 'wb').write(kar)
info = b'INFO' + b'INAM' + struct.pack('<I', 6) + b'Hello\0'
riff = b'RMID' + b'LIST' + struct.pack('<I', len(info)) + info + b'data' + struct.pack('<I', len(mid)) + mid + (b'\0' if len(mid) & 1 else b'')
open(f'{out}/T.RMI', 'wb').write(b'RIFF' + struct.pack('<I', len(riff)) + riff)
# MUS: 140 Hz; MIDI channel 9 = MUS channel 15
body = b''; ev = [e for e in evlist(7)]; vel = {}
items = []
for t, o, data in ev:
    st, ch = data[0] & 0xF0, data[0] & 15; mc = 15 if ch == 9 else ch
    if st == 0x90:
        if vel.get(mc) == data[2]: items.append((t, bytes([0x10 | mc, data[1]])))
        else: vel[mc] = data[2]; items.append((t, bytes([0x10 | mc, 0x80 | data[1], data[2]])))
    elif st == 0x80: items.append((t, bytes([0x00 | mc, data[1]])))
    elif st == 0xC0: items.append((t, bytes([0x40 | mc, 0, data[1]])))
    elif st == 0xB0: items.append((t, bytes([0x40 | mc, 3, data[2]])))
for k, (t, b) in enumerate(items):
    nxt = items[k + 1][0] if k + 1 < len(items) else None
    if nxt is not None and nxt > t: body += bytes([b[0] | 0x80]) + b[1:] + vl(nxt - t)
    else: body += b
body += b'\x60'
open(f'{out}/T.MUS', 'wb').write(b'MUS\x1a' + struct.pack('<HHHHHH', len(body), 16, 2, 0, 0, 0) + body)
# HMP: division at 38h = ticks per second (1 s per quarter); 120 -> 6 per step
def hmp(v2):
    hdr = bytearray(0x388 if v2 else 0x308); hdr[0:8] = b'HMIMIDIP'
    if v2: hdr[8:14] = b'013195'
    tracks = [trk([(0, 0, b'\xff\x03\x80')], vl_hmp, b'\xff\x2f\x80'), trk(evlist(6), vl_hmp, b'\xff\x2f\x80')]
    struct.pack_into('<I', hdr, 0x30, len(tracks)); struct.pack_into('<I', hdr, 0x38, 120)
    data = b''.join(struct.pack('<III', i, 12 + len(t), i) + t for i, t in enumerate(tracks))
    return bytes(hdr) + data
open(f'{out}/T.HMP', 'wb').write(hmp(0)); open(f'{out}/T2.HMP', 'wb').write(hmp(1))
# XMI: 120 Hz, note-ons carry their length, delays = bytes < 80h
x = b''; last = 0
for t, o, data in evlist(6):
    if data[0] & 0xF0 == 0x80: continue
    d = t - last; last = t
    while d > 0: x += bytes([min(d, 127)]); d -= min(d, 127)
    if data[0] & 0xF0 == 0x90:
        n = [e for e in TUNE if e[1] == data[0] & 15 and e[2] == data[1] and e[0] * 6 == t][0]
        x += data + vl(n[4] * 6)
    else: x += data
x += b'\xff\x2f\x00'
def chunk(id, d): return id + struct.pack('>I', len(d)) + d + (b'\0' if len(d) & 1 else b'')
xmid = chunk(b'FORM', b'XMID' + chunk(b'TIMB', b'\x01\x00\x13\x00') + chunk(b'EVNT', x))
open(f'{out}/T.XMI', 'wb').write(chunk(b'FORM', b'XDIR' + chunk(b'INFO', b'\x01\x00')) + chunk(b'CAT ', b'XMID' + xmid))

def play(f, *a):
    r = subprocess.run([mph, f] + list(a), capture_output=True, text=True)
    check(r.returncode == 0, f'{os.path.basename(f)} {a}: exit {r.returncode} {r.stderr[-300:]}')
    msgs = []; cur = []; com = []
    for ln in r.stdout.splitlines():
        if ln.startswith('#'): com.append(ln); continue
        t, b = ln.split(); t = int(t); b = int(b, 16)
        if b & 0x80: cur = [t, b];
        else: cur.append(b)
        if cur and cur[1] & 0xF0 in (0x80, 0x90, 0xA0, 0xB0, 0xE0) and len(cur) == 4: msgs.append(tuple(cur)); cur = [t, cur[1]]
        elif cur and cur[1] & 0xF0 in (0xC0, 0xD0) and len(cur) == 3: msgs.append(tuple(cur)); cur = [t, cur[1]]
    return msgs, com
def notes(msgs):
    return [(t, s & 15, n, 'on' if s & 0xF0 == 0x90 and v else 'off') for t, s, n, v in [m for m in msgs if len(m) == 4 and m[1] & 0xF0 in (0x80, 0x90)]]
def stuck(msgs):
    held = {}
    for t, s, n, v in [m for m in msgs if len(m) == 4]:
        if s & 0xF0 == 0x90 and v: held[(s & 15, n)] = held.get((s & 15, n), 0) + 1
        elif s & 0xF0 in (0x80, 0x90): held[(s & 15, n)] = 0
        elif s & 0xF0 == 0xB0 and n in (120, 123):
            for k in held:
                if k[0] == s & 15: held[k] = 0
    return [k for k, v in held.items() if v]

ref = notes(play(f'{out}/T.MID')[0])
check(len(ref) == 2 * len(TUNE), f'MID: {len(ref)} note messages, expected {2 * len(TUNE)}')
for f in ('T.KAR', 'T.RMI', 'T.MUS', 'T.HMP', 'T2.HMP', 'T.XMI'):
    got = notes(play(f'{out}/{f}')[0])
    key = lambda e: (e[0], e[1], e[2], e[3])
    check(sorted(got, key=key) == sorted(ref, key=key), f'{f}: notes differ from T.MID ({len(got)} vs {len(ref)}; first diff {next((a, b) for a, b in zip(sorted(got, key=key) + [None] * 999, sorted(ref, key=key)) if a != b) if got != ref else None})')
    m, c = play(f'{out}/{f}', '--info'); print(f, c[0][2:])
    check('info_ok 1' in c[0], f'{f}: song_info failed: {c[0]}')
# karaoke file: its title from "@T" (not the track name "Soft Karaoke")
m, c = play(f'{out}/T.KAR', '--info')
check("title 'Test Song'" in c[0] and "info_title 'Test Song'" in c[0], f'KAR title: {c[0]}')
# transpose: +5 on melodic notes, drums untouched; changed while notes sound (no stuck notes)
up = notes(play(f'{out}/T.MID', '--pitch', '5')[0])
check([(t, c, n + (5 if c != 9 else 0), o) for t, c, n, o in ref] == up, 'pitch +5: notes not moved by 5 (drums kept)')
for p in ('-12', '12'):
    m, _ = play(f'{out}/T.MID', '--pitch', p, '--pitchat', '1530', '0', '--pitchat', '3010', p)
    check(not stuck(m), f'pitch {p} changed during the song: stuck notes {stuck(m)}')
m, _ = play(f'{out}/T.MID', '--pitch', '120'); check(not stuck(m) and not any(len(x) == 4 and x[2] > 127 for x in m), 'pitch out of range')
# A-B loop 1.0..2.975 s (B off the note grid: notes AT B were heard before B was marked), 2 loops: the notes in that part come 3 times, the rest once, nothing stuck
m, c = play(f'{out}/T.MID', '--ab', '1000', '2975', '2')
n = [e for e in notes(m) if e[3] == 'on']; part = [e for e in ref if e[3] == 'on' and 1000000 <= e[0] < 3000000]
check(len(n) == len([e for e in ref if e[3] == 'on']) + 2 * len(part), f'A-B: {len(n)} note-ons, expected {len([e for e in ref if e[3] == "on"]) + 2 * len(part)} ({c})')
check(not stuck(m), 'A-B: stuck notes')
# sleep timer at 2 s: no note-on later, everything silenced
m, c = play(f'{out}/T.MID', '--sleep', '2')
check(any('sleep timer' in x for x in c) and not [e for e in notes(m) if e[3] == 'on' and e[0] > 2001000], 'sleep: notes after 2 s')
check(not stuck(m), 'sleep: stuck notes')
print('fmtcheck:', 'all passed' if not fails else f'{len(fails)} FAILED')
sys.exit(1 if fails else 0)

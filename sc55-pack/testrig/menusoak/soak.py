#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""soak.py q|86|w SOCK SECONDS OUTDIR [seed] - random keys and mouse moves/clicks into the menu for SECONDS.
q = QEMU HMP monitor socket, 86 = 86Box automation socket (testrig/86box), w = QEMU, Windows 98 DOS box (no Esc,
no F8 so the box stays where it is). Screenshots every 60 s into OUTDIR (shotNNN.png / .ppm), keys logged to
OUTDIR/keys.txt. Keys: navigation, digits, letters, F1 F3 F4 F6 F7 F8, the F4 list with Enter/Y/N, Esc now and then."""
import random, socket, sys, time, os, json
kind, sock, secs, out = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
rnd = random.Random(int(sys.argv[5]) if len(sys.argv) > 5 else 1)
os.makedirs(out, exist_ok=True)
s = socket.socket(socket.AF_UNIX); s.connect(sock); f = s.makefile('rwb')
def hmp(c):
    s.sendall((c + '\n').encode()); time.sleep(0.05)
    try:
        s.setblocking(False); s.recv(65536)
    except BlockingIOError: pass
    finally: s.setblocking(True)
def a86(c):
    f.write((c + '\n').encode()); f.flush(); return json.loads(f.readline().decode())
K = {'up': ('up', 0x148), 'down': ('down', 0x150), 'left': ('left', 0x14b), 'right': ('right', 0x14d), 'tab': ('tab', 0x0f),
     'pgup': ('pgup', 0x149), 'pgdn': ('pgdn', 0x151), 'home': ('home', 0x147), 'end': ('end', 0x14f), 'esc': ('esc', 0x01),
     'ret': ('ret', 0x1c), 'y': ('y', 0x15), 'n': ('n', 0x31), 'spc': ('spc', 0x39)}
for i in range(1, 9): K['f%d' % i] = ('f%d' % i, 0x3a + i)
for i, c in enumerate('123456789'): K[c] = (c, 2 + i)
for c, sc in zip('asdgmt', (0x1e, 0x1f, 0x20, 0x22, 0x32, 0x14)): K[c] = (c, sc)
def key(name):
    q, sc = K[name]
    if kind in ('q', 'w'): hmp('sendkey ' + q)
    else: a86('key down %#x' % sc); a86('key up %#x' % sc)
def mouse(dx, dy, b):
    if kind in ('q', 'w'): hmp('mouse_move %d %d' % (dx, dy)); hmp('mouse_button %d' % b)
    else: a86('mouse %d %d %d' % (dx, dy, b))
def shot(n):
    if kind in ('q', 'w'): hmp('screendump %s/shot%03d.ppm' % (out, n))
    else: a86('screenshot %s/shot%03d.png' % (out, n))
nav = ['up', 'down', 'left', 'right', 'tab', 'pgup', 'pgdn', 'home', 'end'] + list('123456789') + list('asdgmt')
fk = ['f1', 'f3', 'f6', 'f7', 'f4'] + ([] if kind == 'w' else ['f8'])
log = open(os.path.join(out, 'keys.txt'), 'w')
t0 = time.time(); n = 0; last = -60
while time.time() - t0 < secs:
    r = rnd.random()
    if r < 0.55: k = rnd.choice(nav)
    elif r < 0.75: k = rnd.choice(fk)
    elif r < 0.85: k = rnd.choice(['y', 'n', 'ret', 'spc'])
    elif r < 0.87 and kind != 'w': k = 'esc'
    else: k = None
    if k: key(k); log.write('%.1f %s\n' % (time.time() - t0, k))
    else:
        dx, dy, b = rnd.randint(-120, 120), rnd.randint(-90, 90), rnd.choice([0, 0, 1, 2])
        mouse(dx, dy, b); mouse(0, 0, 0); log.write('%.1f mouse %d %d %d\n' % (time.time() - t0, dx, dy, b))
    n += 1
    if time.time() - t0 - last >= 60: last = time.time() - t0; shot(int(last // 60))
    log.flush()
    time.sleep(rnd.choice([0.15, 0.3, 0.6, 1.2]))
shot(999)
print('%d inputs in %.0f s' % (n, time.time() - t0))

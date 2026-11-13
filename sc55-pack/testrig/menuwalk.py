#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""menuwalk.py DIR [cats] - text MENU in a running menuq.py VM: every entry of every page is started with Enter and
the walk gets back to the menu (Esc / Space / Q / N to leave each program; MENU.BAT brings the menu back).
Prints one line per entry: category, label, OK / STUCK, and what was on screen in between. cats: e.g. 0,2 (default all)."""
import sys, time
sys.path.insert(0, __import__('os').path.dirname(__file__))
from menuq import Q
CATS = ['Sound', 'Settings', 'Tests', 'Games', 'CD-ROM', 'Files']

def in_menu(t):
    return '┌─ Menu' in t and 'SBEMU ──' in t and 'Enter F1 help' in t
def cur_cat(t):
    rows = t.split('\n')
    for i in range(6):
        if len(rows[8 + i]) > 16 and rows[8 + i][16] == '►': return i
    return -1
def cur_label(t):
    rows = t.split('\n')
    for y in range(8, 14):
        r = rows[y]
        if len(r) > 21 and r[20] == '►': return r[21:76].strip().lstrip('0123456789').strip()
    return None
def back(q, t0=90):
    e = time.time() + t0; seen = []
    while time.time() < e:
        t = q.text()
        if in_menu(t): return True, seen
        line = ' | '.join(l.strip() for l in t.split('\n') if l.strip())[:160]
        if not seen or seen[-1] != line: seen.append(line)
        if 'Press any key' in t or 'any key' in t.lower(): q.key('spc')
        elif 'Keep this mode' in t: q.key('y')
        else: q.key('esc'); time.sleep(1.5); t = q.text()
        if not in_menu(t) and 'Esc' not in t and 'Q' in t: q.key('q')
        time.sleep(2)
    return False, seen

def goto(q, cat, idx):
    for _ in range(20):
        t = q.text()
        c = cur_cat(t)
        if c == cat: break
        q.key('right'); time.sleep(0.4)
    q.key('home'); time.sleep(0.3)
    for _ in range(idx): q.key('down'); time.sleep(0.25)
    time.sleep(0.4)
    return cur_label(q.text())

def walk(d, cats=range(6), skip=()):
    q = Q(d); res = []
    for c in cats:
        first = None
        for i in range(30):
            if not q.wait('Enter F1 help', 60): res.append((CATS[c], '?', 'NO MENU', [])); return res
            lab = goto(q, c, i)
            if lab is None or lab == first: break
            if first is None: first = lab
            if any(s in lab for s in skip): res.append((CATS[c], lab, 'SKIP', [])); continue
            q.key('ret'); time.sleep(3)
            t = q.text()
            if in_menu(t) and cur_label(t) == lab:
                res.append((CATS[c], lab, 'STAYED', [l.strip() for l in t.split('\n')[16:23] if l.strip()][-1:])); continue
            ok, seen = back(q)
            res.append((CATS[c], lab, 'OK' if ok else 'STUCK', seen[:3]))
            print('%-9s %-34s %s %s' % res[-1][:3] + (' :: ' + ' // '.join(seen[:2])[:200] if seen else ''), flush=True)
            if not ok: return res
    return res

if __name__ == '__main__':
    cats = [int(x) for x in sys.argv[2].split(',')] if len(sys.argv) > 2 else range(6)
    for r in walk(sys.argv[1], cats, skip=('3D menu',)):
        if r[2] in ('STAYED', 'SKIP', 'NO MENU'): print('%-9s %-34s %s %s' % (r[0], r[1], r[2], r[3]))

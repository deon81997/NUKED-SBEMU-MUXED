#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# a.py SOCK cmd...   one automation request per argument; prints JSON replies (long fields cut)
# pseudo-commands:  @type:TEXT (types TEXT, '\n' = Enter)   @screen (prints the text screen)
import socket, sys, json
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); f = s.makefile('rwb')
def req(c):
    f.write((c + '\n').encode()); f.flush(); return json.loads(f.readline().decode())
K = {}
for i, ch in enumerate('1234567890-='): K[ch] = 2 + i
for i, ch in enumerate('qwertyuiop[]'): K[ch] = 0x10 + i
for i, ch in enumerate("asdfghjkl;'"): K[ch] = 0x1e + i
for i, ch in enumerate('zxcvbnm,./'): K[ch] = 0x2c + i
K.update({' ': 0x39, '\n': 0x1c, '\\': 0x2b, '`': 0x29})
SH = {'!':'1','@':'2','#':'3','$':'4','%':'5','^':'6','&':'7','*':'8','(':'9',')':'0','_':'-','+':'=',':':';','"':"'",'<':',','>':'.','?':'/','|':'\\','~':'`'}
for c in sys.argv[2:]:
    if c.startswith('@type:'):
        for ch in c[6:].replace('\\n', '\n'):
            sh = ch.isupper() or ch in SH
            k = K[SH.get(ch, ch.lower())]
            if sh: req('key down 0x2a')
            req('key down %#x' % k); req('key up %#x' % k)
            if sh: req('key up 0x2a')
        print('typed'); continue
    if c == '@screen':
        j = req('text')
        if not j.get('ok'): print(j); continue
        for row in j['chars']:
            print(bytes.fromhex(row).decode('cp437').rstrip())
        continue
    j = req(c)
    for k in ('data', 'chars', 'attrs'):
        if k in j and isinstance(j[k], str) and len(j[k]) > 200: j[k] = j[k][:200] + '...'
    print(json.dumps(j))

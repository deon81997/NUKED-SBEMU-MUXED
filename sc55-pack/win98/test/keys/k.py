#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# k.py SOCK keyspec... : 'enter' 'esc' 'tab' 'alt+f4' 'win+r' or '@type:..' or 'shot' or 'sleep:N'
import socket, sys, json, time
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
N = {'enter':0x1c,'esc':1,'tab':0xf,'alt':0x38,'ctrl':0x1d,'shift':0x2a,'space':0x39,'f1':0x3b,'f4':0x3e,'win':0xe05b,
     'up':0xe048,'down':0xe050,'left':0xe04b,'right':0xe04d,'bksp':0xe,'del':0xe053,'f10':0x44}
for i in range(1,11): N['f%d'%i]=0x3a+i
def kd(c): req('key down %#x' % c)
def ku(c): req('key up %#x' % c)
for c in sys.argv[2:]:
    if c.startswith('@type:'):
        for ch in c[6:].replace('\\n', '\n'):
            sh = ch.isupper() or ch in SH
            k = K[SH.get(ch, ch.lower())]
            if sh: kd(0x2a)
            kd(k); time.sleep(0.03); ku(k)
            if sh: ku(0x2a)
            time.sleep(0.05)
    elif c.startswith('sleep:'): time.sleep(float(c[6:]))
    elif c.startswith('shot'):
        p = c[5:] or '/tmp/v.png'; print(req('screenshot ' + p))
    else:
        ks = [N.get(p, K.get(p)) for p in c.split('+')]
        for k in ks: kd(k); time.sleep(0.03)
        for k in reversed(ks): ku(k); time.sleep(0.03)
        time.sleep(0.15)

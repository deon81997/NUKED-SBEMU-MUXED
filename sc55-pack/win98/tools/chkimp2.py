#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""chkimp.py without pefile: imports and the Windows 98 DLLs' exports read with i686-w64-mingw32-objdump -p.
Usage: chkimp2.py file.exe [more...]   (DLL dir: $W98DLL or /home/claude/w98dll)"""
import os, re, subprocess, sys
D = os.environ.get('W98DLL', '/home/claude/w98dll')
OBJ = os.environ.get('OBJDUMP', 'i686-w64-mingw32-objdump')
W_OK = {'lstrlenW','MultiByteToWideChar','WideCharToMultiByte','GetCommandLineW','ExtTextOutW','TextOutW',
        'MessageBoxW','MessageBoxExW','GetTextExtentPoint32W','GetCharWidthW','EnumResourceLanguagesW',
        'FindResourceW','FindResourceExW','lstrcpyW','lstrcpynW','lstrcatW','GetStringTypeW','LCMapStringW','CompareStringW'}
def dump(p): return subprocess.run([OBJ, '-p', p], capture_output=True, text=True).stdout
cache = {}
def exports(dll):
    k = dll.upper()
    if k not in cache:
        p = os.path.join(D, k)
        if not os.path.exists(p): cache[k] = None
        else:
            s = set(); t = dump(p); on = False
            for ln in t.splitlines():
                if ln.startswith('[Ordinal/Name Pointer] Table'): on = True; continue
                if on:
                    m = re.match(r'\s*\[\s*(\d+)\]\s+(\S+)', ln)
                    if m: s.add(m.group(2))
                    elif ln.strip() == '': on = False
            for m in re.finditer(r'^\s*\[\s*\d+\]\s+\+base\[\s*(\d+)\]', t, re.M): s.add('#' + m.group(1))
            cache[k] = s
    return cache[k]
bad = 0
for f in sys.argv[1:]:
    print('==', f); dll = None
    for ln in dump(f).splitlines():
        m = re.match(r'\s*DLL Name: (\S+)', ln)
        if m: dll = m.group(1); continue
        m = re.match(r'\s+[0-9a-f]+\s+(\d+)\s+(\S+)$', ln)
        if dll and m:
            n = m.group(2); ex = exports(dll)
            if ex is None: print('  NO-DLL ', dll, n); bad += 1
            elif n not in ex: print('  MISSING', dll, n); bad += 1
            elif n.endswith('W') and dll.upper() in ('KERNEL32.DLL','USER32.DLL','GDI32.DLL','ADVAPI32.DLL') and n not in W_OK:
                print('  W-STUB ', dll, n); bad += 1
print('problems:', bad); sys.exit(1 if bad else 0)

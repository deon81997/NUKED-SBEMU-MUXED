#!/usr/bin/env python3
"""Check a PE file's imports against the real Windows 98 DLL exports.
Usage: chkimp.py file.exe [more...]   (DLL dir: /home/claude/w98dll)"""
import pefile, sys, os
D='/home/claude/w98dll'
# Unicode APIs that actually work on Windows 98 (most other *W are failing stubs)
W_OK={'lstrlenW','MultiByteToWideChar','WideCharToMultiByte','GetCommandLineW','ExtTextOutW','TextOutW',
      'MessageBoxW','MessageBoxExW','GetTextExtentPoint32W','GetCharWidthW','EnumResourceLanguagesW',
      'FindResourceW','FindResourceExW','lstrcpyW','lstrcpynW','lstrcatW','GetStringTypeW','LCMapStringW','CompareStringW'}
cache={}
def exports(dll):
    k=dll.upper()
    if k in cache: return cache[k]
    p=os.path.join(D,k)
    if not os.path.exists(p): cache[k]=None; return None
    pe=pefile.PE(p, fast_load=True); pe.parse_data_directories(directories=[0])
    s=set()
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if e.name: s.add(e.name.decode())
        s.add('#%d'%e.ordinal)
    cache[k]=s; return s
bad=0
for f in sys.argv[1:]:
    pe=pefile.PE(f)
    print('==',f)
    for imp in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[]):
        dll=imp.dll.decode(); ex=exports(dll)
        for s in imp.imports:
            n=s.name.decode() if s.name else '#%d'%s.ordinal
            if ex is None: print('  NO-DLL ',dll,n); bad+=1; continue
            if n not in ex: print('  MISSING',dll,n); bad+=1
            elif n.endswith('W') and dll.upper() in ('KERNEL32.DLL','USER32.DLL','GDI32.DLL','ADVAPI32.DLL') and n not in W_OK:
                print('  W-STUB ',dll,n); bad+=1
print('problems:',bad); sys.exit(1 if bad else 0)

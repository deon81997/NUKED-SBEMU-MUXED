import sys; sys.path.insert(0,'/home/claude/nuked-sbemu-muxed/sc55-pack/testrig/menumx')
from mxvm import *
v=VM86(sys.argv[1]); v.attach()
def cat(name):
    for _ in range(8):
        if any('┌─' in r and name in r for r in v.screen()[0][4:8]): return True
        v.key('right', 300)
    return False
def sel(name):
    v.key('home',300)
    for _ in range(40):
        rows=v.screen()[0]
        for r in rows:
            if len(r)>1 and r[1]=='\x10': 
                if name.upper() in r.upper(): return True
        v.key('down',250)
    return False
exec(sys.argv[2])

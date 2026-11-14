import sys, time
sys.path.insert(0,'/home/claude/nuked-sbemu-muxed/sc55-pack/testrig/86box')
from regress import VM, ENTER, ESC
vm=VM('/home/claude/86vm/u1','/tmp/shots')
import json as _j
def _screen():
    j=vm.req('text')
    if not j.get('ok'): return ''
    return '\n'.join(bytes.fromhex(r).replace(b'\n',b'\x09').replace(b'\r',b'\x09').decode('cp437').rstrip() for r in j['chars'])
vm.screen=_screen
import os; os.makedirs('/tmp/shots',exist_ok=True)
def scr(n=25):
    s=vm.screen().split('\n'); return '\n'.join(l for l in s[-n:] if l.strip())
import socket
def attach():
    vm.s=socket.socket(socket.AF_UNIX); vm.s.connect('/home/claude/86vm/u1/a.sock'); vm.f=vm.s.makefile('rwb')
if os.environ.get('ATTACH'): attach()
def K(sc, ms=300):
    vm.req('key down %#x'%sc); vm.req('key up %#x'%sc); vm.step(ms)
LEFT,RIGHT,UP,DOWN,HOME=0xe04b,0xe04d,0xe048,0xe050,0xe047
def pick(c,i):
    for _ in range(7): K(LEFT,150)
    for _ in range(c): K(RIGHT,150)
    K(HOME,150)
    for _ in range(i): K(DOWN,150)
    t=[l for l in vm.screen().split('\n') if '─ ' in l and '┌─ Menu' not in l and l.strip().startswith('┌')]
    return t[0].strip()[:60] if t else '?'
def inmenu(): return '┌─ Menu' in vm.screen()
def top(n=6): return '\n'.join([l.rstrip() for l in vm.screen().split('\n') if l.strip()][:n])
def go(c,i,wait,exitkeys,ew=3000,show=6):
    name=pick(c,i); K(ENTER, wait); s=top(show)
    for k in exitkeys: K(k,ew)
    ok=inmenu()
    if not ok: K(ENTER,4000); ok=inmenu()   # "press a key" pause screens
    print('==',c,i,name[3:40].strip(' ─'),'-> back' if ok else '-> NOT BACK'); print(s)
    return ok
CATS=["Sound","Settings","Tests","Games","CD-ROM","Files"]
def curcat():
    for l in vm.screen().split('\n'):
        if '┌─ Menu' in l:
            for i,c in enumerate(CATS):
                if ' '+c+' ' in l.split('┐',1)[1]: return i
    return -1
def pick(c,i):
    for _ in range(8):
        if curcat()==c: break
        K(RIGHT,200)
    K(HOME,150)
    for _ in range(i): K(DOWN,150)
    t=[l for l in vm.screen().split('\n') if '─ ' in l and '┌─ Menu' not in l and l.strip().startswith('┌')]
    return t[0].strip()[:60] if t else '?'
def go(c,i,wait,exitkeys,ew=3000,show=6):
    if not inmenu():
        vm.run("C:\\SC55\\MENU",8000)
    name=pick(c,i); K(ENTER, wait)
    if inmenu():
        print('==',c,i,name[3:40].strip(' ─'),'-> not started'); return None
    s=top(show)
    for k in exitkeys: K(k,ew)
    ok=inmenu()
    if not ok:
        s2=top(25)
        if 'Press' in s2 or 'key' in s2.lower(): K(ENTER,6000); ok=inmenu()
    print('==',c,i,name[3:40].strip(' ─'),'-> back' if ok else '-> NOT BACK'); print(s)
    if not ok: print('NOW:\n'+top(25))
    return ok

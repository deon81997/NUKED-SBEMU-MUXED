import sys, os; sys.path.insert(0,'/home/claude/nuked-sbemu-muxed/sc55-pack/testrig/menumx'); sys.path.insert(0,'/home/claude/mx')
from mxvm import *; from gotq import got_quit
D=sys.argv[1]; cyl=int(sys.argv[2]); n=int(sys.argv[3]); pre=sys.argv[4:]
v=VM86(D); v.stop(); v.start(cfg86(cyl))
if v.wait_text('Press F1 to continue', 40000): v.key('f1', 500)
v.wait_text('MX READY', 240000)
for c in pre: v.cmd(c,3000)
v.cmd('CD \\GOT',800)
res=[]
for i in range(n):
    v.cmd('CLS',600)
    if os.environ.get('DELCFG'): v.cmd('DEL CONFIG.GOT',800)
    v.cmd(os.environ.get('GOTCMD','GOT'),1000)
    r='?'
    for t in range(30):
        v.sleep(1000)
        if not v.req('text').get('ok'): r='ok'; break
        if 'would appear' in v.text(): r='FAIL'; break
    res.append(r)
    if r=='ok':
        v.sleep(3000); got_quit(v, D+'/q.png'); v.sleep(3000); v.key('ret',800); v.key('ret',800)
    else:
        v.sleep(3000)
print(res); v.cmd('C:\\SC55\\SBDIAG.EXE FLUSH',3000); v.sleep(2000)
v.stop()

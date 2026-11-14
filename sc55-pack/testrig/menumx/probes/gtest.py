# boot, run PRE (list of commands / 'MENUESC'), then GOT: prints whether GOT got to graphics
import sys; sys.path.insert(0,'/home/claude/nuked-sbemu-muxed/sc55-pack/testrig/menumx'); sys.path.insert(0,'/home/claude/mx')
from mxvm import *
import os
GOTCMD=os.environ.get('GOTCMD','GOT')
D=sys.argv[1]; cyl=int(sys.argv[2]); pre=sys.argv[3:]
c=cfg86(cyl)
if os.environ.get('SERIAL'): c=c.replace('[Ports (COM & LPT)]','[Ports (COM & LPT)]\nserial1_device = stdio')+'\n[Virtual Console (COM) #1]\nmode = 0\n'
v=VM86(D); v.stop(); v.start(c)
if v.wait_text('Press F1 to continue', 40000): v.key('f1', 500)
v.wait_text('MX READY', 240000)
for c in pre:
    if c=='ESC': v.key('esc',4000)
    elif c.startswith('W'): v.sleep(int(c[1:]))
    else: v.cmd(c,3000)
v.cmd('CLS',800); v.cmd('C:\\T\\PIC',1200)
v.cmd('CD \\GOT',800); v.cmd(GOTCMD,2000)
for i in range(20):
    v.sleep(1000)
    if not v.req('text').get('ok'): print('GOT graphics (sound OK)'); break
    if 'would appear' in v.text(): print('GOT: BLASTER message (SB not found)'); break
print([r.strip() for r in v.screen()[0][:3]])

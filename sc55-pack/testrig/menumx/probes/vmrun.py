# vmrun.py D cyl [files to copy into C:\T] -- cmds...   (boots, CDON + mount RAP, runs cmds, prints screen)
import sys, subprocess; sys.path.insert(0,'/home/claude/nuked-sbemu-muxed/sc55-pack/testrig/menumx')
from mxvm import *
D=sys.argv[1]; cyl=int(sys.argv[2]); a=sys.argv[3:]; i=a.index('--'); files=a[:i]; cmds=a[i+1:]
v=VM86(D); v.stop()
for f in files: subprocess.run(['mcopy','-o','-i',D+'/hd.img@@32256',f,'::T/'],check=True)
v.start(cfg86(cyl))
if v.wait_text('Press F1 to continue', 40000): v.key('f1', 500)
v.wait_text('MX READY', 240000)
for c in cmds:
    if c=='CLS': v.cmd('CLS',500)
    elif c.startswith('PRINT'): print(v.text())
    else: v.cmd(c,4000)
print(v.text())

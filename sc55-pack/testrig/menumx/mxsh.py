import sys; sys.path.insert(0,'/home/claude/nuked-sbemu-muxed/sc55-pack/testrig/menumx')
from mxvm import *
d=sys.argv[1]; a=sys.argv[2:]
v=VM86(d)
if a[0]=='start':
    v.start(cfg86(int(a[1]))); print('started'); sys.exit()
v.attach()
i=0
while i<len(a):
    c=a[i]; i+=1
    if c=='scr': print('\n'.join(r.rstrip() for r in v.screen()[0]).rstrip())
    elif c=='k': v.key(a[i]); i+=1
    elif c=='t': v.type(a[i].replace('\\n','\n')); i+=1
    elif c=='s': v.sleep(int(a[i])); i+=1
    elif c=='lvl': print(v.level(int(a[i]))); i+=1
    elif c=='w': print(v.wait_text(a[i], int(a[i+1]))); i+=2
    elif c=='r': print(v.req(a[i])); i+=1

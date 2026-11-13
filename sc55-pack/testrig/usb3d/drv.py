import socket, sys, time, os
def mon(cmd, wait=0.05):
    s = socket.socket(socket.AF_UNIX); s.connect(os.environ.get('T3D','/home/claude/t3d')+'/mon.sock'); s.settimeout(1)
    try: s.recv(4096)
    except Exception: pass
    s.sendall((cmd + '\n').encode()); time.sleep(wait)
    out=b''
    try:
        while True:
            b=s.recv(65536)
            if not b: break
            out+=b
    except Exception: pass
    s.close(); return out.decode('latin-1','replace')
def shot(name):
    from PIL import Image
    p=os.environ.get('T3D','/home/claude/t3d')+'/'+name+'.ppm'; mon('screendump '+p, 1.0)
    Image.open(p).save(os.environ.get('T3D','/home/claude/t3d')+'/'+name+'.png'); os.remove(p)
if __name__=='__main__':
    c=sys.argv[1]
    if c=='shot': shot(sys.argv[2])
    elif c=='key':
        for k in sys.argv[2:]: mon('sendkey '+k); time.sleep(0.15)
    else: print(mon(' '.join(sys.argv[1:]), 0.5))

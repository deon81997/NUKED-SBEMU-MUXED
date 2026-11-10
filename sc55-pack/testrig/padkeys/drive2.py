#!/usr/bin/env python3
"""drive2.py IMG STEPS: QEMU + fakepad; when C:\\STAGE.TXT says REAL, run STEPS: lines 'wait s' | 'shot NAME' |
'pad STATE' (held) | 'tap STATE [hold]' | 'quit'. Screenshots as PNG in /home/claude/padtest/shots."""
import os, subprocess, sys, time, socket
from PIL import Image
TR = '/home/claude/NUKED-SBEMU-MUXED/sc55-pack/testrig'
QEMU = '/home/claude/src/qemu/build/qemu-system-i386'
img, steps = sys.argv[1], open(sys.argv[2]).read().splitlines()
ST = '/tmp/pad.st'; SH = os.environ.get('SHOTS', '/home/claude/padtest/shots'); os.makedirs(SH, exist_ok=True)
open(ST, 'w').write('buttons=0')
def mon(cmd):
    s = socket.socket(socket.AF_UNIX); s.connect('/tmp/mon.sock'); s.settimeout(2)
    b = b''
    t = time.time()
    while b'(qemu)' not in b and time.time() - t < 3:
        try: b += s.recv(4096)
        except Exception: break
    s.send((cmd + '\n').encode())
    out = b''
    t = time.time()
    while time.time() - t < 5:
        try:
            d = s.recv(65536)
            if not d: break
            out += d
            if out.rstrip().endswith(b'(qemu)'): break
        except Exception: break
    s.close()
    return out
def stage():
    if os.path.exists('/tmp/stage.txt'): os.remove('/tmp/stage.txt')
    subprocess.run(['python3', TR + '/mkdisk.py', 'get', img, 'STAGE.TXT', '/tmp/stage.txt'], capture_output=True)
    try: return open('/tmp/stage.txt').read().strip()
    except OSError: return ''
fp = subprocess.Popen(['python3', TR + '/fakepad.py', '4711', 'live:' + ST]); time.sleep(0.5)
if os.path.exists('/tmp/mon.sock'): os.remove('/tmp/mon.sock')
q = subprocess.Popen([QEMU, '-machine', 'pc', '-cpu', 'core2duo', '-m', '64', '-drive', 'file=%s,format=raw,if=ide,index=0' % img,
    '-boot', 'c', '-audiodev', 'none,id=snd', '-device', 'AC97,audiodev=snd', '-display', 'none', '-vga', 'std',
    '-monitor', 'unix:/tmp/mon.sock,server,nowait', '-chardev', 'socket,id=pad,host=127.0.0.1,port=4711',
    '-device', 'piix3-usb-uhci,id=uhci', '-device', 'usb-redir,chardev=pad,bus=uhci.0'], stdout=open('/tmp/qemu.log', 'w'), stderr=subprocess.STDOUT)
t0 = time.time()
while time.time() - t0 < 120 and stage() != 'REAL': time.sleep(1)
print('REAL at %.1f' % (time.time() - t0), flush=True)
for ln in steps:
    a = ln.split()
    if not a or a[0].startswith('#'): continue
    if a[0] == 'wait': time.sleep(float(a[1]))
    elif a[0] == 'shot':
        if os.path.exists('/tmp/s.ppm'): os.remove('/tmp/s.ppm')
        r = mon('screendump /tmp/s.ppm')
        for _ in range(20):
            if os.path.exists('/tmp/s.ppm') and os.path.getsize('/tmp/s.ppm') > 1000: break
            time.sleep(0.25)
        if not os.path.exists('/tmp/s.ppm'): print('no shot', r[-200:]); continue
        time.sleep(0.3)
        Image.open('/tmp/s.ppm').save('%s/%s.png' % (SH, a[1])); print('shot', a[1], flush=True)
    elif a[0] == 'pad': open(ST, 'w').write(' '.join(a[1:]) or 'buttons=0')
    elif a[0] == 'tap':
        open(ST, 'w').write(a[1]); time.sleep(float(a[2]) if len(a) > 2 else 0.3); open(ST, 'w').write('buttons=0'); time.sleep(0.3)
    elif a[0] == 'quit': break
mon('quit'); time.sleep(1); q.kill(); fp.kill()

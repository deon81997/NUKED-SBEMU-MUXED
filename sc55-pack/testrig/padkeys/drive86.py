#!/usr/bin/env python3
"""drive86.py VMDIR: boot the 86Box VM (stepped emulated time), press pad buttons per C:\\STAGE.TXT stage."""
import json, os, socket, subprocess, sys, time
V = sys.argv[1]; TR = '/home/claude/NUKED-SBEMU-MUXED/sc55-pack/testrig'
B86 = '/home/claude/src/86box/build/src/86Box'; ROMS = '/home/claude/src/roms'
ST = '/tmp/xpad1.st'; open(ST, 'w').write('buttons=0')
subprocess.run(['pkill', '-f', '86Box.*' + V]); time.sleep(0.5)
if os.path.exists(V + '/a.sock'): os.remove(V + '/a.sock')
if subprocess.run(['pgrep', '-f', 'Xvfb :9'], capture_output=True).returncode:
    subprocess.Popen(['Xvfb', ':9', '-screen', '0', '1024x768x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); time.sleep(1)
env = dict(os.environ, DISPLAY=':9', SDL_AUDIODRIVER='dummy', XPAD='1', XPAD_STATE=ST)
p = subprocess.Popen([B86, '-R', ROMS, '-N', '-L', V + '/86box.log', '--automation', 'unix:' + V + '/a.sock', '-P', V],
                     cwd=V, env=env, stdout=open(V + '/stdout.log', 'w'), stderr=subprocess.STDOUT)
for _ in range(100):
    if os.path.exists(V + '/a.sock'): break
    time.sleep(0.2)
s = socket.socket(socket.AF_UNIX); s.connect(V + '/a.sock'); f = s.makefile('rwb')
def req(c):
    f.write((c + '\n').encode()); f.flush(); return json.loads(f.readline().decode())
def step(ms): req('step %d' % ms)
def stage():
    if os.path.exists('/tmp/stage86.txt'): os.remove('/tmp/stage86.txt')
    subprocess.run(['python3', TR + '/mkdisk.py', 'get', V + '/hd.img', 'STAGE.TXT', '/tmp/stage86.txt'], capture_output=True)
    try: return open('/tmp/stage86.txt').read().strip()
    except OSError: return ''
def pad(st): open(ST, 'w').write(st); time.sleep(0.05)
SEQ = {
 'KEEN': ['buttons=1000', 'buttons=2000', 'buttons=4000', 'buttons=8000', 'lx=-32768', 'buttons=0001', 'buttons=0010', 'rt=255', 'buttons=0200'],
 'WOLF': ['buttons=0200', 'buttons=4000', 'lx=-32768', 'buttons=8000', 'lt=255'],
 'DEF':  ['buttons=1000', 'buttons=0001', 'buttons=0010', 'buttons=0020', 'buttons=8000'],
}
print(req('step 12000')); req('key down 0x3b'); req('key up 0x3b')
t0 = time.time(); emu = 12; done = set(); last = ''
while emu < 400 and time.time() - t0 < 900:
    step(2000); emu += 2
    st = stage()
    if st != last: print('emu %ds (wall %.0fs) stage %s' % (emu, time.time() - t0, st), flush=True); last = st
    if st in SEQ and st not in done:
        done.add(st); step(4000); emu += 4
        for x in SEQ[st]:
            pad(x); step(600); pad('buttons=0'); step(600); emu += 1.2
    if st == 'DONE': step(2000); break
j = req('screenshot'); print('screenshot keys', list(j.keys())[:5])
s.close(); p.kill(); print('end')

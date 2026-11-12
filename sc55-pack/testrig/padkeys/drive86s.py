#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""drive86s.py VMDIR STEPS: 86Box (stepped emulated time) + emulated 360 pad; when C:\\STAGE.TXT says REAL, run STEPS
(wait s | shot NAME | pad STATE | tap STATE [hold]). PNG screenshots into $SHOTS."""
import json, os, socket, subprocess, sys, time
V, steps = sys.argv[1], open(sys.argv[2]).read().splitlines()
TR = '/home/claude/NUKED-SBEMU-MUXED/sc55-pack/testrig'
B86 = '/home/claude/src/86box/build/src/86Box'; ROMS = '/home/claude/src/roms'
SH = os.environ.get('SHOTS', '/home/claude/padtest/shots86'); os.makedirs(SH, exist_ok=True)
ST = os.environ.get('XPST', '/tmp/xpad1.st'); open(ST, 'w').write('buttons=0')
subprocess.run(['pkill', '-f', '86Box.*' + V]); time.sleep(0.5)
if os.path.exists(V + '/a.sock'): os.remove(V + '/a.sock')
if subprocess.run(['pgrep', '-f', 'Xvfb :9'], capture_output=True).returncode:
    subprocess.Popen(['Xvfb', ':9', '-screen', '0', '1024x768x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); time.sleep(1)
env = dict(os.environ, DISPLAY=':9', SDL_AUDIODRIVER='dummy', ALSOFT_DRIVERS='null', XPAD='1', XPAD_STATE=ST)
p = subprocess.Popen([B86, '-R', ROMS, '-N', '--automation', 'unix:' + V + '/a.sock', '-P', V],
                     cwd=V, env=env, stdout=open(V + '/stdout.log', 'w'), stderr=subprocess.STDOUT)
for _ in range(100):
    if os.path.exists(V + '/a.sock'): break
    time.sleep(0.2)
time.sleep(0.5)
s = socket.socket(socket.AF_UNIX); s.connect(V + '/a.sock'); f = s.makefile('rwb')
def req(c):
    f.write((c + '\n').encode()); f.flush(); return json.loads(f.readline().decode())
def step(sec): req('step %d' % int(sec * 1000))
def stage():
    if os.path.exists(os.environ.get('XPST', '/tmp/x') + '.stage'): os.remove(os.environ.get('XPST', '/tmp/x') + '.stage')
    subprocess.run(['python3', TR + '/mkdisk.py', 'get', V + '/hd.img', 'STAGE.TXT', os.environ.get('XPST', '/tmp/x') + '.stage'], capture_output=True)
    try: return open(os.environ.get('XPST', '/tmp/x') + '.stage').read().strip()
    except OSError: return ''
def pad(x): open(ST, 'w').write(x); time.sleep(0.05)
step(12); req('key down 0x3b'); req('key up 0x3b')
for _ in range(60):
    step(2)
    if stage() == 'REAL': break
print('REAL', flush=True)
for ln in steps:
    a = ln.split()
    if not a or a[0].startswith('#'): continue
    if a[0] == 'wait': step(float(a[1]))
    elif a[0] == 'shot': print(req('screenshot %s/%s.png' % (SH, a[1])).get('ok'), a[1], flush=True)
    elif a[0] == 'key': req('key down %s' % a[1]); step(0.15); req('key up %s' % a[1]); step(0.2)
    elif a[0] == 'pad': pad(' '.join(a[1:]) or 'buttons=0')
    elif a[0] == 'tap': pad(a[1]); step(float(a[2]) if len(a) > 2 else 0.3); pad('buttons=0'); step(0.3)
s.close(); p.kill()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""drive.py IMG [qemu usb args...]: boot the DOS test disk in QEMU with fakepad (live), press pad buttons for each
test stage (C:\\STAGE.TXT), quit when DONE. Env: QEMU, TMAX (s)."""
import os, subprocess, sys, time
TR = '/home/claude/NUKED-SBEMU-MUXED/sc55-pack/testrig'
QEMU = os.environ.get('QEMU', '/home/claude/src/qemu/build/qemu-system-i386')
img = sys.argv[1]; usb = sys.argv[2:] or ['-device', 'piix3-usb-uhci,id=uhci', '-device', 'usb-redir,chardev=pad,bus=uhci.0']
ST = '/tmp/pad.st'
open(ST, 'w').write('buttons=0')
def pad(s):
    open(ST, 'w').write(s); print('  pad', s, flush=True)
def stage():
    if os.path.exists('/tmp/stage.txt'): os.remove('/tmp/stage.txt')
    r = subprocess.run(['python3', TR + '/mkdisk.py', 'get', img, 'STAGE.TXT', '/tmp/stage.txt'], capture_output=True)
    try: return open('/tmp/stage.txt').read().strip()
    except OSError: return ''
SEQ = {
 'KEEN': ['buttons=1000', 'buttons=2000', 'buttons=4000', 'buttons=8000', 'lx=-32768', 'buttons=0001', 'buttons=0010', 'rt=255', 'buttons=0200'],
 'KEEN2': ['buttons=1000', 'buttons=2000', 'buttons=0010', 'buttons=0020', 'buttons=0001'],
 'WOLF': ['buttons=0200', 'buttons=4000', 'lx=-32768', 'buttons=8000', 'lt=255'],
 'DEF':  ['buttons=1000', 'buttons=0001', 'buttons=0010', 'buttons=0020', 'buttons=8000'],
}
fp = subprocess.Popen(['python3', TR + '/fakepad.py', '4711', 'live:' + ST])
time.sleep(0.5)
if os.path.exists('/tmp/mon.sock'): os.remove('/tmp/mon.sock')
q = subprocess.Popen([QEMU, '-machine', 'pc', '-cpu', 'core2duo', '-m', '64', '-drive', 'file=%s,format=raw,if=ide,index=0' % img,
    '-boot', 'c', '-audiodev', 'none,id=snd', '-device', 'AC97,audiodev=snd', '-display', 'none', '-vga', 'std',
    '-monitor', 'unix:/tmp/mon.sock,server,nowait', '-chardev', 'socket,id=pad,host=127.0.0.1,port=4711'] + usb,
    stdout=open('/tmp/qemu.log', 'w'), stderr=subprocess.STDOUT)
t0 = time.time(); done = set(); last = ''
while time.time() - t0 < float(os.environ.get('TMAX', '240')):
    time.sleep(1)
    s = stage()
    if s != last: print('%5.1f stage %s' % (time.time() - t0, s), flush=True); last = s
    if s in SEQ and s not in done:
        done.add(s); time.sleep(4)
        for st in SEQ[s]:
            pad(st); time.sleep(0.8); pad('buttons=0'); time.sleep(0.8)
    if s == 'DONE': time.sleep(2); break
subprocess.run('echo quit | socat - UNIX-CONNECT:/tmp/mon.sock', shell=True, capture_output=True)
time.sleep(1); q.kill(); fp.kill()
print('end %.1f s' % (time.time() - t0))

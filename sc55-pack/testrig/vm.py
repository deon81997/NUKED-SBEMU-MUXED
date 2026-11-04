#!/usr/bin/env python3
"""vm.py - run the DOS test PC in QEMU (headless) and drive it through the HMP monitor.
   python3 vm.py start            boots c.img (AC97 sound card like the D810, 486/Pentium-class TCG CPU)
   python3 vm.py shot NAME        screenshot to shots/NAME.png
   python3 vm.py key K [K ...]    QEMU key names (esc, up, ret, spc, f10, y, ctrl-alt-f12 ...)
   python3 vm.py type TEXT        types text, \\n = Enter
   python3 vm.py cmd HMPCOMMAND   any monitor command
   python3 vm.py stop"""
import os, socket, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
QEMU = '/home/claude/work/qemu/b7/qemu-system-i386'
BIOS = '/home/claude/work/qemu/src/pc-bios'
SOCK = os.path.join(HERE, 'mon.sock')

def mon(cmd, wait=0.3):
    s = socket.socket(socket.AF_UNIX); s.connect(SOCK)
    s.settimeout(2)
    try:
        s.recv(4096)
    except Exception:
        pass
    s.sendall((cmd + '\n').encode())
    time.sleep(wait)
    out = b''
    try:
        while True:
            b = s.recv(65536)
            if not b: break
            out += b
    except Exception:
        pass
    s.close()
    return out.decode('latin-1', 'replace')

def shot(name):
    os.makedirs(os.path.join(HERE, 'shots'), exist_ok=True)
    ppm = os.path.join(HERE, 'shots', name + '.ppm')
    mon('screendump ' + ppm, 1.0)
    from PIL import Image
    im = Image.open(ppm); png = ppm[:-4] + '.png'
    if im.width < 640: im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    im.save(png); os.remove(ppm)
    print(png)

KEYMAP = {' ': 'spc', '\n': 'ret', '\\': 'backslash', ':': 'shift-semicolon', '.': 'dot', '-': 'minus', '/': 'slash',
          '_': 'shift-minus', '=': 'equal', ',': 'comma', ';': 'semicolon', '*': 'shift-8', '>': 'shift-dot', '@': 'shift-2'}
def typ(text):
    for ch in text:
        if ch in KEYMAP: k = KEYMAP[ch]
        elif ch.isupper(): k = 'shift-' + ch.lower()
        else: k = ch
        mon('sendkey ' + k, 0.08)

if __name__ == '__main__':
    a = sys.argv[1]
    if a == 'start':
        img = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'c.img')
        if os.path.exists(SOCK): os.remove(SOCK)
        args = [QEMU, '-L', BIOS, '-machine', 'pc', '-cpu', 'pentium3', '-m', '256',
                '-drive', 'file=%s,format=raw,if=ide,index=0' % img, '-cdrom', os.path.join(HERE, 'duke.iso'), '-boot', 'c',
                '-audiodev', 'none,id=snd', '-device', 'AC97,audiodev=snd',
                '-display', 'none', '-vga', 'std', '-monitor', 'unix:%s,server,nowait' % SOCK,
                '-rtc', 'base=localtime']
        log = open(os.path.join(HERE, 'qemu.log'), 'w')
        p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        open(os.path.join(HERE, 'qemu.pid'), 'w').write(str(p.pid))
        time.sleep(1.5); print('started', p.pid, 'alive' if p.poll() is None else 'DIED')
    elif a == 'shot': shot(sys.argv[2])
    elif a == 'key':
        for k in sys.argv[2:]: mon('sendkey ' + k, 0.15)
    elif a == 'type': typ(sys.argv[2].replace('\\n', '\n'))
    elif a == 'cmd': print(mon(' '.join(sys.argv[2:]), 1.0))
    elif a == 'stop':
        try: mon('quit', 0.5)
        except Exception: pass
        print('stopped')

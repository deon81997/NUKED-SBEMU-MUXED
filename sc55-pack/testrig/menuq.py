#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""menuq.py - the pack installed by its own SETUP.BAT on a QEMU DOS PC (FreeDOS or MS-DOS 7.1), driven through MENU.

   python3 menuq.py DIR build [msdos|freedos]    DIR/c.img: DOS + NUKEDSB (from /home/claude/rel, SC55MENU.EXE from
                                                 output/) + a SoundFont; AUTOEXEC installs (SETUP C) and runs MENU
   from menuq import Q; q = Q(DIR); q.start(usb='ohci'); q.wait('NUKED'); q.key('down', 'ret'); q.text(); q.shot('x')

Environment: MS71=/tmp/dl/w98qi/ms71 (IO.SYS MSDOS.SYS COMMAND.COM from QuickInstall's FULL.866, testrig/padkeys/mercy.py),
FD=/home/claude/fd (FreeDOS KERNEL.SYS COMMAND.COM), SF2=/tmp/dl/timgm6mb/TIMGM6MB.SF2 (pretty-midi's TimGM6mb, GPL)."""
import os, shutil, socket, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
REL = os.environ.get('REL', '/home/claude/rel/NUKEDSB')
MS71 = os.environ.get('MS71', '/tmp/dl/w98qi/ms71')
FD = os.environ.get('FD', '/home/claude/fd')
SF2 = os.environ.get('SF2', '/tmp/dl/timgm6mb/TIMGM6MB.SF2')
# CP437 as drawn on screen: 00-1F and 7F are glyphs (►, ♪...), not control characters
GLYPH = ' ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼' + bytes(range(32, 127)).decode() + '⌂' + bytes(range(128, 256)).decode('cp437')

def build(d, dos='msdos', sbopts='/A:220 /I:5 /D:1 /P:330 /VSF:C:\\SC55\\SF2\\TIMGM6MB.SF2 /JOY:1 /USBKB:1 /USBMS:2',
          menu=None, extra=None):
    w = os.path.join(d, 'c'); shutil.rmtree(w, ignore_errors=True); os.makedirs(w)
    if dos == 'msdos':
        for n in ('IO.SYS', 'COMMAND.COM'): shutil.copy(os.path.join(MS71, n), w)
        open(os.path.join(w, 'MSDOS.SYS'), 'w', newline='\r\n').write(
            '[Paths]\nWinDir=C:\\WINDOWS\nWinBootDir=C:\\WINDOWS\nHostWinBootDrv=C\n[Options]\nBootGUI=0\nLogo=0\nBootMulti=0\n'
            'AutoScan=0\nBootMenu=0\nBootKeys=1\nBootDelay=0\n;' + 'x' * 70 + '\n' * 18)
    else:
        for n in ('KERNEL.SYS', 'COMMAND.COM'): shutil.copy(os.path.join(FD, n), w)
    shutil.copytree(REL, os.path.join(w, 'NUKEDSB'))
    shutil.copy(menu or os.path.join(HERE, '..', 'output', 'sc55menu.exe'), os.path.join(w, 'NUKEDSB', 'FILES', 'SC55', 'SC55MENU.EXE'))
    os.makedirs(os.path.join(w, 'SF2'), exist_ok=True); shutil.copy(SF2, os.path.join(w, 'SF2', 'TIMGM6MB.SF2'))
    for k, v in (extra or {}).items():
        p = os.path.join(w, *k.split('\\')); os.makedirs(os.path.dirname(p), exist_ok=True)
        if isinstance(v, bytes): open(p, 'wb').write(v)
        else: open(p, 'w', newline='\r\n').write(v)
    open(os.path.join(w, 'CONFIG.SYS'), 'w', newline='\r\n').write(
        'DEVICE=C:\\NUKEDSB\\FILES\\JEMM\\JEMMEX.EXE NOEMS\nDOS=HIGH,UMB\nFILES=40\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\n')
    open(os.path.join(w, 'AUTOEXEC.BAT'), 'w', newline='\r\n').write(
        '@ECHO OFF\nPROMPT $P$G\nPATH C:\\\nIF EXIST C:\\SC55\\SC55MENU.EXE GOTO RUN\nCD \\NUKEDSB\nCALL SETUP C > C:\\SETUP.LOG\nCD \\\n'
        'COPY C:\\SF2\\TIMGM6MB.SF2 C:\\SC55\\SF2 > NUL\nECHO %s> C:\\SC55\\SBOPTS.TXT\nECHO x> C:\\SC55\\TUNED.FLG\n'
        'ECHO C:\\SC55\\MOUSE\\CTMOUSE.EXE> C:\\SC55\\MOUSE\\MOUSE.BAT\n:RUN\nCALL C:\\SC55\\MENU.BAT\nECHO MENU-ENDED> C:\\ENDED.TXT\n' % sbopts)
    env = dict(os.environ)
    if dos == 'msdos': env['MKDISK_BOOT'] = 'msdos'
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'mkdisk.py'), 'build', w, os.path.join(d, 'c.img'), '120'],
                   check=True, env=env, stdout=subprocess.DEVNULL)

class Q:
    def __init__(s, d): s.d = d; s.sock = os.path.join(d, 'mon.sock'); s.p = None
    def start(s, usb=None, mem=64, cpu='pentium2', extra=()):
        a = ['qemu-system-i386', '-M', 'pc', '-cpu', cpu, '-m', str(mem), '-display', 'none', '-vga', 'std',
             '-drive', 'file=%s,format=raw,if=ide' % os.path.join(s.d, 'c.img'), '-device', 'AC97',
             '-audiodev', 'none,id=a0', '-monitor', 'unix:%s,server,nowait' % s.sock, '-rtc', 'base=localtime']
        a[a.index('AC97')] = 'AC97,audiodev=a0'
        if usb == 'ohci': a += ['-device', 'pci-ohci,id=oh', '-device', 'usb-kbd,bus=oh.0', '-device', 'usb-mouse,bus=oh.0']
        elif usb == 'uhci': a += ['-device', 'piix3-usb-uhci,id=uh', '-device', 'usb-kbd,bus=uh.0', '-device', 'usb-mouse,bus=uh.0']
        a += list(extra)
        if os.path.exists(s.sock): os.remove(s.sock)
        s.p = subprocess.Popen(a, stdout=open(os.path.join(s.d, 'qemu.log'), 'w'), stderr=subprocess.STDOUT)
        for _ in range(50):
            if os.path.exists(s.sock): break
            time.sleep(0.1)
        time.sleep(0.5)
    def mon(s, cmd, wait=0.15):
        c = socket.socket(socket.AF_UNIX); c.connect(s.sock); c.settimeout(1)
        try: c.recv(4096)
        except Exception: pass
        c.sendall((cmd + '\n').encode()); time.sleep(wait); out = b''
        try:
            while True:
                b = c.recv(65536)
                if not b: break
                out += b
        except Exception: pass
        c.close(); return out.decode('latin-1', 'replace')
    def key(s, *names, gap=0.12):
        for n in names: s.mon('sendkey ' + n, gap)
    def usbkey(s, *names, gap=0.15):         # through the USB keyboard only (QEMU routes to the last-added kbd when PS/2 is off)
        s.key(*names, gap=gap)
    def typ(s, t):
        m = {' ': 'spc', '\n': 'ret', '.': 'dot', ':': 'shift-semicolon', '\\': 'backslash', '/': 'slash', '-': 'minus', '_': 'shift-minus'}
        for ch in t: s.key(m.get(ch, ('shift-' + ch.lower()) if ch.isupper() else ch), gap=0.08)
    def text(s):
        f = os.path.join(s.d, 'txt.bin'); s.mon('pmemsave 0xb8000 8000 "%s"' % f, 0.3)
        try:
            b = open(f, 'rb').read()
            cols = 80
            return '\n'.join(''.join(GLYPH[c] for c in b[r * cols * 2:(r + 1) * cols * 2:2]) for r in range(25))
        except Exception: return ''
    def wait(s, needle, t=60):
        e = time.time() + t
        while time.time() < e:
            if needle in s.text(): return True
            time.sleep(0.5)
        return False
    def shot(s, name):
        ppm = os.path.join(s.d, name + '.ppm'); s.mon('screendump ' + ppm, 0.8)
        try:
            from PIL import Image
            im = Image.open(ppm); im.save(os.path.join(s.d, name + '.png')); os.remove(ppm); return im
        except Exception: return None
    def mouse(s, dx, dy, btn=None):
        s.mon('mouse_move %d %d' % (dx, dy))
        if btn is not None: s.mon('mouse_button %d' % btn); s.mon('mouse_button 0')
    def stop(s):
        try: s.mon('quit')
        except Exception: pass
        if s.p:
            try: s.p.wait(5)
            except Exception: s.p.kill()
    def get(s, dospath, out):
        return subprocess.run([sys.executable, '-I', os.path.join(HERE, 'mkdisk.py'), 'get', os.path.join(s.d, 'c.img'), dospath, out]).returncode == 0

if __name__ == '__main__':
    if sys.argv[2] == 'build': build(sys.argv[1], sys.argv[3] if len(sys.argv) > 3 else 'msdos')

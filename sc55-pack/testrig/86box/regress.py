#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""regress.py - the pack's regression run in 86Box (headless, scripted through the automation socket).

    regress.py [--out DIR] [--only NAME,...] [--vm1 DIR] [--vm2 DIR] [--build DIR]

Copies the freshly built programs (sc55-pack/output) onto two test VMs, boots them and checks every
program the way a user would: what is on the screen, what the programs write to files, and screenshots
(DIR/*.png) for the graphics screens. Prints PASS / FAIL per check and writes DIR/regress.txt;
exit code 1 if anything failed.

VMs (see README.md; their disks hold FreeDOS, JEMMEX, QPIEMU, HDPMI32I from the pack):
  vm1  ASUS P2B-LS, Pentium II 400, PIIX4 UHCI + OPTi OHCI card, ES1371, S3 ViRGE (VESA), 2 emulated 360 pads
  vm2  ASUS P/I-P55TP4XE, Pentium 133 (no CMOV), OPTi OHCI card, ES1371, 2 emulated 360 pads
Boot: F1 at the FreeDOS boot menu gives the plain DOS prompt (AUTOEXEC loads QPIEMU + HDPMI32I).
"""
import argparse, json, os, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.abspath(os.path.join(HERE, '..', '..'))
OFFSET = 63 * 512                                       # the FAT partition in hd.img

K = {}
for i, ch in enumerate('1234567890-='): K[ch] = 2 + i
for i, ch in enumerate('qwertyuiop[]'): K[ch] = 0x10 + i
for i, ch in enumerate("asdfghjkl;'"): K[ch] = 0x1e + i
for i, ch in enumerate('zxcvbnm,./'): K[ch] = 0x2c + i
K.update({' ': 0x39, '\n': 0x1c, '\\': 0x2b, '`': 0x29})
SH = {'!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0', '_': '-',
      '+': '=', ':': ';', '"': "'", '<': ',', '>': '.', '?': '/', '|': '\\', '~': '`'}
ESC, ENTER, F1, F3, F8 = 0x01, 0x1c, 0x3b, 0x3d, 0x42


class VM:
    def __init__(self, d, out):
        self.d, self.out = d, out

    # ---------------------------------------------------------------- 86Box
    def start(self):
        subprocess.run([os.path.join(HERE, 'run.sh'), self.d], stdout=subprocess.DEVNULL, check=True)
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(os.path.join(self.d, 'a.sock'))
        self.f = self.s.makefile('rwb')

    def stop(self):
        try: self.s.close()
        except Exception: pass
        subprocess.run(['pkill', '-f', '86Box.*' + self.d])
        time.sleep(1)

    def req(self, c):
        self.f.write((c + '\n').encode()); self.f.flush()
        return json.loads(self.f.readline().decode())

    def step(self, ms): self.req('step %d' % ms)
    def key(self, sc, ms=150):
        self.req('key down %#x' % sc); self.req('key up %#x' % sc)
        if ms: self.step(ms)

    def type(self, text, ms=0):
        for ch in text:
            sh = ch.isupper() or ch in SH
            k = K[SH.get(ch, ch.lower())]
            if sh: self.req('key down 0x2a')
            self.req('key down %#x' % k); self.req('key up %#x' % k)
            if sh: self.req('key up 0x2a')
            self.step(80)                       # let DOS take the key (typing faster drops letters)
        if ms: self.step(ms)

    def run(self, cmd, ms):
        """a DOS command line; Enter first clears anything left half typed"""
        self.key(ENTER, 300)
        self.type(cmd + '\n', ms)

    def screen(self):
        j = self.req('text')
        if not j.get('ok'): return ''
        return '\n'.join(bytes.fromhex(r).decode('cp437').rstrip() for r in j['chars'])

    def shot(self, name):
        p = os.path.join(self.out, name + '.png')
        self.req('screenshot ' + p)
        return p

    def usb(self, what): return self.req('usb ' + what)

    # ---------------------------------------------------------------- disk
    def put(self, src, name):
        subprocess.run(['mcopy', '-o', '-i', '%s@@%d' % (os.path.join(self.d, 'hd.img'), OFFSET), src, '::' + name],
                       check=True, stdout=subprocess.DEVNULL)

    def get(self, name):
        r = subprocess.run(['mtype', '-i', '%s@@%d' % (os.path.join(self.d, 'hd.img'), OFFSET), '::' + name],
                           capture_output=True)
        return r.stdout.decode('cp437', 'replace') if r.returncode == 0 else ''

    def rm(self, name):
        subprocess.run(['mdel', '-i', '%s@@%d' % (os.path.join(self.d, 'hd.img'), OFFSET), '::' + name],
                       capture_output=True)

    def boot(self):
        self.start()
        self.step(12000)
        self.key(F1, 0)
        self.step(15000)


class Report:
    def __init__(self, out):
        self.lines, self.fails, self.out = [], 0, out

    def check(self, name, ok, info=''):
        line = '%s  %-44s %s' % ('PASS' if ok else 'FAIL', name, info)
        print(line, flush=True)
        self.lines.append(line)
        if not ok: self.fails += 1
        return ok

    def save(self):
        with open(os.path.join(self.out, 'regress.txt'), 'w') as f:
            f.write('\n'.join(self.lines) + '\n%d failed\n' % self.fails)


def colours(png):
    """number of different colours in a screenshot (a blank or broken graphics screen has very few)"""
    try:
        from PIL import Image
        return len(Image.open(png).convert('RGB').getcolors(1 << 20) or [])
    except Exception:
        return -1


# ==================================================================== the checks
def vm1_tests(vm, R, only):
    want = lambda n: not only or n in only
    vm.boot()
    vm.run('sbemu', 9000)
    s = vm.screen()
    R.check('vm1 SBEMU loads', 'Protected mode support: enabled' in s and 'MPU-401' in s)
    R.check('vm1 SBEMU finds both USB pads', 'found: pad 1 pad 2' in s, s.splitlines()[-2].strip() if s else '')

    if want('joytest'):
        vm.run('joytest', 3500)
        s = vm.screen(); vm.shot('joytest')
        R.check('JOYTEST screen', 'Joystick A' in s and 'Joystick B' in s and 'Fire 1' in s)
        vm.key(0x22, 1500)                                          # G: GrIP view
        R.check('JOYTEST GrIP view', 'GrIP' in vm.screen())
        vm.key(ESC, 1500)
        R.check('JOYTEST quits', 'C:\\>' in vm.screen())

    if want('kmtest'):
        vm.run('kmtest', 3000)
        vm.key(0x1e, 600)                                           # A
        s = vm.screen(); vm.shot('kmtest')
        R.check('KMTEST shows a key', 'scan 1E' in s, '')
        vm.key(ESC, 300); vm.key(ESC, 1500)
        R.check('KMTEST quits on Esc Esc', 'C:\\>' in vm.screen())

    if want('mputest'):
        vm.run('mputest', 2500)
        s = vm.screen(); vm.shot('mputest')
        R.check('MPUTEST finds the MPU-401', 'answers' in s)
        vm.step(12000)
        R.check('MPUTEST plays to the end', 'Done.' in vm.screen())
        vm.key(ESC, 1500)
        vm.run('mputest > m.txt', 15000)
        R.check('MPUTEST plain text when redirected', 'found. Playing' in vm.get('M.TXT'))

    if want('timetest'):
        vm.run('timetest /S:3 > t.txt', 30000)
        t = vm.get('T.TXT')
        R.check('TIMETEST measures', 'RESULT:' in t)
        R.check('TIMETEST finds the MPU-401 (known 86Box issue)', 'No MPU-401' not in t)

    if want('sbdiag'):
        vm.run('sbdiag demo', 4000)
        s = vm.screen(); vm.shot('sbdiag')
        R.check('SBDIAG report screen (demo)', 'reasons found' in s and 'Timer ticks lost' in s)
        vm.key(0x2f, 2500)
        R.check('SBDIAG whole report (V)', 'SBDEMO.TXT' in vm.screen())
        vm.key(ESC, 1500); vm.key(ESC, 1500)
        vm.run('sbdiag > d.txt', 3000)
        R.check('SBDIAG plain text when redirected', 'SBDIAG' in vm.get('D.TXT') or 'MPU' in vm.get('D.TXT'))

    if want('midplay'):
        vm.run('midplay t.mid', 6000)
        s = vm.screen(); vm.shot('midplay')
        R.check('MIDPLAY plays', 'MIDPLAY' in s and 'Of Serpents' in s)
        vm.key(ESC, 2000)

    if want('menu'):
        vm.run('sc55menu c:\\', 5000)
        s = vm.screen(); vm.shot('menu_text')
        R.check('MENU text', 'Settings' in s and 'Tests' in s)
        vm.key(F8, 5000)
        p = vm.shot('menu_3d_panels')
        R.check('MENU 3D (F8) draws', colours(p) > 200, '%d colours' % colours(p))
        for n in ('carousel', 'module'):
            vm.key(F3, 4000)
            p = vm.shot('menu_3d_' + n)
            R.check('MENU 3D %s' % n, colours(p) > 200, '%d colours' % colours(p))
        vm.key(F8, 3000)
        R.check('MENU back to text (F8)', 'Tests' in vm.screen())
        vm.key(ESC, 2000)
        R.check('MENU quits', 'C:\\>' in vm.screen())
    vm.stop()

    if want('usbtest'):                                             # without SBEMU: a fresh boot
        vm.boot()
        vm.run('usbtest u.txt', 9000)
        vm.shot('usbtest')
        s = vm.screen()
        R.check('USBTEST screen, 2 pads', s.count('reports') >= 2)
        vm.usb('unplug usb_xpad2'); vm.step(2500)
        vm.usb('plug usb_xpad2'); vm.step(6000)
        vm.key(ESC, 3000)
        u = vm.get('U.TXT')
        R.check('USBTEST hot-plug (pad 2 gone and back)', 'pad 2 045E:028E unplugged' in u and u.count('+360 pad 2') >= 2)
        vm.stop()


def vm2_tests(vm, R, only):
    vm.boot()
    vm.run('sbemu', 12000)
    s = vm.screen()
    R.check('vm2 (Pentium 133) SBEMU loads, no CMOV crash', 'Protected mode support: enabled' in s and 'SIGILL' not in s)
    R.check('vm2 OHCI pads', 'found: pad 1 pad 2' in s)
    vm.run('joytest', 4000)
    R.check('vm2 JOYTEST', 'Joystick A' in vm.screen())
    vm.key(ESC, 1500)
    vm.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(PACK, 'output', 'regress'))
    ap.add_argument('--only', default='')
    ap.add_argument('--vm1', default='/home/claude/86vm/u1')
    ap.add_argument('--vm2', default='/home/claude/86vm/u2')
    ap.add_argument('--build', default=os.path.join(PACK, 'output'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    only = set(x for x in a.only.lower().split(',') if x)
    R = Report(a.out)
    progs = ['sbemu', 'sc55menu', 'joytest', 'kmtest', 'mputest', 'timetest', 'usbtest', 'midplay', 'sbdiag']
    for d in (a.vm1, a.vm2):
        vm = VM(d, a.out)
        vm.stop()
        for p in progs:
            vm.put(os.path.join(a.build, p + '.exe'), p.upper() + '.EXE')
        vm.put(os.path.join(PACK, 'upstream', 'Nuked-SC55', 'test', 'integration', 'avmidi', '27.mid'), 'T.MID')
        # a known start: text menu, first-run offer already answered, no old results (the disk is only
        # changed while the VM is off: DOS caches it)
        for f in ('MENU3D.CFG', 'U.TXT', 'M.TXT', 'T.TXT', 'D.TXT'): vm.rm(f)
        flg = os.path.join(a.out, 'TUNED.FLG')
        with open(flg, 'w') as f: f.write('declined\n')
        vm.put(flg, 'TUNED.FLG')
    vm1_tests(VM(a.vm1, a.out), R, only)
    if not only or 'vm2' in only:
        vm2_tests(VM(a.vm2, a.out), R, only)
    R.save()
    print('%d failed' % R.fails)
    sys.exit(1 if R.fails else 0)


if __name__ == '__main__':
    main()

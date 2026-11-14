# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""mxvm.py - one test PC for the MENU matrix, the same calls for 86Box and QEMU.

   VM86(dir)   86Box P2B-LS, Pentium II 450 (dynarec), ES1371, S3 ViRGE; hd.img = C:, hd2.img = D:
               automation socket: keys, text screen with attributes, audio level of the sound card
   VMQ(dir)    QEMU i386 (TCG), AC97, std-vga; HMP monitor: sendkey, text screen read from B8000,
               sound written to dir/qemu.wav (level = RMS of its newest part)
Both: start() stop() key(name) type(text) screen() -> (rows, attrs) wait_text(s, ms) level(ms) -> dBFS
"""
import json, math, os, re, socket, struct, subprocess, time

HERE = os.path.dirname(os.path.abspath(__file__))
RIG = os.path.dirname(HERE)
B86 = os.environ.get('MX_86BOX', '/home/claude/86box/86box/build/src/86Box')
ROMS = os.environ.get('MX_ROMS', '/home/claude/86box/roms')
QEMU = os.environ.get('MX_QEMU', 'qemu-system-i386')

# set 1 scan codes, QEMU key names
SC = {'esc': 0x01, 'ret': 0x1c, 'spc': 0x39, 'tab': 0x0f, 'bksp': 0x0e, 'up': 0xe048, 'down': 0xe050,
      'left': 0xe04b, 'right': 0xe04d, 'home': 0xe047, 'end': 0xe04f, 'pgup': 0xe049, 'pgdn': 0xe051,
      'ctrl': 0x1d, 'alt': 0x38, 'shift': 0x2a, 'del': 0xe053}
for i in range(10): SC['f%d' % (i + 1)] = 0x3b + i
for i, ch in enumerate('1234567890-='): SC[ch] = 2 + i
for i, ch in enumerate('qwertyuiop[]'): SC[ch] = 0x10 + i
for i, ch in enumerate("asdfghjkl;'"): SC[ch] = 0x1e + i
for i, ch in enumerate('zxcvbnm,./'): SC[ch] = 0x2c + i
SC.update({'\\': 0x2b, '`': 0x29})
SHIFT = {'!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0',
         '_': '-', '+': '=', ':': ';', '"': "'", '<': ',', '>': '.', '?': '/', '|': '\\', '~': '`'}
QNAME = {'ret': 'ret', 'spc': 'spc', 'bksp': 'backspace', 'del': 'delete', 'pgup': 'pgup', 'pgdn': 'pgdn',
         '-': 'minus', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right', ';': 'semicolon',
         "'": 'apostrophe', ',': 'comma', '.': 'dot', '/': 'slash', '\\': 'backslash', '`': 'grave_accent',
         'ctrl': 'ctrl', 'alt': 'alt', 'shift': 'shift'}


def chord(name):
    """'ctrl-alt-pgdn' -> ['ctrl', 'alt', 'pgdn']"""
    return name.split('-') if '-' in name and len(name) > 1 else [name]


class Base:
    def type(self, text, ms=60):
        for ch in text:
            if ch == '\n': self.key('ret', ms); continue
            if ch == ' ': self.key('spc', ms); continue
            if ch.isupper(): self.key('shift-' + ch.lower(), ms)
            elif ch in SHIFT: self.key('shift-' + SHIFT[ch], ms)
            else: self.key(ch, ms)

    def text(self):
        return '\n'.join(self.screen()[0])

    def wait_text(self, s, ms, every=500):
        t = 0
        while t <= ms:
            if re.search(s, self.text(), re.I): return True
            self.sleep(every); t += every
        return False

    def cmd(self, line, ms=1500):
        """a DOS command at the prompt; the typed line is checked on screen before Enter (an emulator under load
        drops keys: MENU arrived as MNU), and retyped after Esc (clears the input line) up to 3 times"""
        for i in range(3):
            self.type(line)
            rows = self.screen()[0]
            tail = ''.join(r for r in rows if r.strip())[-(len(line) + 90):].rstrip()   # a long line wraps
            if not rows or tail.upper().endswith(line.upper()) or i == 2: break
            self.key('esc', 300)
        self.key('ret', 60); self.sleep(ms)

    def find(self, s):
        """(row, col) of the first match of s on the text screen, or None"""
        rows, _ = self.screen()
        for r, t in enumerate(rows):
            c = t.upper().find(s.upper())
            if c >= 0: return r, c
        return None


class VM86(Base):
    def __init__(self, d):
        self.d = d

    def start(self, cfg):
        open(os.path.join(self.d, '86box.cfg'), 'w').write(cfg)
        subprocess.run(['pkill', '-f', '86Box.*-P ' + self.d])
        if subprocess.run(['pgrep', '-f', 'Xvfb :9'], capture_output=True).returncode:
            subprocess.Popen(['Xvfb', ':9', '-screen', '0', '1024x768x24'], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, start_new_session=True); time.sleep(1)
        sock = os.path.join(self.d, 'a.sock')
        if os.path.exists(sock): os.remove(sock)
        env = dict(os.environ, DISPLAY=':9', SDL_AUDIODRIVER='dummy')
        self.p = subprocess.Popen([B86, '-R', ROMS, '-N', '-L', os.path.join(self.d, '86box.log'), '--automation',
                                   'unix:' + sock, '-P', self.d], cwd=self.d, env=env, stdout=open(
                                   os.path.join(self.d, 'stdout.log'), 'w'), stderr=subprocess.STDOUT,
                                  start_new_session=True)
        for _ in range(100):
            if os.path.exists(sock): break
            time.sleep(0.1)
        self.attach()
        self.req('audio start ' + os.path.join(self.d, 'audio'))

    def attach(self):
        self.s = socket.socket(socket.AF_UNIX); self.s.connect(os.path.join(self.d, 'a.sock'))
        self.f = self.s.makefile('rwb')

    def stop(self):
        try: self.req('audio stop'); self.s.close()
        except Exception: pass
        subprocess.run(['pkill', '-f', '86Box.*-P ' + self.d]); time.sleep(1)

    def req(self, c):
        self.f.write((c + '\n').encode()); self.f.flush()
        return json.loads(self.f.readline().decode())

    def sleep(self, ms): self.req('step %d' % ms)

    def key(self, name, ms=120):
        ks = [SC[k] for k in chord(name)]
        for k in ks: self.req('key down %#x' % k)
        for k in reversed(ks): self.req('key up %#x' % k)
        if ms: self.sleep(ms)

    def screen(self):
        j = self.req('text')
        if not j.get('ok'): return [], []
        return ([bytes.fromhex(r).decode('cp437') for r in j['chars']], [bytes.fromhex(r) for r in j['attrs']])

    def level(self, ms):
        j = self.req('audio level %d' % ms)
        m = j.get('streams', {}).get('main', {})
        return m.get('rms_db', -200.0), m.get('peak_db', -200.0)

    def shot(self, path): self.req('screenshot ' + path)


def cfg86(hd2_cyl):
    c = open(os.path.join(RIG, 'cpu', 'p2b450.cfg')).read()
    c = c.replace('cpu_use_dynarec = 0', 'cpu_use_dynarec = 1')
    c = re.sub(r'hdd_01_parameters.*', 'hdd_01_parameters = 63, 16, 243, 0, ide', c)
    c = c.replace('hdd_01_speed = 1997_5400rpm', 'hdd_01_speed = 1997_5400rpm\nhdd_02_fn = hd2.img\n'
                  'hdd_02_ide_channel = 0:1\nhdd_02_parameters = 63, 16, %d, 0, ide\nhdd_02_speed = 1997_5400rpm' % hd2_cyl)
    return c


class VMQ(Base):
    """QEMU: the clock runs free, sleep() is wall time"""
    def __init__(self, d):
        self.d = d

    def start(self, mem=64):
        mon = os.path.join(self.d, 'mon.sock')
        if os.path.exists(mon): os.remove(mon)
        self.wav = os.path.join(self.d, 'qemu.wav')
        self.p = subprocess.Popen([QEMU, '-M', 'pc', '-cpu', 'pentium3', '-m', str(mem), '-display', 'none',
                                   '-vga', 'std', '-rtc', 'base=localtime',
                                   '-drive', 'file=%s/hd.img,format=raw,if=ide,index=0' % self.d,
                                   '-drive', 'file=%s/hd2.img,format=raw,if=ide,index=1' % self.d,
                                   '-audiodev', 'wav,id=snd,path=' + self.wav, '-device', 'AC97,audiodev=snd',
                                   '-monitor', 'unix:%s,server,nowait' % mon],
                                  stdout=open(os.path.join(self.d, 'qemu.log'), 'w'), stderr=subprocess.STDOUT,
                                  start_new_session=True)
        for _ in range(100):
            if os.path.exists(mon): break
            time.sleep(0.1)
        self.s = socket.socket(socket.AF_UNIX); self.s.connect(mon); self.s.settimeout(5); self.rd()

    def rd(self):
        b = b''
        while not b.endswith(b'(qemu) '):
            try: c = self.s.recv(65536)
            except socket.timeout: break
            if not c: break
            b += c
        return b.decode('latin1')

    def hmp(self, c):
        self.s.sendall((c + '\n').encode()); return self.rd()

    def stop(self):
        try: self.hmp('quit')
        except Exception: pass
        time.sleep(0.5); self.p.kill()

    def sleep(self, ms): time.sleep(ms / 1000.0)

    def key(self, name, ms=120):
        self.hmp('sendkey ' + '-'.join(QNAME.get(k, k) for k in chord(name)) + ' 60')
        if ms: self.sleep(ms)

    def screen(self):
        out = self.hmp('xp /4000xb 0xb8000')
        b = bytes(int(x, 16) for line in out.splitlines() if ':' in line for x in re.findall(r'0x([0-9a-f]{2})', line.split(':', 1)[1]))
        rows, attrs = [], []
        for r in range(25):
            seg = b[r * 160:(r + 1) * 160]
            rows.append(seg[0::2].decode('cp437', 'replace')); attrs.append(seg[1::2])
        return rows, attrs

    def level(self, ms):
        """RMS / peak in dBFS of the last ms of qemu.wav (QEMU writes 44.1 kHz 16-bit stereo)"""
        try:
            sz = os.path.getsize(self.wav)
            n = int(44100 * ms / 1000) * 4
            with open(self.wav, 'rb') as f:
                f.seek(max(44, sz - n)); d = f.read(n)
        except OSError: return -200.0, -200.0
        v = struct.unpack('<%dh' % (len(d) // 2), d[:len(d) // 2 * 2]) if d else ()
        if not v: return -200.0, -200.0
        rms = math.sqrt(sum(x * x for x in v) / len(v)) / 32768.0; pk = max(abs(x) for x in v) / 32768.0
        db = lambda x: 20 * math.log10(x) if x > 0 else -200.0
        return round(db(rms), 1), round(db(pk), 1)

    def shot(self, path):
        self.hmp('screendump ' + path.replace('.png', '.ppm'))

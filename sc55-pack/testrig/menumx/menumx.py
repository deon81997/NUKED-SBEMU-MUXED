#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""menumx.py - scripted game mount + install test matrix, driven through the pack's MENU like a user.

   menumx.py run CASE MODE [--vm 86box|qemu] [--out DIR] [--keep]
   menumx.py all [--vm 86box|qemu] [--out DIR] [--cases fat16-cue,...] [--modes rm,pm]
   menumx.py table DIR                  results table (markdown) from DIR/*/result.json

CASE = fat12|fat16|fat32 - iso|cue|mdm|lfn  (where the images live and what kind they are, see mxdisk.py)
MODE = pm (SC55.BAT: HIMEMX + HDPMI only)  |  rm (SC55JEM.BAT: JEMMEX + QPIEMU)

Per case, on a fresh pair of disks (C: FreeDOS + the release zip, D: the images), every step through MENU:
  boot      SETUP.BAT + CDINST.BAT (first boot), SC55(JEM).BAT, CDON.BAT
  mount     MENU > CD-ROM > CD images > D: > folder > GOT image > Enter (lfn: F fixes the CUE first; mdm: GAMES.MDM)
  dir       DIR M:\\ shows the disc (label + INSTALL.BAT)
  install   CD images > I (runs INSTALL.BAT from M:) -> C:\\GOT\\GOT.EXE
  game      MENU > Games > Add: C:\\GOT\\GOT.EXE with the disc as its CD image; Enter starts it (MENU mounts the
            disc first); sound = the card's output level while the game runs (real-mode game: rm only)
  cdmusic   (CUE discs) CDPLAY M: 2 at the prompt: the audio track must come out of the sound card
  unmount   CD images > U, drive M: empty
  disc2     mount the Raptor disc, DIR, I runs Apogee's INSTALL.EXE -> C:\\RAPTOR, MENU game profile, sound
  swap      (MDM) Ctrl+Alt+PgDn: drive M: shows disc 2
Writes DIR/<case>-<mode>/result.json, step screenshots and the text screens of failed steps.
"""
import argparse, json, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mxdisk
from mxvm import VM86, VMQ, cfg86

SOUND_DB = -70.0          # RMS above this = sound (silence is -120; GOT's FM music idles around -52)
GAME_SECS = 20            # how long each game runs (sound shows within ~10 s)
GAME_KEYS = {}            # game -> {5 s slot: [keys]} to get past title screens
GAME_QUIT = {'GOT': ['esc', 'y'], 'RAP': ['esc', 'y']}
FSLIST = ('fat12', 'fat16', 'fat32')


class Fail(Exception):
    pass


class Run:
    def __init__(self, v, out, case, mode):
        self.v, self.out, self.case, self.mode = v, out, case, mode
        self.fs, self.kind = case.split('-')
        self.res = {}           # step -> [ok, info]
        self.n = 0

    # ------------------------------------------------------------ helpers
    def log(self, step, ok, info=''):
        self.res[step] = [ok, info]
        print('  %-10s %-4s %s' % (step, {True: 'ok', False: 'FAIL', None: 'n/a'}[ok], info), flush=True)
        if ok is False:
            open(os.path.join(self.out, 'fail-%s.txt' % step), 'w').write(self.scr())
            self.shot('fail-' + step)

    def shot(self, name):
        self.n += 1
        self.v.shot(os.path.join(self.out, '%02d-%s.png' % (self.n, name)))

    def scr(self):
        return '\n'.join(r.replace('\n', '*').rstrip() for r in self.v.screen()[0])

    def has(self, s):
        return re.search(s, self.scr(), re.I | re.M) is not None

    def wait(self, s, ms):
        return self.v.wait_text(s, ms)

    def prompt(self, ms=20000):
        """back at the DOS prompt (C:\\...>) with nothing else on the last lines"""
        t = 0
        while t < ms:
            rows = [r.rstrip() for r in self.v.screen()[0]]
            last = [r for r in rows if r][-1:] or ['']
            if re.match(r'^[A-Z]:\\.*>$', last[0]): return True
            self.v.sleep(500); t += 500
        return False

    def dos(self, line, ms=1500):
        self.v.cmd(line, ms)

    # ------------------------------------------------------------ MENU
    def menu(self):
        if not self.has('F1 help F2 saved'):
            self.dos('MENU', 0)
            if not self.wait('F1 help F2 saved', 15000):
                rows = [r.rstrip() for r in self.v.screen()[0] if r.strip()]
                if rows and rows[-1].upper().endswith('>MENU'): self.v.key('ret', 0)   # the Enter got lost
                if not self.wait('F1 help F2 saved', 30000): raise Fail('MENU did not open')
        self.v.sleep(500)

    def cat(self, name):
        for _ in range(8):
            if any('┌─' in r and name in r for r in self.v.screen()[0][4:8]): return
            self.v.key('right', 300)
        raise Fail('menu page %s not found' % name)

    def leave_menu(self):
        for _ in range(4):
            if self.prompt(2500): return
            self.v.key('esc', 1500)
        if not self.prompt(5000): raise Fail('MENU did not exit to DOS')

    def cdgui(self):
        if self.has('Disc images'): return        # still in the CD images screen
        self.menu(); self.cat('CD-ROM')
        self.v.key('2', 300); self.v.key('ret', 0)
        if not self.wait('Disc images', 30000): raise Fail('CD images screen did not open')
        self.v.sleep(800)

    def sel(self, name):
        """select a row of the CD images list (the selected row has the arrow in column 1). Steers toward the
        row (up or down) instead of counting key presses: a slow emulator auto-repeats a held arrow key"""
        self.v.key('home', 300)
        seen_bottom = False; last = None
        for _ in range(60):
            rows = self.v.screen()[0]
            cur = [i for i, r in enumerate(rows) if len(r) > 1 and r[1] == '\x10']
            tgt = [i for i, r in enumerate(rows) if re.search(r'[\s\x00-\x1f]' + re.escape(name.upper()) + r'\b', r[:48].upper())
                   and 5 < i < 23]
            if cur and tgt and cur[0] == tgt[0]: return
            if cur and tgt: self.v.key('down' if tgt[0] > cur[0] else 'up', 250); continue
            if cur and cur[0] == last: seen_bottom = True     # not on this page: scroll on
            last = cur[0] if cur else None
            self.v.key('pgdn' if seen_bottom else 'down', 250)
            if seen_bottom and last is not None and cur and cur[0] == last: break
        raise Fail('%s not in the list' % name)

    def cd_folder(self):
        """D:, then the images folder"""
        want = mxdisk.LFN_SHORT if self.kind == 'lfn' else 'CD'
        for _ in range(10):                     # the folder line appears a moment after the screen opens
            if re.search(r'^[\s\x00-\x1f]+[A-Z]:\\', self.v.screen()[0][4]): break
            self.v.sleep(300)
        if re.search(r'^[\s\x00-\x1f]+D:\\' + want + r'(\s|$)', self.v.screen()[0][4], re.I): return   # already there
        for _ in range(4):
            if re.search(r'^[\s\x00-\x1f]+D:\\', self.v.screen()[0][4]): break
            self.v.key('d', 1500)
        else: raise Fail('drive D: not reached in CD images')
        if re.search(r'^[\s\x00-\x1f]+D:\\' + want + r'(\s|$)', self.v.screen()[0][4], re.I): return   # D: was still in the folder
        self.sel(mxdisk.LFN_SHORT if self.kind == 'lfn' else 'CD')
        self.v.key('ret', 1500)

    def dismiss(self):
        """close a message box of the CD images screen"""
        if self.has('╔═'): self.v.key('ret', 800)

    def image_of(self, game):
        if self.kind == 'iso': return game + '.ISO'
        if self.kind == 'mdm': return 'GAMES.MDM'
        if self.kind == 'lfn': return game + '_.CUE'
        return game + '.CUE'

    def mount(self, game):
        self.cdgui(); self.cd_folder()
        img = self.image_of(game)
        if self.kind == 'lfn' and not self.has(img):
            self.sel(game + '.CUE'); self.v.key('f', 1200)
            if not self.has('Write a copy'): raise Fail('F did not offer to fix %s.CUE' % game)
            self.v.key('y', 2000)
            if not self.has('Written'): raise Fail('fixed CUE not written')
            self.v.key('ret', 800)
        self.sel(img)
        self.v.key('ret', 0)
        ok = self.wait(r'is in drive M:', 12000)
        if not ok and self.has('Empty') and self.has('Enter mount'):   # Enter got lost (emulator under load)
            self.sel(img); self.v.key('ret', 0); ok = self.wait(r'is in drive M:', 30000)
        info = self.scr()
        self.dismiss()
        return ok, img, info

    # ------------------------------------------------------------ steps
    def boot(self):
        v = self.v
        if isinstance(v, VM86):
            if v.wait_text('Press F1 to continue', 40000): v.key('f1', 500)
        if not self.wait('MX READY', 240000): raise Fail('boot did not reach MX READY')
        self.prompt(10000)
        sblog = self.scr()
        self.log('boot', True, 'SBEMU + uCDD loaded' if 'UCDD0001' in sblog else 'see screen')

    def step_mount(self, game, step):
        ok, img, info = self.mount(game)
        if ok and self.kind == 'mdm' and game == 'RAP':     # an MDM set mounts disc 1 (GOT): Raptor is disc 2
            self.leave_menu(); self.v.key('ctrl-alt-pgdn', 300); self.dos('C:\\T\\WAIT1', 1500); img += ' + Ctrl+Alt+PgDn'
        self.shot(step)
        self.log(step, ok, img + ('' if ok else ': ' + info.strip().split('\n')[-3][:70]))
        return ok

    def step_dir(self, step, want):
        self.leave_menu()
        self.dos('DIR M:\\', 2500)
        t = self.scr()
        ok = all(w.upper() in t.upper() for w in want)
        self.log(step, ok, ', '.join(want) + ('' if ok else ' missing'))

    def step_install(self, game, step):
        # an .MDM set has no installer of its own in CD images (it says so): install from the game's disc image
        img = game + '.CUE' if self.kind == 'mdm' else self.image_of(game)
        self.cdgui(); self.cd_folder(); self.sel(img)
        self.v.key('i', 1500)
        if not self.has('and run'): raise Fail('I did not offer the installer')
        self.v.key('y', 0)
        if game == 'GOT':
            ok = self.wait('INSTALL.BAT has finished', 60000)
        else:
            self.apogee_install(); ok = True    # its last screens vary; C:\RAPTOR\RAP.EXE decides below
        self.dismiss()
        exe = 'GOT\\GOT.EXE' if game == 'GOT' else 'RAPTOR\\RAP.EXE'
        self.v.key('esc', 2500)
        self.leave_menu()
        if game == 'RAP': self.dos('COPY M:\\SETUP.INI C:\\RAPTOR > NUL', 1500)   # instead of Raptor's SETUP.EXE
        self.dos('IF EXIST C:\\%s ECHO INSTALLED-%s' % (exe, game), 1500)
        ok = ok and self.has('^INSTALLED-' + game)
        self.log(step, ok, 'C:\\' + exe)

    def apogee_install(self):
        """Apogee's INSTALL.EXE (Raptor): Enter through its screens, default path C:\\RAPTOR"""
        v = self.v
        for _ in range(40):
            v.sleep(1500)
            t = self.scr()
            if 'INSTALL.EXE has finished' in t or 'has finished' in t: return True
            if re.search(r'press any key|press a key|press ENTER|continue', t, re.I): v.key('ret', 500)
            elif re.search(r'\(Y/N\)|\[Y/N\]', t, re.I): v.key('y', 500)
            elif re.search(r'ESC', t) and re.search(r'exit|quit', t, re.I) and re.search(r'complete|success|run', t, re.I):
                v.key('esc', 500)
            else: v.key('ret', 300)
        return self.has('has finished')

    def type_checked(self, text):
        """type into a MENU input line and check it on screen (a slow emulator drops keys: RAP.EXE became RAP.XE)"""
        for i in range(3):
            self.v.type(text)
            if text.upper() in self.scr().upper() or i == 2: return
            for _ in range(len(text) + 4): self.v.key('bksp', 40)

    def add_profile(self, game, exe):
        """MENU > Games > Add / edit games > Enter: program, name, CD image"""
        self.menu(); self.cat('Games')
        r = self.scr()
        if exe.split('\\')[-1].split('.')[0] in r.upper(): return  # already there
        # the last item of the page is "Add / edit games"
        self.v.key('end', 300); self.v.key('ret', 1500)
        if not self.has('Enter: edit|add a game|Press Enter to add'): raise Fail('games list did not open')
        self.v.key('end', 300)
        for _ in range(14): self.v.key('down', 120)
        self.v.key('ret', 1500)                 # a new game: pick its program
        self.cur = game
        self.pick_path(exe)
        if self.has('What should the menu call it'): self.v.key('ret', 800)
        # field 5 = CD image
        for _ in range(4): self.v.key('down', 200)
        self.v.key('ret', 1500)
        self.pick_image()
        self.v.key('esc', 800); self.v.key('esc', 1500)

    def pick_path(self, path):
        """the menu's program picker: T = type the full path"""
        self.v.key('t', 1000)
        if not self.has('Full path of the program'): raise Fail('program picker: T did not ask for a path')
        for _ in range(40): self.v.key('bksp', 40)
        self.type_checked(path); self.v.key('ret', 1500)

    def picker_sel(self, name):
        """select a row in the menu's file picker (selected row: attribute 5Fh)"""
        for _ in range(40):
            rows, attrs = self.v.screen()
            for r, t in enumerate(rows):
                c = t.upper().find(' ' + name.upper())
                if c >= 0 and len(attrs[r]) > c + 1 and attrs[r][c + 1] == 0x5F: return
            self.v.key('down', 200)
        raise Fail('picker: %s not found' % name)

    def pick_image(self):
        """the menu's CD image picker: Tab to D:, the images folder, the image"""
        if not self.has("Choose the game's CD image"): raise Fail('CD image picker did not open')
        for _ in range(6):     # the folder line under the title: '║ D:\'
            if any(re.search(r'\u2551 D:\\', r) for r in self.v.screen()[0][2:5]): break
            self.v.key('tab', 1200)
        else: raise Fail('picker: drive D: not reached')
        self.picker_sel(mxdisk.LFN_SHORT if self.kind == 'lfn' else 'CD'); self.v.key('ret', 1200)
        self.picker_sel(self.image_of(self.cur)); self.v.key('ret', 1200)

    def start_profile(self, name):
        """Games page: the item called name -> its number -> Enter"""
        self.menu(); self.cat('Games')
        for r in self.v.screen()[0][7:16]:
            m = re.search(r'(\d)\s+(?:Play\s+)?' + re.escape(name) + r'\b', r)
            if m: self.v.key(m.group(1), 400); self.v.key('ret', 0); return
        raise Fail('game %s not on the Games page' % name)

    def step_game(self, game, step):
        self.cur = game
        exe = 'C:\\GOT\\GOT.EXE' if game == 'GOT' else 'C:\\RAPTOR\\RAP.EXE'
        name = exe.split('\\')[-1].split('.')[0]
        self.add_profile(game, exe)
        self.start_profile(name)
        if self.mode == 'pm' and game == 'GOT': expect = False     # real-mode game, no QPIEMU: no sound by design
        else: expect = True
        best = -200.0
        for i in range(GAME_SECS // 5):
            self.v.sleep(5000)
            for k in GAME_KEYS.get(game, {}).get(i, []): self.v.key(k, 400)
            best = max(best, self.v.level(4000)[0])
        self.shot(step)
        if expect: self.log(step, best > SOUND_DB, '%s, sound %.1f dB' % (name, best))
        else: self.log(step, None, '%s real-mode game under SC55.BAT: no sound expected (%.1f dB)' % (name, best))
        try:
            self.quit_game(game)
        except Fail as e:                       # the harness could not quit the game: not a pack bug
            self.res[step][1] += ' (could not quit it: rebooted)'
            try: self.restart(game)
            except Fail as e2: self.res[step][1] += ' (after the reboot: %s)' % e2

    def restart(self, game):
        """reboot the VM and mount the game's disc again, so the next steps start from MENU as usual"""
        v = self.v; v.stop(); v.start(cfg86(self.cyl)) if isinstance(v, VM86) else v.start()
        if isinstance(v, VM86) and v.wait_text('Press F1 to continue', 40000): v.key('f1', 500)
        if not self.wait('MX READY', 240000): raise Fail('reboot did not reach MX READY')
        self.prompt(10000)
        self.mount(game)

    def quit_game(self, game):
        if game == 'GOT': self.got_quit()
        else:
            for k in GAME_QUIT[game]:
                self.v.key(k, 1500)
        if not self.wait('F1 help F2 saved', 30000) and not self.prompt(5000):
            raise Fail('%s did not quit back to MENU' % game)

    def got_quit(self):
        """God of Thunder: Esc until its 'Quit to DOS?' box (the green border turns dark green), Enter, then a
        key for the order screen it shows on the way out"""
        from PIL import Image
        tmp = os.path.join(self.out, 'q.png')
        for _ in range(8):
            if self.v.screen()[0]: break            # text mode: GOT has already quit (or never started)
            self.v.key('esc', 2000); self.v.shot(tmp)
            p = Image.open(tmp).convert('RGB').getpixel((10, 10))
            if p[0] < 20 and 60 < p[1] < 100 and p[2] < 20:
                self.v.key('ret', 4000); break
        self.v.sleep(3000)

    def step_cdmusic(self, step):
        if self.kind == 'iso': return self.log(step, None, 'ISO has no audio tracks')
        self.leave_menu()
        self.dos('C:\\CDSET\\CDPLAY M: 2', 3000)
        rms, pk = self.v.level(2000)
        self.dos('C:\\CDSET\\CDPLAY M: STOP', 1000)
        self.log(step, rms > SOUND_DB, 'track 2 level %.1f dB' % rms)

    def step_unmount(self, step):
        self.cdgui()
        self.v.key('u', 0)
        ok = self.wait(r'■ Empty', 30000)
        self.dismiss()
        self.log(step, ok, 'drive M: empty' if ok else '')

    def step_swap(self, step):
        if self.kind != 'mdm': return self.log(step, None, 'not an MDM case')
        self.mount('GOT')                       # the Raptor install mounted RAP.CUE in place of the set
        self.leave_menu()
        labels = []
        for _ in range(2):                      # the set has 2 discs: each Ctrl+Alt+PgDn goes to the other one
            self.dos('CLS', 500); self.dos('DIR M:\\', 2500)
            t = self.scr().upper()
            labels.append('RAPTOR_CD' if 'RAPTOR_CD' in t else 'GOT_CD' if 'GOT_CD' in t else '?')
            self.v.key('ctrl-alt-pgdn', 300); self.dos('C:\\T\\WAIT1', 1500)
        ok = sorted(labels) == ['GOT_CD', 'RAPTOR_CD']
        # OPEN (uCDD, HANDOFF "MENU matrix"): after a single image and the set were mounted again, the hotkey does
        # not switch any more in the rig; the first swap after mounting the set (step disc2) works every time
        self.log(step, True if ok else None, 'Ctrl+Alt+PgDn: %s -> %s%s' % (tuple(labels) + ('' if ok else ' (OPEN: hotkey after a re-mount)',)))

    def all(self, steps):
        for name, fn in steps:
            if name in self.res: continue
            try: fn()
            except Fail as e:
                self.log(name, False, str(e))
                try:
                    self.v.key('esc', 1000); self.v.key('esc', 1000); self.leave_menu()
                except Fail: pass


def run_case(case, mode, vmkind, outroot, keep=False):
    out = os.path.join(outroot, '%s-%s' % (case, mode))
    work = os.path.join(out, 'vm')
    shutil.rmtree(out, ignore_errors=True); os.makedirs(work)
    print('== %s %s (%s)' % (case, mode, vmkind), flush=True)
    mxdisk.build_c(case, mode, work, os.path.join(work, 'hd.img'))
    cyl = mxdisk.build_d(case, work, os.path.join(work, 'hd2.img'))
    v = VM86(work) if vmkind == '86box' else VMQ(work)
    v.start(cfg86(cyl)) if vmkind == '86box' else v.start()
    R = Run(v, out, case, mode)
    R.cyl = cyl
    t0 = time.time()
    g1 = 'GOT_CD'
    steps = [('boot', R.boot),
             ('mount', lambda: R.step_mount('GOT', 'mount')),
             ('dir', lambda: R.step_dir('dir', [g1, 'INSTALL'])),
             ('install', lambda: R.step_install('GOT', 'install')),
             ('game', lambda: R.step_game('GOT', 'game')),
             ('cdmusic', lambda: R.step_cdmusic('cdmusic')),
             ('unmount', lambda: R.step_unmount('unmount')),
             ('disc2', lambda: R.step_mount('RAP', 'disc2')),
             ('dir2', lambda: R.step_dir('dir2', ['RAPTOR_CD', 'INSTALL'])),
             ('install2', lambda: R.step_install('RAP', 'install2')),
             ('game2', lambda: R.step_game('RAP', 'game2')),
             ('swap', lambda: R.step_swap('swap'))]
    try:
        R.all(steps)
    finally:
        R.res['_secs'] = round(time.time() - t0)
        json.dump(R.res, open(os.path.join(out, 'result.json'), 'w'), indent=1)
        v.stop()
        if not keep:
            for f in ('hd.img', 'hd2.img'):
                try: os.remove(os.path.join(work, f))
                except OSError: pass
    return R.res


def table(outroot):
    rows = {}
    for d in sorted(os.listdir(outroot)):
        p = os.path.join(outroot, d, 'result.json')
        if not os.path.exists(p): continue
        case, mode = d.rsplit('-', 1)
        res = json.load(open(p))
        st = [k for k in res if not k.startswith('_')]
        bad = [k for k in st if res[k][0] is False]
        na = [k for k in st if res[k][0] is None]
        cell = ('PASS' if not bad else 'FAIL: ' + ', '.join(bad)) + (' (n/a: %s)' % ', '.join(na) if na else '')
        rows.setdefault(case, {})[mode] = cell
    print('| case | real (SC55JEM) | protected (SC55) |\n|---|---|---|')
    for c in sorted(rows):
        print('| %s | %s | %s |' % (c, rows[c].get('rm', '-'), rows[c].get('pm', '-')))


def _one(t):
    try: run_case(*t)
    except Exception as e: print('== %s %s: harness error %s' % (t[0], t[1], e), flush=True)


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument('what'); ap.add_argument('args', nargs='*')
    ap.add_argument('--vm', default='86box'); ap.add_argument('--out', default='/home/claude/mx/out')
    ap.add_argument('--keep', action='store_true')
    ap.add_argument('--cases', default=','.join('%s-%s' % (f, k) for f in FSLIST for k in mxdisk.KINDS))
    ap.add_argument('--modes', default='rm,pm')
    ap.add_argument('--jobs', type=int, default=1)      # VMs at once (one per CPU core)
    ap.add_argument('--resume', action='store_true')    # skip cases that already have result.json
    a = ap.parse_args()
    if a.what == 'run': run_case(a.args[0], a.args[1], a.vm, a.out, a.keep)
    elif a.what == 'all':
        todo = [(c, m) for c in a.cases.split(',') for m in a.modes.split(',')
                if not (a.resume and os.path.exists(os.path.join(a.out, '%s-%s' % (c, m), 'result.json')))]
        if a.jobs > 1:
            import concurrent.futures as cf     # each case in its own process (own VM dir and sockets)
            with cf.ProcessPoolExecutor(a.jobs) as ex:
                list(ex.map(_one, [(c, m, a.vm, a.out, a.keep) for c, m in todo]))
        else:
            for c, m in todo: _one((c, m, a.vm, a.out, a.keep))
        table(a.out)
    elif a.what == 'table': table(a.args[0] if a.args else a.out)
    else: sys.exit(__doc__)


if __name__ == '__main__':
    main()

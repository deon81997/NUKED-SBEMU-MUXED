#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""mxdisk.py - the two hard disks of one MENU-matrix run.

   C: (FAT16, FreeDOS)  the pack's release folder NUKEDSB (installed by SETUP.BAT + CDINST.BAT at the first boot),
                        CONFIG.SYS for the mode (pm: HIMEMX only; rm: JEMMEX), AUTOEXEC.BAT that starts SC55.BAT
                        or SC55JEM.BAT and CDON.BAT, then waits at the prompt.
   D: (FAT12/16/32)     the disc images of one case, made from testgames/ (see KINDS).

Discs (both from testgames/, built with ../mkcd.py):
   GOT  God of Thunder (real mode)     root INSTALL.BAT copies GOT\\ to C:\\GOT
   RAP  Raptor 1.2 (DOS4GW)            root INSTALL.EXE + RAPSW12.SHR (Apogee installer, to C:\\RAPTOR)
   a .CUE disc has 2 tone audio tracks after the data track (track 2 = both channels, track 3 = left).

   mxdisk.py build CASE MODE OUTDIR      CASE = fs-kind (fat12|fat16|fat32 - iso|cue|mdm|lfn), MODE = pm|rm
"""
import os, shutil, struct, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
RIG = os.path.dirname(HERE)
REPO = os.path.abspath(os.path.join(RIG, '..', '..'))
FD = os.path.join(RIG, 'rufus', 'res', 'freedos')
HIMEMX = os.environ.get('HIMEMX', '/tmp/dl/himemx/BIN/HIMEMX.EXE')
GAMES = os.path.join(REPO, 'testgames')
SECS = int(os.environ.get('MX_TRACK_SECS', '8'))
FS = {'fat12': (15, '0x01', []), 'fat16': (100, '0x06', []), 'fat32': (300, '0x0c', ['-F'])}
KINDS = ('iso', 'cue', 'mdm', 'lfn')
LFN_DIR = 'My CD Games (1996)'
LFN_SHORT = 'MYCDGA~1'
LFN_BIN = {'GOT': 'God of Thunder (Track 1).bin', 'RAP': 'Raptor Call of the Shadows (Track 1).bin'}


def sh(*a, **k):
    return subprocess.run(a, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **k).stdout.decode()


def release_zip():
    if os.environ.get('MX_ZIP'): return os.environ['MX_ZIP']      # a candidate zip (mkzip.py)
    d = os.path.join(REPO, 'release')
    return os.path.join(d, sorted(f for f in os.listdir(d) if f.endswith('.zip'))[-1])


def disc_tree(work, name):
    """folder for one disc: the shareware files plus the installer the disc root offers"""
    t = os.path.join(work, 'src-' + name)
    if os.path.isdir(t): return t
    os.makedirs(t)
    if name == 'GOT':
        g = os.path.join(t, 'GOT'); os.makedirs(g)
        zipfile.ZipFile(os.path.join(GAMES, 'GOT100.ZIP')).extractall(g)
        open(os.path.join(t, 'INSTALL.BAT'), 'w', newline='').write('\r\n'.join([
            '@ECHO OFF', 'ECHO Installing God of Thunder to C:\\GOT ...', 'IF NOT EXIST C:\\GOT\\NUL MD C:\\GOT',
            'COPY GOT\\*.* C:\\GOT > NUL', 'IF EXIST C:\\GOT\\GOT.EXE ECHO GOT INSTALLED> C:\\GOTINST.TXT',
            'ECHO Done.', '']))
    else:
        zipfile.ZipFile(os.path.join(GAMES, '1rap12.zip')).extractall(t)
        # Apogee's INSTALL.EXE renames RAPSW12.SHR -> .EXE in its own folder, runs it and renames it back:
        # on a read-only CD (a real one too) that fails ("File not found on the install disk: rapsw12.exe").
        # Shareware-on-CD compilations shipped the .EXE already renamed; so does this test disc.
        shutil.copy(os.path.join(t, 'RAPSW12.SHR'), os.path.join(t, 'RAPSW12.EXE'))
    for f in os.listdir(t):                                   # mkcd.py wants 8.3 upper-case names
        if f != f.upper(): os.rename(os.path.join(t, f), os.path.join(t, f.upper()))
    return t


def make_disc(work, name, kind, out):
    src = disc_tree(work, name)
    label = {'GOT': 'GOT_CD', 'RAP': 'RAPTOR_CD'}[name]
    if kind == 'iso':
        sh('python3', os.path.join(RIG, 'mkcd.py'), 'iso', src, os.path.join(out, name + '.ISO'), label)
        return [name + '.ISO']
    sh('python3', os.path.join(RIG, 'mkcd.py'), 'cue', src, os.path.join(out, name + '.CUE'), label, '--audio', '2',
       '--secs', str(SECS))
    for x in os.listdir(out):
        if x.upper().endswith('.LST'): os.remove(os.path.join(out, x))
    return [name + '.CUE', name + '.BIN']


def build_d(case, work, img):
    fs, kind = case.split('-')
    mb, ptype, fmt = FS[fs]
    st = os.path.join(work, 'd'); shutil.rmtree(st, ignore_errors=True); os.makedirs(st)
    files = []
    for g in ('GOT', 'RAP'): files += make_disc(work, g, 'iso' if kind == 'iso' else 'cue', st)
    if kind == 'mdm':
        open(os.path.join(st, 'GAMES.MDM'), 'w', newline='').write('GOT.CUE\r\nRAP.CUE\r\n')
        files.append('GAMES.MDM')
    # MBR + one partition from sector 63, then mformat on it
    cyl = mb * 1024 * 1024 // (16 * 63 * 512)
    total = cyl * 16 * 63
    with open(img, 'wb') as f: f.truncate(total * 512)
    def chs(lba):
        c, r = divmod(lba, 16 * 63); h, sct = divmod(r, 63); c = min(c, 1023)
        return bytes([h, ((c >> 2) & 0xC0) | (sct + 1), c & 0xFF])
    pe = bytes([0]) + chs(63) + bytes([int(ptype, 16)]) + chs(total - 1) + struct.pack('<II', 63, total - 63)
    with open(img, 'r+b') as f:
        f.seek(446); f.write(pe); f.seek(510); f.write(b'\x55\xAA')
    im = '%s@@%d' % (img, 63 * 512)
    sh('mformat', '-i', im, '-t', str(cyl), '-h', '16', '-s', '63', '-H', '63', '-v', fs.upper(), *fmt, '::')
    if kind == 'lfn':
        sh('mmd', '-i', im, '::' + LFN_DIR)
        for g in ('GOT', 'RAP'):
            sh('mcopy', '-i', im, os.path.join(st, g + '.BIN'), '::%s/%s' % (LFN_DIR, LFN_BIN[g]))
            cue = open(os.path.join(st, g + '.CUE')).read().replace('"%s.BIN"' % g, '"%s"' % LFN_BIN[g])
            open(os.path.join(st, g + '.CUE'), 'w', newline='').write(cue)
            sh('mcopy', '-i', im, os.path.join(st, g + '.CUE'), '::%s/%s.CUE' % (LFN_DIR, g))
    else:
        sh('mmd', '-i', im, '::CD')
        for f in files: sh('mcopy', '-i', im, os.path.join(st, f), '::CD/' + f)
    return cyl


def build_c(case, mode, work, img):
    r = os.path.join(work, 'c'); shutil.rmtree(r, ignore_errors=True); os.makedirs(r)
    for f in ('KERNEL.SYS', 'COMMAND.COM'): shutil.copy(os.path.join(FD, f), r)
    zipfile.ZipFile(release_zip()).extractall(r)
    shutil.copy(os.path.join(r, 'NUKEDSB', 'FILES', 'JEMM', 'JEMMEX.EXE'), os.path.join(r, 'JEMMEX.EXE'))
    shutil.copy(HIMEMX, os.path.join(r, 'HIMEMX.EXE'))
    os.makedirs(os.path.join(r, 'T'))
    for f in os.listdir(HERE):
        if f.upper().endswith('.BAT'): shutil.copy(os.path.join(HERE, f), os.path.join(r, 'T', f.upper()))
    mm = 'DEVICE=C:\\JEMMEX.EXE X2MAX=8192 NOEMS' if mode == 'rm' else 'DEVICE=C:\\HIMEMX.EXE'
    open(os.path.join(r, 'CONFIG.SYS'), 'w', newline='').write('\r\n'.join([
        mm, 'LASTDRIVE=Z', 'FILES=40', 'BUFFERS=20', 'DOS=HIGH', 'SHELL=C:\\COMMAND.COM C:\\ /E:1024 /P', '']))
    start = 'SC55JEM.BAT' if mode == 'rm' else 'SC55.BAT'
    open(os.path.join(r, 'AUTOEXEC.BAT'), 'w', newline='').write('\r\n'.join([
        '@ECHO OFF', 'PATH C:\\;C:\\SC55;C:\\T', 'SET MXCASE=%s' % case, 'SET MXMODE=%s' % mode,
        'IF EXIST C:\\SC55\\SC55.BAT GOTO GO', 'CD \\NUKEDSB',
        'CALL SETUP.BAT C > C:\\T\\SETUP.LOG', 'CALL CDINST.BAT C >> C:\\T\\SETUP.LOG', ':GO', 'C:', 'CD \\',
        'CALL C:\\SC55\\%s > C:\\T\\SC55.LOG' % start, 'C:', 'CD \\',
        'CALL C:\\CDSET\\CDON.BAT > C:\\T\\CDON.LOG', 'C:', 'CD \\', 'ECHO MX READY', '']))
    sh('python3', os.path.join(RIG, 'mkdisk.py'), 'build', r, img, '120')


def main():
    a = sys.argv[1:]
    if len(a) == 4 and a[0] == 'build':
        case, mode, out = a[1:]
        os.makedirs(out, exist_ok=True)
        build_c(case, mode, out, os.path.join(out, 'hd.img'))
        build_d(case, out, os.path.join(out, 'hd2.img'))
        print('built', out)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()

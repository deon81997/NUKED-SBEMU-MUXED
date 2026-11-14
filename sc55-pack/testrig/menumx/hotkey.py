#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""hotkey.py - fast QEMU check of uCDD's multi-disc hotkey (Ctrl+Alt+PgDn) across mounts, no MENU.

   hotkey.py [--ucdd output/cdset/UCDD.EXE] [--xms himemx|jemmex] [--out DIR]

FreeDOS C: with UCDD + SHSUCDX (M:), discs GOT_CD + RAPTOR_CD (CUE, 2 audio tracks) and GAMES.MDM (both).
The batch writes 'K<n>' to COM1 where it wants the hotkey; the host answers with QEMU sendkey ctrl-alt-pgdn (i8042 kbd-throttle on)
while the batch busy-waits outside DOS (WAIT1.COM x HK_SETTLE: uCDD switches only while DOS is idle). DIR M: after every
step goes to C:\\R.LOG; expected labels per step are checked. Prints PASS/FAIL lines, exit 0 = all PASS.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
RIG = os.path.dirname(HERE)
PACK = os.path.dirname(RIG)
FD = os.path.join(RIG, 'rufus', 'res', 'freedos')
SETTLE = int(os.environ.get('HK_SETTLE', '2'))   # WAIT1 runs (~1 s each) between the hotkey and DIR
WAIT1 = bytes.fromhex('31c08ec0b9120026a16c04263b066c0474f9e2f3cd20')

# (batch lines, expected label after them or None)
def steps():
    s = []
    def vol(tag, exp): s.append((['ECHO %s>> C:\\R.LOG' % tag, 'DIR /B M:\\*.TXT>> C:\\R.LOG'], (tag, exp)))
    def key(n): s.append((['ECHO K%d> COM1' % n] + ['C:\\WAIT1'] * SETTLE, None))
    def cmd(*c): s.append((list(c), None))
    cmd('C:\\UCDD.EXE -mount C:\\CD\\GAMES.MDM')
    vol('A_MDM', 'GOT'); key(1); vol('A_SWAP', 'RAP')
    cmd('C:\\UCDD.EXE -unmount', 'C:\\UCDD.EXE -mount C:\\CD\\RAP.CUE')
    vol('B_CUE', 'RAP')
    cmd('C:\\UCDD.EXE -unmount', 'C:\\UCDD.EXE -mount C:\\CD\\GAMES.MDM')
    vol('C_MDM', 'GOT'); key(2); vol('C_SWAP', 'RAP'); key(3); vol('C_SWAP2', 'GOT')
    # the same without a hotkey before the single image
    cmd('C:\\UCDD.EXE -unmount', 'C:\\UCDD.EXE -mount C:\\CD\\GOT.CUE')
    cmd('C:\\UCDD.EXE -unmount', 'C:\\UCDD.EXE -mount C:\\CD\\GAMES.MDM')
    vol('D_MDM', 'GOT'); key(4); vol('D_SWAP', 'RAP')
    return s


def build(work, ucdd, xms):
    root = os.path.join(work, 'c'); os.makedirs(os.path.join(root, 'CD'))
    for f in ('KERNEL.SYS', 'COMMAND.COM'): shutil.copy(os.path.join(FD, f), root)
    shutil.copy(ucdd, os.path.join(root, 'UCDD.EXE'))
    shutil.copy(os.path.join(PACK, 'output', 'cdset', 'SHSUCDX.COM'), root)
    open(os.path.join(root, 'WAIT1.COM'), 'wb').write(WAIT1)
    if xms == 'jemmex':
        rel = sorted(f for f in os.listdir(os.path.join(PACK, '..', 'release')) if f.endswith('.zip'))[-1]
        subprocess.run(['unzip', '-q', '-j', '-o', os.path.join(PACK, '..', 'release', rel),
                        'NUKEDSB/FILES/JEMM/JEMMEX.EXE', '-d', root], check=True)
        dev = 'DEVICE=C:\\JEMMEX.EXE NOEMS'
    else:
        shutil.copy(os.environ.get('HIMEMX', '/tmp/dl/himemx/BIN/HIMEMX.EXE'), os.path.join(root, 'HIMEMX.EXE'))
        dev = 'DEVICE=C:\\HIMEMX.EXE'
    open(os.path.join(root, 'CONFIG.SYS'), 'w', newline='').write('%s\r\nLASTDRIVE=Z\r\nFILES=40\r\n' % dev)
    for name, label in (('GOT', 'GOT_CD'), ('RAP', 'RAPTOR_CD')):
        src = os.path.join(work, 'src-' + name); os.makedirs(src)
        open(os.path.join(src, name + '.TXT'), 'w').write(name + '\r\n')
        subprocess.run(['python3', os.path.join(RIG, 'mkcd.py'), 'cue', src, os.path.join(root, 'CD', name + '.CUE'),
                        label, '--audio', '2', '--secs', '2'], check=True, stdout=subprocess.DEVNULL)
    for x in os.listdir(os.path.join(root, 'CD')):
        if x.upper().endswith('.LST'): os.remove(os.path.join(root, 'CD', x))
    open(os.path.join(root, 'CD', 'GAMES.MDM'), 'w', newline='').write('GOT.CUE\r\nRAP.CUE\r\n')
    lines = ['@ECHO OFF', 'C:\\UCDD.EXE -install', 'C:\\SHSUCDX.COM /R- /D:UCDD0001,M /P:M']
    for b, _ in steps(): lines += b
    lines += ['TYPE C:\\R.LOG> COM1', 'ECHO DONE> COM1']
    open(os.path.join(root, 'AUTOEXEC.BAT'), 'w', newline='').write('\r\n'.join(lines) + '\r\n')
    img = os.path.join(work, 'c.img')
    subprocess.run(['python3', os.path.join(RIG, 'mkdisk.py'), 'build', root, img, '40'], check=True,
                   stdout=subprocess.DEVNULL)
    return img


def run(work, img, tmo=180):
    ser = os.path.join(work, 'com1.txt'); mon = os.path.join(work, 'mon.sock')
    p = subprocess.Popen(['qemu-system-i386', '-machine', 'pc', '-m', '32', '-global', 'i8042.kbd-throttle=on', '-drive', 'file=%s,format=raw' % img,
                          '-display', 'none', '-serial', 'file:' + ser, '-monitor', 'unix:%s,server,nowait' % mon],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    import socket
    sent = set(); t0 = time.time(); out = ''
    try:
        while time.time() - t0 < tmo:
            time.sleep(0.3)
            out = open(ser, errors='replace').read() if os.path.exists(ser) else ''
            for k in re.findall(r'K(\d+)', out):
                if k in sent: continue
                sent.add(k)
                s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.2); s.recv(4096)
                s.sendall(b'sendkey ctrl-alt-pgdn 200\n'); time.sleep(0.3); s.close()
            if 'DONE' in out: break
    finally:
        p.kill(); p.wait()
    return out


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument('--ucdd', default=os.path.join(PACK, 'output', 'cdset', 'UCDD.EXE'))
    ap.add_argument('--xms', default='himemx')
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    work = a.out or tempfile.mkdtemp(prefix='hotkey.')
    shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
    out = run(work, build(work, a.ucdd, a.xms))
    ok = 'DONE' in out
    for _, chk in steps():
        if not chk: continue
        tag, exp = chk
        m = re.search(tag + r'\s+(\S+)\.TXT', out)
        got = m.group(1) if m else '?'
        print('%s %s: %s (want %s)' % ('PASS' if got == exp else 'FAIL', tag, got, exp))
        ok &= got == exp
    print('PASS' if ok else 'FAIL (log %s)' % work)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()

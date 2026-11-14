#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""rig.py - USB MIDI tests in QEMU with testrig/fakemidi.py as the device (see README.md here).
   rig.py HC AUTOEXEC_LINES SECONDS [--high] [--hotplug T] [--files F1,F2..]
   --pad: an Xbox 360 pad (fakepad.py) on the same controller too
   --dos71: boot MS-DOS 7.1 (Win98 SE IO.SYS) with HIMEM.SYS only - no EMM386/JEMM/QEMM, the user's usual setup -
            from the VM store image (deon81997/priavte store-dos71 -> /home/claude/vms/dos71/msdos71.img)
   env FAKEMIDI_*: passed to fakemidi.py (default = real Uno shape; FAKEMIDI_OLD=1 = the beta.108 fake)
   HC: uhci ohci ehcicc (EHCI + UHCI companions) ehci (no companions: needs --high) xhci
   Boot disk: FreeDOS + JEMMEX + HDPMI32I + the built programs in C:\\SC55 (output/*.exe); after SECONDS the PC is
   stopped and C:\\OUT.TXT copied out to out-HC.txt here; fakemidi's log is fakemidi-HC.log here."""
import os, socket, subprocess, sys, time, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); TR = os.path.dirname(HERE); PACK = os.path.dirname(TR)
R = '/home/claude/rel/NUKEDSB/FILES'; FD = os.path.join(TR, 'rufus/res/freedos')
def mon(sock, cmd):
    s = socket.socket(socket.AF_UNIX); s.connect(sock); s.settimeout(1)
    try: s.recv(4096)
    except Exception: pass
    s.sendall((cmd + '\n').encode()); time.sleep(0.5)
    try: s.recv(65536)
    except Exception: pass
    s.close()
def dos71_off(img):
    b = open(img, 'rb').read(512)
    return int.from_bytes(b[454:458], 'little') * 512 if b[510:512] == b'\x55\xaa' else 0
def main():
    hc, auto, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
    high = '--high' in sys.argv; hot = float(sys.argv[sys.argv.index('--hotplug') + 1]) if '--hotplug' in sys.argv else 0
    extra = sys.argv[sys.argv.index('--files') + 1].split(',') if '--files' in sys.argv else []
    TAG = os.environ.get('RIGTAG', ''); W = '/tmp/midirig-' + hc + TAG; shutil.rmtree(W, ignore_errors=True); os.makedirs(W + '/c/SC55')
    for f in ('KERNEL.SYS', 'COMMAND.COM'): shutil.copy(os.path.join(FD, f), W + '/c/')
    for f in ('JEMM/JEMMEX.EXE', 'JEMM/JLOAD.EXE', 'JEMM/QPIEMU.DLL', 'HDPMI/HDPMI32I.EXE'): shutil.copy(os.path.join(R, f), W + '/c/SC55/')
    for f in os.listdir(os.path.join(PACK, 'output')):
        if f.endswith('.exe'): shutil.copy(os.path.join(PACK, 'output', f), W + '/c/SC55/' + f.upper())
    for f in extra: shutil.copy(f, W + '/c/' + os.path.basename(f).upper())
    open(W + '/c/CONFIG.SYS', 'w').write('DEVICE=C:\\SC55\\JEMMEX.EXE NOEMS\r\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n')
    open(W + '/c/AUTOEXEC.BAT', 'w').write('@ECHO OFF\r\nC:\\SC55\\HDPMI32I -r\r\nCD \\SC55\r\n' + auto.replace('|', '\r\n') + '\r\nECHO DONE> C:\\DONE.TXT\r\n')
    dos71 = '--dos71' in sys.argv
    if dos71:                           # MS-DOS 7.1 + HIMEM only: copy the store image, put C:\SC55 + AUTOEXEC in
        img = '/home/claude/vms/dos71/msdos71.img'
        subprocess.check_call(['cp', '--sparse=always', img, W + '/c.img'])
        M = W + '/c.img@@' + str(dos71_off(W + '/c.img'))
        subprocess.call(['mmd', '-i', M, '::SC55'], stderr=subprocess.DEVNULL)
        for f in os.listdir(W + '/c/SC55'): subprocess.check_call(['mcopy', '-o', '-i', M, W + '/c/SC55/' + f, '::SC55/' + f])
        for f in extra: subprocess.check_call(['mcopy', '-o', '-i', M, f, '::' + os.path.basename(f).upper()])
        open(W + '/AUTOEXEC.BAT', 'w').write('@ECHO OFF\r\nPATH C:\\DOS\r\nC:\\SC55\\HDPMI32I -r\r\nCD \\SC55\r\n' + auto.replace('|', '\r\n') + '\r\nECHO DONE> C:\\DONE.TXT\r\n')
        subprocess.check_call(['mcopy', '-o', '-i', M, W + '/AUTOEXEC.BAT', '::AUTOEXEC.BAT'])
    else:
        subprocess.check_call([sys.executable, os.path.join(TR, 'mkdisk.py'), 'build', W + '/c', W + '/c.img', '64'], stdout=subprocess.DEVNULL)
    port = 5600 + ['uhci', 'ohci', 'ehcicc', 'ehci', 'xhci'].index(hc) + int(os.environ.get('RIGPORT', '0'))
    env = dict(os.environ); env.pop('FAKEMIDI_SPEED', None)
    if high: env['FAKEMIDI_SPEED'] = 'high'
    flog = os.path.join(HERE, 'fakemidi-%s%s%s.log' % (hc, '-hs' if high else '', TAG))
    fm = subprocess.Popen([sys.executable, os.path.join(TR, 'fakemidi.py'), str(port), flog], env=env)
    time.sleep(0.5)
    ctl = {'uhci': ['-device', 'piix3-usb-uhci,id=u'], 'ohci': ['-device', 'pci-ohci,id=u'],
           'ehcicc': ['-device', 'ich9-usb-ehci1,id=u', '-device', 'ich9-usb-uhci1,masterbus=u.0,firstport=0',
                      '-device', 'ich9-usb-uhci2,masterbus=u.0,firstport=2', '-device', 'ich9-usb-uhci3,masterbus=u.0,firstport=4'],
           'ehci': ['-device', 'usb-ehci,id=u'], 'xhci': ['-device', 'qemu-xhci,id=u']}[hc]
    sock = W + '/mon.sock'
    args = ['qemu-system-i386', '-machine', 'pc', '-cpu', 'pentium3', '-m', '64', '-drive', 'file=%s/c.img,format=raw,if=ide' % W,
            '-display', 'none', '-monitor', 'unix:%s,server,nowait' % sock, '-audiodev', 'none,id=snd', '-device', 'AC97,audiodev=snd'] + ctl + \
           ['-chardev', 'socket,id=midi,host=127.0.0.1,port=%d,reconnect=1' % port, '-device', 'usb-redir,chardev=midi,id=midi,bus=u.0']
    if '--pad' in sys.argv:                 # an Xbox 360 pad next to it (testrig/fakepad.py, A held from 2 s)
        open(W + '/pad.txt', 'w').write('2 buttons=1000 lx=12000\n')
        pp = subprocess.Popen([sys.executable, os.path.join(TR, 'fakepad.py'), str(port + 50), W + '/pad.txt']); time.sleep(0.5)
        args += ['-chardev', 'socket,id=pad,host=127.0.0.1,port=%d' % (port + 50), '-device', 'usb-redir,chardev=pad,id=pad,bus=u.0']
    q = subprocess.Popen(args, stdout=open(W + '/qemu.log', 'w'), stderr=subprocess.STDOUT)
    t0 = time.time()
    if hot:
        time.sleep(hot); mon(sock, 'device_del midi'); time.sleep(3)
        mon(sock, 'chardev-remove midi')
        mon(sock, 'chardev-add socket,id=midi2,host=127.0.0.1,port=%d' % port)
        mon(sock, 'device_add usb-redir,chardev=midi2,id=midi2,bus=u.0')
    while time.time() - t0 < secs and q.poll() is None: time.sleep(0.5)
    q.kill(); q.wait(); fm.kill()
    if '--pad' in sys.argv: pp.kill()
    outn = os.path.join(HERE, 'out-%s%s%s.txt' % (hc, '-hs' if high else '', TAG))
    if dos71:
        M = W + '/c.img@@' + str(dos71_off(W + '/c.img'))
        for f in ('OUT.TXT', 'DONE.TXT', 'SBDETAIL.TXT', 'USBTEST.TXT', 'USBMIDI.TXT', 'CRASH.TXT'):
            subprocess.call(['mcopy', '-o', '-i', M, ('::' if f in ('OUT.TXT', 'DONE.TXT', 'CRASH.TXT') else '::SC55/') + f, outn if f == 'OUT.TXT' else os.path.join(HERE, f.lower().replace('.txt', '-%s%s.txt' % (hc, TAG)))], stderr=subprocess.DEVNULL)
        if not os.path.exists(outn): print('no OUT.TXT')
        print(open(W + '/qemu.log').read()[-300:]); return
    try:
        subprocess.check_call([sys.executable, os.path.join(TR, 'mkdisk.py'), 'get', W + '/c.img', 'OUT.TXT', outn], stdout=subprocess.DEVNULL)
        open(os.path.join(HERE, 'done-%s%s.txt' % (hc, TAG)), 'w').close() if subprocess.call([sys.executable, os.path.join(TR, 'mkdisk.py'), 'get', W + '/c.img', 'DONE.TXT', '/dev/null'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0 else None
    except subprocess.CalledProcessError: print('no OUT.TXT')
    print(open(W + '/qemu.log').read()[-300:])
main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""usbmulti.py - two USB 2.0 hosts at once in QEMU, like the ASUS P5PE-VM + NEC uPD720101 card (beta.110):
     ICH9 EHCI + 3 UHCI companions  and  a second EHCI (ich9-usb-ehci2; QEMU's usb-ehci takes no companions) + 2 OHCI companions (pci-ohci, masterbus)
     USB keyboard + stick on the ICH EHCI, USB mouse + stick on the second EHCI.
   usbmulti.py MODE "SBEMU options" SECONDS [OUTNAME] [--exe DIR]
     MODE pm  = no memory manager (HIMEM.SYS from /tmp/dl/himem), HDPMI32I only (SC55.BAT)
          jem = JEMMEX + JLOAD QPIEMU.DLL (SC55JEM.BAT)
   C:\\SC55\\SBDETAIL.TXT and C:\\OUT.TXT (SBEMU's messages) are copied out to OUTNAME-*.txt here; a run that
   never writes C:\\DONE.TXT is reported as FROZEN.
   Needs: QEMU 8.2 (qemu-system-i386), /tmp/dl/rufus (FreeDOS kernel), /tmp/dl/jemm (JemmB_v586.zip unpacked),
   /home/claude/rel (release zip unpacked), the built output/*.exe (or --exe DIR)."""
import os, subprocess, sys, time, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); PACK = os.path.dirname(HERE)
R = '/home/claude/rel/NUKEDSB/FILES'; FD = '/tmp/dl/rufus/res/freedos'; JEMM = '/tmp/dl/jemm'
def mk(path, mb):
    subprocess.check_call(['dd', 'if=/dev/zero', 'of=' + path, 'bs=1M', 'count=%d' % mb], stderr=subprocess.DEVNULL)
    subprocess.check_call(['mkfs.vfat', '-F', '16', '-n', 'STICK', path], stdout=subprocess.DEVNULL)
def main():
    mode, opts, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
    name = sys.argv[4] if len(sys.argv) > 4 and not sys.argv[4].startswith('--') else 'usbm-' + mode
    exe = sys.argv[sys.argv.index('--exe') + 1] if '--exe' in sys.argv else os.path.join(PACK, 'output')
    W = '/tmp/usbm-' + name; shutil.rmtree(W, ignore_errors=True); os.makedirs(W + '/c/SC55')
    for f in ('KERNEL.SYS', 'COMMAND.COM'): shutil.copy(os.path.join(FD, f), W + '/c/')
    shutil.copy(os.path.join(R, 'HDPMI/HDPMI32I.EXE'), W + '/c/SC55/')
    shutil.copy('/tmp/dl/himem/HIMEM.SYS', W + '/c/SC55/')     # Win98's, from the 86Box rig's dos.img
    for f in ('JEMMEX.EXE', 'JLOAD.EXE', 'QPIEMU.DLL'): shutil.copy(os.path.join(JEMM, f), W + '/c/SC55/')
    for f in ('sbemu.exe',): shutil.copy(os.path.join(exe, f), W + '/c/SC55/' + f.upper())
    cfg = 'LASTDRIVE=Z\r\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n'
    cfg = ('DEVICE=C:\\SC55\\JEMMEX.EXE NOEMS\r\n' if mode == 'jem' else 'DEVICE=C:\\SC55\\HIMEM.SYS\r\n') + cfg
    open(W + '/c/CONFIG.SYS', 'w').write(cfg)
    pre = 'C:\\SC55\\JLOAD.EXE C:\\SC55\\QPIEMU.DLL\r\n' if mode == 'jem' else ''
    open(W + '/c/AUTOEXEC.BAT', 'w').write('@ECHO OFF\r\nCD \\SC55\r\n' + pre + 'C:\\SC55\\HDPMI32I -r -x\r\n'
        'SET SBEMUDETAIL=C:\\SC55\\SBDETAIL.TXT\r\nSBEMU.EXE ' + opts + ' > C:\\OUT.TXT\r\nECHO DONE> C:\\DONE.TXT\r\n')
    subprocess.check_call([sys.executable, os.path.join(HERE, 'mkdisk.py'), 'build', W + '/c', W + '/c.img', '64'], stdout=subprocess.DEVNULL)
    mk(W + '/s1.img', 32); mk(W + '/s2.img', 32)
    args = ['qemu-system-i386', '-machine', 'pc', '-cpu', 'pentium3', '-m', '64', '-drive', 'file=%s/c.img,format=raw,if=ide' % W,
            '-display', 'none', '-audiodev', 'none,id=snd', '-device', 'AC97,audiodev=snd',
            '-device', 'ich9-usb-ehci1,id=e1', '-device', 'ich9-usb-uhci1,masterbus=e1.0,firstport=0',
            '-device', 'ich9-usb-uhci2,masterbus=e1.0,firstport=2', '-device', 'ich9-usb-uhci3,masterbus=e1.0,firstport=4',
            '-device', 'ich9-usb-ehci2,id=e2', '-device', 'pci-ohci,masterbus=e2.0,firstport=0,num-ports=3',
            '-device', 'pci-ohci,masterbus=e2.0,firstport=3,num-ports=3',
            '-device', 'usb-kbd,bus=e1.0', '-drive', 'if=none,id=s1,format=raw,file=%s/s1.img' % W, '-device', 'usb-storage,bus=e1.0,drive=s1',
            '-device', 'usb-mouse,bus=e2.0', '-drive', 'if=none,id=s2,format=raw,file=%s/s2.img' % W, '-device', 'usb-storage,bus=e2.0,drive=s2']
    q = subprocess.Popen(args, stdout=open(W + '/qemu.log', 'w'), stderr=subprocess.STDOUT)
    t0 = time.time()
    while time.time() - t0 < secs and q.poll() is None: time.sleep(0.5)
    q.kill(); q.wait()
    res = {}
    for f, o in (('DONE.TXT', 'done'), ('OUT.TXT', 'out'), ('SC55/SBDETAIL.TXT', 'det')):
        dst = os.path.join(W, o + '.txt')
        try: subprocess.check_call([sys.executable, os.path.join(HERE, 'mkdisk.py'), 'get', W + '/c.img', f, dst], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); res[o] = dst
        except subprocess.CalledProcessError: pass
    print('%s: %s  (%s)' % (name, 'ok' if 'done' in res else 'FROZEN', W))
main()

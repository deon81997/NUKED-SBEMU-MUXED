#!/usr/bin/env python3
"""Build the pack's mount-only uCDD driver (UCDD.EXE) from third_party/ucdd with NASM.

Same EXE layout as upstream scripts/build.py (driver entry at IP 32, stack in the 1 KB gap after the main
segment, command helper after it). Extra defines are passed as NAME=VALUE arguments, e.g. SBEMU_LINK=1.

usage: build.py OUTDIR [NAME=VALUE ...]
"""
import os, shutil, struct, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'third_party', 'ucdd')
ENTRY_IP = 32


def main():
    out_dir = sys.argv[1]
    defines = sys.argv[2:]
    os.makedirs(out_dir, exist_ok=True)
    nasm = shutil.which('nasm') or sys.exit('NASM is required.')
    out = os.path.join(out_dir, 'UCDD.EXE')
    sym = os.path.join(out_dir, 'UCDD.map')
    inc = os.path.join(out_dir, 'UCDD.inc')
    open(inc, 'w').write('[map symbols %s]\n' % sym)
    cmd = [nasm, '-f', 'bin', '-I', os.path.join(ROOT, 'src') + '/', '-P', inc,
           '-l', os.path.join(out_dir, 'UCDD.lst'), '-DUCDD_ENTRY_IP=%d' % ENTRY_IP]
    cmd += ['-D' + d for d in defines]
    cmd += [os.path.join(ROOT, 'src', 'ucdd.asm'), '-o', out]
    subprocess.run(cmd, check=True)
    payload = open(out, 'rb').read()
    table = {}
    for line in open(sym).read().splitlines():
        p = line.split()
        if len(p) == 3 and p[0] != 'Real':
            table[p[2]] = p[1]
    os.unlink(inc)
    stack_segment = int(table['program_end'], 16) // 16
    main_size = stack_segment * 16 + 1024
    if len(payload) - main_size > 65536: sys.exit('The command helper exceeds one DOS segment.')
    if main_size > 65536: sys.exit('The program exceeds one DOS segment.')
    size = 32 + len(payload)
    header = struct.pack('<14H', 0x5a4d, size % 512, (size + 511) // 512, 0, 2, 1, 1, stack_segment, 1024, 0,
                         ENTRY_IP, 0, 28, 0)
    open(out, 'wb').write(header.ljust(32, b'\0') + payload)
    print('%s: %d bytes' % (out, os.path.getsize(out)))


main()

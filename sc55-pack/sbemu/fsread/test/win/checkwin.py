# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""Check fsread against a volume Windows made with mkvhd.ps1.
   python3 checkwin.py FSTEST VOLUME.VHD VOLUME.TXT
FSTEST = host build of test/fstest.c. Compares, for every file and folder Windows listed:
long name, size, MD5 of the content read by fsread through its 8.3 path, and (NTFS) that
fsread's 8.3 name is the one Windows made. A raw image (no partition table) also works."""
import hashlib, struct, subprocess, sys

fstest, vhd, txt = sys.argv[1:4]
with open(vhd, 'rb') as f:
    mbr = f.read(512)
lba = 0
if mbr[510:512] == b'\x55\xaa' and mbr[3:11] not in (b'NTFS    ', b'EXFAT   '):
    lba = struct.unpack_from('<I', mbr, 446 + 8)[0]          # first partition
win = [l.rstrip('\r\n').split('|') for l in open(txt, encoding='utf-8-sig') if l[:1] in 'FD']
fs = open(txt, encoding='utf-8-sig').readline().strip()
walk = subprocess.run([fstest, vhd, str(lba), '512'], capture_output=True, text=True, errors='replace').stdout
ours = {}                                                    # long path -> (8.3 path, attr)
for l in walk.splitlines():
    if l.startswith('A '):
        p83, rest = l[2:].split(' ', 1)
        lp, attr = rest.rsplit(' ', 3)[0], rest.rsplit(' ', 3)[1]
        ours[lp] = (p83, int(attr, 16))
bad = 0; checked = 0; refused = []
def err(*a):
    global bad; bad += 1; print('BAD', *a)
for e in win:
    lp, sp = e[1], e[2]
    if lp not in ours:
        err('missing', lp); continue
    p83, attr = ours[lp]
    if 'NTFS' in fs and p83.upper() != sp.replace('/', '\\').upper():
        err('8.3 name', lp, 'ours', p83, 'Windows', sp)
    wattr = int(e[3] if e[0] == 'D' else e[4]) & 0x37
    if (attr & 0x17) != (wattr & 0x17):
        err('attributes', lp, hex(attr), hex(wattr))
    if e[0] == 'F':
        r = subprocess.run([fstest, vhd, str(lba), '512', 'cat', p83], capture_output=True)
        if r.returncode:
            if 'open -5' in r.stderr.decode():
                refused.append(lp); continue
            err('read', lp, r.stderr.decode().strip()); continue
        if len(r.stdout) != int(e[3]) or hashlib.md5(r.stdout).hexdigest() != e[5]:
            err('content', lp, len(r.stdout), e[3])
    checked += 1
extra = set(ours) - {e[1] for e in win}
for x in sorted(extra):
    print('note: fsread lists', x, '(Windows listing skipped it)')
print('%s: %d checked, %d bad, refused (compressed/encrypted): %s' % (fs, checked, bad, refused or 'none'))
sys.exit(1 if bad else 0)

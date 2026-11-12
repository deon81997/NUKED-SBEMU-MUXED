#!/bin/sh
# NTFS image with Windows-style name pairs (Win32 long name + DOS 8.3 name) on 300 files, made with
# ntfs-3g's system.ntfs_dos_name; writes dn.img, dn.txt (checkwin.py listing format) into $1.
#   sh mkdosnames.sh /tmp && python3 win/checkwin.py FSTEST /tmp/dn.img /tmp/dn.txt
set -e; O=${1:-/tmp}; M=$O/dnmnt; mkdir -p $M
rm -f $O/dn.img; truncate -s 64M $O/dn.img; L=$(sudo losetup -f --show $O/dn.img)
sudo mkntfs -q -F -Q $L >/dev/null 2>&1; sudo ntfs-3g $L $M
sudo python3 -I - $M $O/dn.txt <<'PY'
import os, random, subprocess, hashlib, sys
m, out = sys.argv[1], sys.argv[2]; d = m + '/dosnames'; os.mkdir(d); r = random.Random(1)
words = ['Report', 'Setup Files', 'My Document', 'Readme First', 'Install Notes', 'Game Save', 'Config Backup']
lines = ['#fs=NTFS ntfs-3g dos names', 'D|/dosnames|\\DOSNAMES|16']
for i in range(300):
    w = r.choice(words); n = '%s %03d.%s' % (w, i, r.choice(['txt', 'dat', 'html', 'cfg']))
    open(os.path.join(d, n), 'w').write(n)
    dos = '%s%04X~1.%s' % (''.join(c for c in w.upper() if c.isalnum())[:2], i, n.rsplit('.', 1)[1][:3].upper())
    subprocess.run(['setfattr', '-h', '-v', dos, '-n', 'system.ntfs_dos_name', os.path.join(d, n)], check=True)
    lines.append('F|/dosnames/%s|\\DOSNAMES\\%s|%d|32|%s' % (n, dos, len(n), hashlib.md5(n.encode()).hexdigest()))
open(out, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
PY
sudo umount $M; sudo losetup -d $L

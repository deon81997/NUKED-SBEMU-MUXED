#!/bin/sh
# Build exFAT / NTFS test images with real mkfs + FUSE (needs sudo, loop devices,
# exfatprogs, exfat-fuse, ntfs-3g, attr) and write the expected listing next to each.
#   sh mkimg.sh OUTDIR
# Images: exfat.img, exfat4k.img (4K sectors), ntfs.img, ntfs4k.img; *.exp = expected.
set -e
OUT=${1:-/tmp/fsimg}; HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"; M=$OUT/mnt; sudo mkdir -p "$M"

fill() {   # $1 = mount point, $2 = ntfs|exfat
  d=$1; cd "$d"
  echo hello > small.txt
  printf 'readme\r\n' > readme.txt
  echo upper > UPPER.TXT
  for i in 01 02 03 04 05 06 07 08 09 10 11 12; do echo "lfn $i" > "Long File Name $i.txt"; done
  echo dots > "a b.c.d.txt"; echo uni > "ünïcode ✓.txt"; echo hid > .hidden; echo noext > NoExtensionLongName
  mkdir -p "Sub Dir/Deeper Level/third"; head -c 100000 /dev/urandom > "Sub Dir/Deeper Level/x.bin"
  echo deep > "Sub Dir/Deeper Level/third/deep.txt"
  head -c 3000000 /dev/urandom > contig.bin
  mkdir frag; for i in $(seq 1 60); do head -c 65536 /dev/urandom > frag/p$i; sync; done
  for i in $(seq 1 2 60); do rm frag/p$i; done; sync
  head -c 2500000 /dev/urandom > frag/fragmented.bin
  mkdir many; i=0; while [ $i -lt 1500 ]; do echo $i > "many/file with long name $i.dat"; i=$((i+1)); done
  if [ $2 = ntfs ]; then
    for i in $(seq 0 1999); do printf 'D%05d' $i | dd of=sparse.bin bs=1 seek=$((i*65536)) conv=notrunc status=none; done
    echo dos > "long dos name file.txt"
    setfattr -h -v 0sTE9OR0RPfjEuVFhU -n system.ntfs_dos_name "long dos name file.txt" 2>/dev/null || true  # LONGDO~1.TXT
    mkdir comp; setfattr -h -v 0x00000800 -n system.ntfs_attrib_be comp 2>/dev/null || true
    head -c 200000 /dev/zero > comp/compressed.bin
  fi
  cd /
}

mk() {   # $1 name, $2 exfat|ntfs, $3 sector size
  img=$OUT/$1.img; rm -f "$img"; truncate -s 160M "$img"
  L=$(sudo losetup -b $3 -f --show "$img")
  if [ $2 = exfat ]; then sudo mkfs.exfat -q $L; sudo mount.exfat-fuse -o uid=$(id -u) $L $M
  else sudo mkntfs -q -F -Q -s $3 $L >/dev/null; sudo ntfs-3g -o uid=$(id -u),compression $L $M; fi
  fill $M $2
  python3 -I "$HERE/explist.py" $M > "$OUT/$1.exp"
  sudo umount $M; sudo losetup -d $L
}
mk exfat exfat 512; mk exfat4k exfat 4096; mk ntfs ntfs 512; mk ntfs4k ntfs 4096
ls -la "$OUT"

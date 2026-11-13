#!/bin/bash
# (testrig copy) exFAT/NTFS stick images for qemu-usbdrive.sh: needs exfatprogs exfat-fuse ntfs-3g gdisk fdisk; root (loop devices)
# mkimg.sh: sa.img (MBR: NTFS + exFAT), sg.img (GPT: exFAT), each filled with the same test files
set -e; cd /home/claude/vm
head -c 307200 /dev/urandom > big.bin
fill() { M=/tmp/m$RANDOM; mkdir -p $M; if [ $1 = ntfs ]; then ntfs-3g $2 $M; else L=$(losetup -f --show $2); mount.exfat-fuse $L $M >/dev/null; fi
  cp big.bin $M/DataFile10.bin; echo "long name file $1" > "$M/Long File Name.txt"
  mkdir -p $M/Dir1/SubDir; echo "nested $1" > $M/Dir1/SubDir/Nested.txt
  mkdir -p $M/MANY; for i in $(seq 1 40); do echo $i > $M/MANY/F$i.TXT; done
  sync; fusermount3 -u $M || umount $M; [ $1 = ntfs ] || losetup -d $L; }
truncate -s 64M p1.img p2.img p3.img
mkntfs -q -F -p 2048 -H 16 -S 63 p1.img >/dev/null 2>&1; mkfs.exfat p2.img >/dev/null; mkfs.exfat p3.img >/dev/null
fill ntfs p1.img; fill exfat p2.img; fill exfat p3.img
truncate -s 140M sa.img; printf 'label: dos\nstart=2048,size=131072,type=7\nstart=133120,size=131072,type=7\n' | sfdisk -q sa.img
dd if=p1.img of=sa.img bs=512 seek=2048 conv=notrunc status=none; dd if=p2.img of=sa.img bs=512 seek=133120 conv=notrunc status=none
truncate -s 70M sg.img; sgdisk -q -n 1:2048:+64M -t 1:0700 sg.img; dd if=p3.img of=sg.img bs=512 seek=2048 conv=notrunc status=none
rm -f p1.img p2.img p3.img; ls -la sa.img sg.img

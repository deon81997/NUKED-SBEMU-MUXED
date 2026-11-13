#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# mkstick.sh OUTDIR - stick images for the MENU matrix (part 2): root (loop devices), mkfs.fat, exfatprogs, ntfs-3g, genisoimage
#   s1.img  MBR: FAT16 (32 MB, GAME1.ISO) + FAT32 (64 MB, GAME2.ISO)
#   s2.img  MBR: exFAT (64 MB, GAME3.ISO) + NTFS (64 MB, GAME4.ISO)            (read-only in DOS)
#   s3.img  MBR: FAT16 (32 MB, GAME5.ISO) - the stick swapped in for s1 / s2
# Each GAMEn.ISO: ID.TXT ("DISC n"), GAME\PLAY.BAT (MPUTEST tune through SBEMU), GAME\DATA.BIN (300 KB, also kept as
# OUTDIR/data.bin for the byte check after the install).
set -e; O=$1; mkdir -p $O; cd $O; head -c 307200 /dev/urandom > data.bin
for n in 1 2 3 4 5; do rm -rf iso$n; mkdir -p iso$n/GAME; printf 'DISC %s\r\n' $n > iso$n/ID.TXT; cp data.bin iso$n/GAME/DATA.BIN
  printf '@ECHO OFF\r\nC:\\SC55\\MPUTEST.EXE /P:330 > C:\\MT.TXT\r\nIF ERRORLEVEL 1 ECHO PLAY FAILED>> C:\\UT.TXT\r\nIF NOT ERRORLEVEL 1 ECHO PLAY OK>> C:\\UT.TXT\r\n' > iso$n/GAME/PLAY.BAT
  genisoimage -quiet -V DISC$n -o GAME$n.ISO iso$n; rm -rf iso$n; done
part() { # part IMG START_SECTOR SIZE_SECTORS TYPE FS ISO
  truncate -s $(( $3 * 512 )) p.img
  case $5 in fat16) mkfs.fat -F 16 -n STICK p.img >/dev/null;; fat32) mkfs.fat -F 32 -n STICK p.img >/dev/null;;
             exfat) mkfs.exfat p.img >/dev/null;; ntfs) mkntfs -q -F -p $2 -H 16 -S 63 p.img >/dev/null 2>&1;; esac
  M=/tmp/mm$RANDOM; mkdir -p $M
  case $5 in fat*) mcopy -i p.img $6 ::/ ;;
    ntfs) ntfs-3g p.img $M; cp $6 $M/; sync; umount $M;;
    exfat) L=$(losetup -f --show p.img); mount -t exfat $L $M 2>/dev/null || mount.exfat-fuse $L $M >/dev/null
           cp $6 $M/; sync; umount $M; losetup -d $L;; esac
  dd if=p.img of=$1 bs=512 seek=$2 conv=notrunc status=none; rm -f p.img; rmdir $M; }
# FAT16 32 MB = 65536 sectors, others 64 MB = 131072
printf 'label: dos\nstart=2048,size=65536,type=6\nstart=67584,size=131072,type=c\n' > l1; truncate -s 100M s1.img; sfdisk -q s1.img < l1
part s1.img 2048 65536 6 fat16 GAME1.ISO; part s1.img 67584 131072 c fat32 GAME2.ISO
printf 'label: dos\nstart=2048,size=131072,type=7\nstart=133120,size=131072,type=7\n' > l2; truncate -s 132M s2.img; sfdisk -q s2.img < l2
part s2.img 2048 131072 7 exfat GAME3.ISO; part s2.img 133120 131072 7 ntfs GAME4.ISO
printf 'label: dos\nstart=2048,size=65536,type=6\n' > l3; truncate -s 34M s3.img; sfdisk -q s3.img < l3
part s3.img 2048 65536 6 fat16 GAME5.ISO
rm -f l1 l2 l3; ls -la s1.img s2.img s3.img

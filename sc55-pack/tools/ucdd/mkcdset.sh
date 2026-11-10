#!/bin/sh
# Builds the virtual CD programs of C:\CDSET into output/cdset (needs NASM):
#   UCDD.EXE    uCDD 0.9.5-a3 (third_party/ucdd), mount-only, with the drive's own CD-Audio + the SBEMU
#               mailbox (src/sbemulink.asm, define SBEMU_LINK)
#   SHSUCDX.COM SHSUCDX 3.09 (third_party/shsucdx) with /P:x (x: is reported as the first CD drive)
set -e
cd "$(dirname "$0")/../.."
mkdir -p output/cdset
python3 tools/ucdd/build.py output/cdset/ucdd "SBEMU_LINK=1" "UCDD_VERSION='0.9.5-a3-NUKED'"
cp output/cdset/ucdd/UCDD.EXE output/cdset/UCDD.EXE
nasm -O9 -w-all -I third_party/shsucdx/ -o output/cdset/SHSUCDX.COM third_party/shsucdx/shsucdx.nsm
ls -l output/cdset/UCDD.EXE output/cdset/SHSUCDX.COM

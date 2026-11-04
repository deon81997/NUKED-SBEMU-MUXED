# QEMU test rig

Headless DOS PC for testing the pack: FreeDOS (KERNEL.SYS/COMMAND.COM from Rufus `res/freedos`), JEMMEX,
HDPMI, SBEMU on QEMU's ICH AC97, a CD drive (VIDE-CDD + SHSUCDX).

- `mkdisk.py build <folder> c.img 120` - bootable FreeDOS FAT16 disk from a folder; `get` / `ls` read it back.
- `mkiso.py duke.iso` - ISO9660 with `SUPPORT\DUKE3D.IDF` of the size Duke 3D Atomic 1.5 checks (479985668 bytes, sparse).
- `vm.py start | shot NAME | key K | type TEXT | cmd HMP | stop` - drives QEMU through its monitor.
- `kt.c` - KT.EXE: sends a key script to SBEMU's key player (same side channel as GAMETEST).

QEMU 8.2.2 built with `--target-list=i386-softmmu`, `--python=/usr/bin/python3.12`, dtc/keycodemapdb from GitHub mirrors, tests/fp skipped.

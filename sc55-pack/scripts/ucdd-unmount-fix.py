#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""Patch the pack's uCDD 0.9.5-a2 (mount-only build with the SBCDA1 CD-audio mailbox for SBEMU /VCDA).

That build points the unit's audio entry at its mailbox handler when an image with audio tracks is mounted,
and clears it again in clear_unit - but the two guards that refuse to change a disc while an audio entry is
attached were left in place, so a CUE with music could never be unmounted ("The drive is locked or in use").
This removes those two guards (control function op 2 and the IOCTL eject request).

usage: ucdd-unmount-fix.py UCDD.EXE OUT.EXE    (input md5 e843a21a..., 40757 bytes)
"""
import sys
H = 32  # EXE header (2 paragraphs); offsets below are from the start of the driver payload
PATCHES = ((0x3C1, '0f856d02'),   # IOCTL output eject: jnz request_error when AUDIO_ENTRY != 0
           (0x6D2, '0f85f600'))   # control op 2 (UCDD -unmount): jnz .done (8002h) when AUDIO_ENTRY != 0
d = bytearray(open(sys.argv[1], 'rb').read())
for off, exp in PATCHES:
    got = d[H + off:H + off + 4].hex()
    if got == '90909090': continue
    if got != exp: sys.exit(f'unexpected bytes at {off:#x}: {got} (not the pack build?)')
    d[H + off:H + off + 4] = b'\x90' * 4
open(sys.argv[2], 'wb').write(d)
print('patched', sys.argv[2])

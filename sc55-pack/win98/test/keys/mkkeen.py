#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
# mkkeen.py KEYLOG.COM KEEN4E.EXE : KEYLOG wrapped in an MZ header (CS:IP = PSP:100h), padded to
# 103224 bytes, so its id (name + size) is Commander Keen 4's (B408B7B3) in PADMAP.TXT
import struct, sys
com = open(sys.argv[1], 'rb').read(); size = 103224
hdr = struct.pack('<2sHHHHHHHHHHHHH', b'MZ', size % 512, (size + 511) // 512, 0, 2, 0x10, 0xFFFF,
                  0xFFF0, 0xFFFE, 0, 0x100, 0xFFF0, 0x1C, 0).ljust(32, b'\0')
open(sys.argv[2], 'wb').write(hdr + com + b'\0' * (size - 32 - len(com)))

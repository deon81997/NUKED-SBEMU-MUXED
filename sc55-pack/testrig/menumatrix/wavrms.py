# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""wavrms.py OUT.WAV - "WAV n s loud" = seconds of the QEMU AC97 output with sound (16-bit; peak > 300 per 0.1 s)"""
import sys, struct
d = open(sys.argv[1], 'rb').read(); i = 12; fmt = None
while i + 8 <= len(d):
    cid, sz = d[i:i+4], struct.unpack('<I', d[i+4:i+8])[0]
    if cid == b'fmt ': fmt = struct.unpack('<HHIIHH', d[i+8:i+24])
    if cid == b'data': data = d[i+8:]; break
    i += 8 + sz
ch, rate = fmt[1], fmt[2]; n = len(data) // 2; s = struct.unpack('<%dh' % n, data[:n*2]); blk = rate * ch // 10
loud = sum(1 for k in range(0, n, blk) if max(abs(v) for v in s[k:k+blk] or [0]) > 300)
print('WAV %d s, %.1f s loud' % (n // (rate * ch), loud / 10))

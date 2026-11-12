#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""Write a Windows 9x-compatible .ICO (8-bit paletted BMP entries + AND mask)."""
import struct, sys
from PIL import Image, ImageDraw

def draw(sz):
    im = Image.new('RGBA', (sz, sz), (0, 0, 0, 0)); d = ImageDraw.Draw(im); s = sz / 32
    d.rectangle([1*s, 6*s, 31*s-1, 26*s-1], fill=(40, 40, 48, 255), outline=(10, 10, 10, 255))
    d.rectangle([4*s, 9*s, 20*s-1, 15*s-1], fill=(255, 150, 40, 255))
    for i in range(7):
        x = 4*s + i*3.6*s; d.rectangle([x, 18*s, x+3*s-1, 24*s-1], fill=(240, 240, 240, 255))
    for i in [0, 1, 3, 4, 5]:
        x = 4*s + i*3.6*s + 2.2*s; d.rectangle([x, 18*s, x+1.6*s-1, 21.5*s-1], fill=(20, 20, 20, 255))
    d.ellipse([23*s, 9*s, 28*s-1, 14*s-1], fill=(180, 180, 190, 255))
    return im

def entry(im):
    w, h = im.size
    rgb = im.convert('RGB').quantize(colors=256)
    pal = rgb.getpalette()[:768]; pal += [0] * (768 - len(pal))
    px = rgb.load(); a = im.load()
    xor = b''
    for y in range(h - 1, -1, -1):
        row = bytes(px[x, y] if a[x, y][3] >= 128 else 0 for x in range(w))
        xor += row + b'\0' * ((4 - len(row) % 4) % 4)
    andm = b''
    for y in range(h - 1, -1, -1):
        bits = 0; row = b''
        for x in range(w):
            bits = (bits << 1) | (1 if a[x, y][3] < 128 else 0)
            if x % 8 == 7: row += bytes([bits]); bits = 0
        andm += row + b'\0' * ((4 - len(row) % 4) % 4)
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 8, 0, len(xor) + len(andm), 0, 0, 256, 0)
    quad = b''.join(bytes([pal[i*3+2], pal[i*3+1], pal[i*3], 0]) for i in range(256))
    return hdr + quad + xor + andm

imgs = [draw(32), draw(32).resize((16, 16), Image.LANCZOS)]
data = [entry(i) for i in imgs]
out = struct.pack('<HHH', 0, 1, len(imgs)); off = 6 + 16 * len(imgs)
for im, d in zip(imgs, data):
    out += struct.pack('<BBBBHHII', im.size[0], im.size[1], 0, 0, 1, 8, len(d), off); off += len(d)
open(sys.argv[1], 'wb').write(out + b''.join(data))

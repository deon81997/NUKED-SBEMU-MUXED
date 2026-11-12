#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""Spinning voxel figures for README.md (all original, made with Pillow): the SC-55-style sound module,
a beige DOS PC, Port 220 in 3D, a gamepad and Flopsy the floppy.

    python3 docs/90s/voxels.py      -> writes vox-*.gif next to this script
"""
import math, os
from PIL import Image, ImageDraw

OUT = os.path.dirname(os.path.abspath(__file__))


def box(m, x0, y0, z0, x1, y1, z1, c):
    for x in range(x0, x1):
        for y in range(y0, y1):
            for z in range(z0, z1):
                m[(x, y, z)] = c


def shade(c, f): return tuple(max(0, min(255, int(v * f))) for v in c)


FACES = [((1, 0, 0), [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)]), ((-1, 0, 0), [(0, 0, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1)]),
         ((0, 1, 0), [(0, 1, 0), (1, 1, 0), (1, 1, 1), (0, 1, 1)]), ((0, -1, 0), [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]),
         ((0, 0, 1), [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]), ((0, 0, -1), [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)])]
LIGHT = (0.45, -0.55, 0.7)


def render(m, size, frames=16, scale=7, tilt=0.5, bg=(0, 0, 32), bob=False, ms=90, name='vox.gif'):
    xs, ys, zs = zip(*m)
    cx, cy, cz = (max(xs) + min(xs) + 1) / 2, (max(ys) + min(ys) + 1) / 2, (max(zs) + min(zs) + 1) / 2
    W, H = size
    out = []
    for k in range(frames):
        a = 0.8 * math.sin(2 * math.pi * k / frames)                # swing left-right, face stays in view
        ca, sa, ct, st = math.cos(a), math.sin(a), math.cos(tilt), math.sin(tilt)

        def rot(p):
            x, y, z = p[0] - cx, p[1] - cy, p[2] - cz
            x, y = x * ca - y * sa, x * sa + y * ca
            return x, y * ct - z * st, y * st + z * ct       # screen x, depth (towards viewer = smaller), screen up

        polys = []
        for (x, y, z), c in m.items():
            for n, corners in FACES:
                if (x + n[0], y + n[1], z + n[2]) in m: continue
                rn = rot((n[0] + cx, n[1] + cy, n[2] + cz))
                if rn[1] > 0: continue                         # facing away
                pts = [rot((x + u, y + v, z + w)) for u, v, w in corners]
                depth = sum(p[1] for p in pts) / 4
                l = 0.55 + 0.45 * max(0, -rn[0] * LIGHT[0] * -1 + -rn[1] * 0.4 + rn[2] * LIGHT[2])
                dy = abs(math.sin(2 * math.pi * k / frames * 2)) * 4 if bob else 0
                polys.append((depth, [(W / 2 + p[0] * scale, H / 2 - (p[2] + dy / scale) * scale) for p in pts], shade(c, l)))
        im = Image.new('RGB', (W, H), bg)
        d = ImageDraw.Draw(im)
        d.ellipse([W / 2 - W * 0.3, H - 22, W / 2 + W * 0.3, H - 8], fill=shade(bg, 0.5) if bg != (0, 0, 0) else (20, 20, 20))
        for depth, pts, col in sorted(polys, key=lambda t: -t[0]):
            d.polygon(pts, fill=col, outline=shade(col, 0.8))
        out.append(im.convert('P', palette=Image.ADAPTIVE, colors=64))
    out[0].save(os.path.join(OUT, name), save_all=True, append_images=out[1:], duration=ms, loop=0, optimize=True, disposal=2)


def sc55():
    m = {}
    box(m, 0, 0, 0, 22, 9, 4, (60, 62, 70))                       # half-rack case
    box(m, 0, -1, 0, 22, 0, 4, (40, 42, 48))                        # front panel
    box(m, 3, -2, 1, 11, -1, 3, (90, 200, 90))                      # LCD
    for i in range(5): m[(4 + i * 2, -2, 2)] = (20, 60, 20)       # LCD bars
    for i in range(4): box(m, 13 + i * 2, -2, 1, 14 + i * 2, -1, 2, (200, 200, 200))   # buttons
    m[(20, -2, 2)] = (255, 120, 0)                                  # knob
    m[(1, -2, 3)] = (255, 40, 40)                                   # power LED
    return m


def pc():
    m = {}
    box(m, 0, 0, 0, 16, 14, 4, (210, 200, 170))                     # desktop case
    box(m, 2, -1, 1, 8, 0, 2, (60, 60, 60))                         # floppy slot
    m[(13, -1, 2)] = (0, 255, 0)
    box(m, 2, 2, 4, 14, 13, 14, (220, 210, 180))                    # monitor
    box(m, 3, 1, 5, 13, 2, 13, (40, 40, 40))                        # bezel
    box(m, 4, 0, 6, 12, 1, 12, (0, 0, 0))                           # screen
    for i, x in enumerate((4, 5, 6, 7)): m[(x, -1, 11)] = (200, 200, 200)   # C:\>
    m[(9, -1, 11)] = (200, 200, 200) if True else None
    box(m, 1, -6, 0, 15, -3, 1, (200, 190, 160))                    # keyboard
    return m


def port220():
    m = {}
    box(m, 0, 0, 0, 14, 2, 10, (30, 150, 70))                       # the card
    box(m, -2, 0, 0, 0, 2, 12, (170, 170, 180))                     # bracket
    for z in (3, 6, 9): m[(-3, 0, z)] = (60, 60, 200)               # jacks
    for x in range(3, 12, 2): box(m, x, 0, -2, x + 1, 2, 0, (230, 180, 40))   # gold fingers
    for x, z in ((4, 6), (9, 6)):                                   # eyes
        box(m, x, -1, z, x + 2, 0, z + 2, (255, 255, 255)); m[(x + 1, -2, z)] = (0, 0, 0)
    for x in range(5, 10): m[(x, -1, 3)] = (0, 0, 0)                # smile
    m[(4, -1, 4)] = m[(10, -1, 4)] = (0, 0, 0)
    box(m, 10, -1, 1, 13, 0, 3, (20, 20, 20))                       # chip
    return m


def gamepad():
    m = {}
    box(m, 0, 0, 0, 18, 8, 3, (120, 120, 135))
    box(m, -2, 4, 0, 3, 10, 3, (120, 120, 135)); box(m, 15, 4, 0, 20, 10, 3, (120, 120, 135))   # grips
    for x, y in ((3, 2), (4, 2), (5, 2), (4, 1), (4, 3)): m[(x, y, 3)] = (20, 20, 20)     # d-pad
    for (x, y), c in (((13, 1), (40, 200, 40)), ((15, 2), (220, 40, 40)), ((11, 2), (40, 90, 230)), ((13, 3), (240, 200, 30))):
        m[(x, y, 3)] = c
    m[(8, 2, 3)] = m[(10, 2, 3)] = (160, 160, 160)
    return m


def flopsy():
    m = {}
    box(m, 0, 0, 0, 12, 2, 12, (40, 60, 200))                       # disk
    box(m, 3, -1, 8, 9, 0, 12, (190, 190, 200))                     # shutter
    box(m, 2, -1, 1, 10, 0, 6, (240, 240, 240))                     # label
    for x in (4, 7): box(m, x, -2, 3, x + 1, -1, 5, (0, 0, 0))      # eyes
    box(m, 3, 0, -3, 4, 1, 0, (40, 40, 40)); box(m, 8, 0, -3, 9, 1, 0, (40, 40, 40))   # legs
    box(m, 2, -1, -4, 5, 2, -3, (220, 30, 30)); box(m, 7, -1, -4, 10, 2, -3, (220, 30, 30))   # shoes
    return m


if __name__ == '__main__':
    render(sc55(), (240, 150), scale=8, tilt=0.75, name='vox-sc55.gif')
    render(pc(), (200, 200), scale=7, tilt=0.4, name='vox-pc.gif')
    render(port220(), (200, 200), scale=10, bob=True, name='vox-port220.gif')
    render(gamepad(), (220, 150), scale=8, tilt=0.9, name='vox-pad.gif')
    render(flopsy(), (180, 200), scale=10, bob=True, name='vox-flopsy.gif')
    for f in sorted(os.listdir(OUT)):
        if f.startswith('vox-'): print(f, os.path.getsize(os.path.join(OUT, f)))

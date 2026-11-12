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


KEY = (255, 0, 255)


def render(m, size, frames=12, scale=7, tilt=0.5, bg=(0, 0, 32), bob=True, ms=90, name='vox.gif', cap=None, spin=False):
    size = (size[0] // 2, size[1] // 2); scale = scale / 2         # drawn at half size, stored 2x: chunky pixels like the sprites
    xs, ys, zs = zip(*m)
    cx, cy, cz = (max(xs) + min(xs) + 1) / 2, (max(ys) + min(ys) + 1) / 2, (max(zs) + min(zs) + 1) / 2
    W, H = size
    raw = []
    for k in range(frames):
        a = 2 * math.pi * k / frames if spin else 0.35 * math.sin(2 * math.pi * k / frames)   # spin, or a little dance
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
                dy = abs(math.sin(2 * math.pi * k / frames * 2)) * 3 if bob else 0
                polys.append((depth, [(p[0], -(p[2] + dy / 3.5)) for p in pts], shade(c, l)))
        raw.append(polys)
    xs = [x for polys in raw for _, pts, _ in polys for x, _ in pts]; ys = [y for polys in raw for _, pts, _ in polys for _, y in pts]
    scale = min(scale, 0.94 * W / (max(xs) - min(xs)), 0.94 * H / (max(ys) - min(ys)))   # fits the frame, nothing clipped
    ox, oy = W / 2 - (max(xs) + min(xs)) / 2 * scale, H / 2 - (max(ys) + min(ys)) / 2 * scale
    out = []
    for polys in raw:
        polys = [(dp, [(ox + x * scale, oy + y * scale) for x, y in pts], col) for dp, pts, col in polys]
        im = Image.new('RGB', (W, H), KEY)
        d = ImageDraw.Draw(im)
        for depth, pts, col in sorted(polys, key=lambda t: -t[0]):
            d.polygon(pts, fill=col, outline=shade(col, 0.75))
        if cap:                                                     # pixel caption under the figure, like the nav buttons
            from PIL import ImageFont
            ft = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf', 8)
            c2 = Image.new('RGB', (W, H + 24), KEY); c2.paste(im, (0, 0)); im = c2
            d = ImageDraw.Draw(im); d.fontmode = '1'
            lines = cap.split('|')
            tw = max(d.textlength(l, font=ft) for l in lines)
            d.rounded_rectangle([W // 2 - tw / 2 - 3, H - 1, W // 2 + tw / 2 + 3, H + 22], 3, fill=(16, 16, 24))   # plate: readable on white and dark pages
            for j, (line, col) in enumerate(zip(lines, ((255, 255, 0), (255, 255, 255)))):
                d.text((W // 2 + 1, H + 5 + j * 11), line, font=ft, fill=(0, 0, 0), anchor='mm')
                d.text((W // 2, H + 4 + j * 11), line, font=ft, fill=col, anchor='mm')
        im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
        out.append(im)
    sheet = Image.new('RGB', (out[0].width * len(out), out[0].height))
    for i, f in enumerate(out): sheet.paste(f, (i * f.width, 0))
    palimg = sheet.quantize(colors=63, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = palimg.getpalette()
    ki = min(range(len(pal) // 3), key=lambda i: sum(abs(pal[i * 3 + j] - KEY[j]) for j in range(3)))
    frames = [f.quantize(palette=palimg, dither=Image.NONE) for f in out]   # one palette, magenta -> transparent
    frames[0].save(os.path.join(OUT, name), save_all=True, append_images=frames[1:], duration=ms, loop=0, disposal=2,
                   transparency=ki, optimize=False)


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


def soundcard():
    m = {}
    box(m, 0, 0, 0, 18, 1, 9, (25, 120, 60))                        # PCB
    box(m, -2, -1, 0, 0, 2, 11, (170, 170, 180))                    # bracket
    for z, c in ((2, (230, 150, 40)), (4, (60, 160, 230)), (6, (230, 90, 160)), (8, (90, 220, 90))): m[(-3, 0, z)] = c   # jacks
    for x in range(3, 16, 2): box(m, x, 0, -2, x + 1, 1, 0, (230, 180, 40))   # gold fingers
    box(m, 3, -2, 3, 8, 0, 7, (25, 25, 30))                         # DSP chip
    box(m, 10, -2, 4, 13, 0, 7, (25, 25, 30))                       # OPL3
    box(m, 14, -1, 2, 17, 0, 4, (220, 210, 90))                     # crystal
    box(m, 3, -1, 8, 16, 0, 9, (200, 200, 210))                     # game/MIDI port
    return m


def cdrom():
    m = {}
    box(m, 0, 0, 0, 18, 16, 5, (215, 205, 175))                     # 5.25" bay drive
    box(m, 1, -1, 1, 17, 0, 4, (45, 45, 50))                        # front bezel
    box(m, 3, -2, 2, 15, -1, 3, (25, 25, 28))                       # tray slot
    m[(15, -2, 1)] = (0, 230, 0); m[(13, -2, 1)] = (200, 200, 200)  # LED, eject
    for x in range(4, 14):                                          # the disc, half out
        for y in range(-9, -2):
            if (x - 8.5) ** 2 + (y + 5.5) ** 2 < 22: m[(x, y, 2)] = (200, 210, 230) if (x + y) % 3 else (230, 180, 230)
    m[(8, -6, 2)] = m[(9, -6, 2)] = (90, 90, 100)
    return m


def usbstick():
    m = {}
    box(m, 0, 0, 0, 5, 14, 3, (210, 40, 40))                        # body
    box(m, 1, -5, 0, 4, 0, 3, (200, 200, 210))                      # connector
    box(m, 1, -5, 1, 4, -1, 2, (40, 40, 40))
    m[(2, 12, 3)] = (255, 230, 60)                                  # LED
    box(m, 1, 4, 3, 4, 10, 4, (240, 240, 240))                      # label
    return m


def keymouse():
    m = {}
    box(m, 0, 0, 0, 22, 8, 2, (205, 200, 185))                      # keyboard
    for x in range(1, 21, 2):
        for y in range(1, 7, 2): m[(x, y, 2)] = (235, 232, 220)
    box(m, 6, 1, 2, 15, 2, 3, (235, 232, 220))                      # space bar
    box(m, 25, 2, 0, 30, 9, 3, (205, 200, 185))                     # mouse
    box(m, 25, 7, 3, 30, 9, 4, (180, 175, 160)); m[(27, 9, 3)] = (60, 60, 60)
    box(m, 27, 9, 1, 28, 14, 2, (120, 120, 120))                    # cable
    return m


def joystick():
    m = {}
    box(m, 0, 0, 0, 10, 10, 3, (40, 40, 45))                        # base
    box(m, 4, 4, 3, 6, 6, 12, (60, 60, 65))                         # stick
    box(m, 3, 3, 12, 7, 7, 15, (200, 30, 30))                       # grip top
    m[(4, 3, 14)] = (255, 220, 0)                                   # hat
    box(m, 1, 1, 3, 3, 3, 4, (220, 40, 40)); box(m, 7, 1, 3, 9, 3, 4, (40, 120, 230))   # base buttons
    box(m, 9, 4, 1, 14, 5, 2, (90, 90, 90))                         # cable to the gameport
    return m


def win98():
    m = {}
    box(m, 0, 0, 0, 18, 12, 14, (220, 212, 185))                    # CRT
    box(m, 1, -1, 1, 17, 0, 13, (50, 50, 55))
    box(m, 2, -2, 2, 16, -1, 12, (0, 128, 128))                     # teal desktop
    box(m, 4, -3, 5, 14, -2, 11, (192, 192, 192))                   # a window
    box(m, 4, -3, 10, 14, -2, 11, (0, 0, 128))                      # title bar
    m[(13, -4, 10)] = (192, 192, 192)
    box(m, 2, -3, 2, 16, -2, 3, (192, 192, 192))                    # taskbar
    m[(3, -4, 2)] = (0, 160, 0)                                     # Start
    box(m, 4, 4, -2, 14, 10, 0, (205, 197, 170))                    # stand
    return m


def claude():
    """Claude as a little voxel buddy (original figure, not a logo): coral body, typing on a beige keyboard"""
    m = {}
    C, D = (217, 119, 87), (180, 90, 62)
    box(m, 2, 2, 0, 10, 8, 7, C)                                    # body
    box(m, 1, 1, 7, 11, 9, 15, C)                                   # head
    box(m, 3, 0, 10, 5, 1, 13, (255, 255, 255)); m[(4, -1, 11)] = (30, 20, 20)   # eyes
    box(m, 8, 0, 10, 10, 1, 13, (255, 255, 255)); m[(8, -1, 11)] = (30, 20, 20)
    for x in range(5, 8): m[(x, 0, 8)] = (90, 40, 30)               # smile, corners up
    m[(4, 0, 9)] = m[(8, 0, 9)] = (90, 40, 30)
    m[(2, 0, 9)] = m[(10, 0, 9)] = (250, 160, 150)                  # cheeks
    box(m, 5, 4, 15, 7, 6, 17, D); box(m, 4, 3, 17, 8, 7, 19, (255, 200, 90))   # antenna with a light
    box(m, 0, 0, 3, 2, 3, 5, C); box(m, 10, 0, 3, 12, 3, 5, C)      # arms reaching forward
    box(m, -1, -5, 0, 13, -1, 2, (215, 205, 180))                   # keyboard
    for x in range(0, 12, 2):
        for y in (-4, -2): m[(x, y, 2)] = (240, 235, 220)
    box(m, 3, 2, -3, 5, 5, 0, D); box(m, 7, 2, -3, 9, 5, 0, D)      # legs
    return m


# ---------------------------------------------------------------- small icons (feature list, test lab, hall of fame)
def disc(x0=0, y0=0, z=0, r=6.5, m=None):
    m = {} if m is None else m
    for x in range(-7, 8):
        for y in range(-7, 8):
            d = x * x + y * y
            if d < r * r and d > 2: m[(x0 + x, y0 + y, z)] = (210, 215, 235) if (x + y) % 4 else (235, 190, 235)
    return m


def i_speaker():
    m = {}
    box(m, 0, 0, 0, 8, 6, 12, (205, 190, 155))
    for x, z, r in ((4, 8, 2.2), (4, 3, 1.4)):
        for dx in range(-3, 4):
            for dz in range(-3, 4):
                if dx * dx + dz * dz <= r * r: m[(x + dx, -1, z + dz)] = (40, 40, 40) if dx * dx + dz * dz > 1 else (170, 170, 170)
    return m


def i_mt32():
    m = {}
    box(m, 0, 0, 0, 22, 9, 4, (180, 180, 185))
    box(m, 3, -1, 1, 11, 0, 3, (230, 100, 40))                      # orange display
    for i in range(5): box(m, 13 + i * 2, -1, 1, 14 + i * 2, 0, 2, (40, 40, 40))
    box(m, 1, 10, 0, 21, 14, 2, (240, 240, 240))                    # keys behind
    for x in range(2, 21, 3): box(m, x, 10, 2, x + 1, 13, 3, (20, 20, 20))
    return m


def i_note():
    m = {}
    box(m, 0, 0, 0, 4, 2, 3, (255, 210, 60)); box(m, 3, 0, 3, 4, 2, 13, (255, 210, 60))
    box(m, 8, 0, 2, 12, 2, 5, (255, 210, 60)); box(m, 11, 0, 5, 12, 2, 15, (255, 210, 60))
    box(m, 3, 0, 12, 12, 2, 15, (255, 210, 60))
    return m


def i_bug():
    m = {}
    box(m, 1, 0, 1, 9, 10, 4, (200, 40, 40)); box(m, 5, 0, 4, 5, 10, 4, (0, 0, 0))
    for y in (2, 5, 8): m[(4, y, 4)] = m[(6, y, 4)] = (20, 20, 20)
    box(m, 3, -3, 1, 7, 0, 4, (30, 30, 30))                         # head
    m[(4, -4, 3)] = m[(6, -4, 3)] = (255, 255, 255)
    for y in (2, 5, 8): m[(0, y, 1)] = m[(-1, y, 0)] = m[(9, y, 1)] = m[(10, y, 0)] = (30, 30, 30)   # legs
    m[(3, -5, 4)] = m[(7, -5, 4)] = (30, 30, 30)                    # feelers
    return m


def i_laptop():
    m = {}
    box(m, 0, 0, 0, 18, 12, 1, (50, 50, 55))                        # base
    for x in range(1, 17, 2):
        for y in range(2, 8, 2): m[(x, y, 1)] = (90, 90, 95)
    box(m, 0, 12, 1, 18, 13, 13, (50, 50, 55)); box(m, 1, 11, 2, 17, 12, 12, (40, 90, 160))   # lid, screen
    return m


def i_tower():
    m = {}
    box(m, 0, 0, 0, 8, 14, 20, (215, 205, 175))
    box(m, 1, -1, 15, 7, 0, 17, (60, 60, 60)); box(m, 1, -1, 11, 7, 0, 13, (60, 60, 60))   # drive bays
    box(m, 2, -1, 8, 5, 0, 9, (40, 40, 40))                         # floppy
    m[(6, -1, 4)] = (0, 230, 0); m[(6, -1, 2)] = (230, 120, 0)
    return m


def i_trophy():
    m = {}
    G = (240, 190, 40)
    box(m, 2, 2, 0, 9, 9, 2, (90, 60, 30))
    box(m, 4, 4, 2, 7, 7, 6, G)
    for z in range(6, 13):
        r = 2 + (z - 6) * 0.45
        for x in range(-4, 5):
            for y in range(-4, 5):
                if x * x + y * y <= r * r: m[(5 + x, 5 + y, z)] = G
    box(m, -1, 5, 8, 0, 6, 12, G); box(m, 11, 5, 8, 12, 6, 12, G)  # handles
    return m


def i_crate():
    m = {}
    box(m, 0, 0, 0, 12, 12, 10, (190, 140, 80))
    for z in (0, 9): box(m, 0, -1, z, 12, 0, z + 1, (130, 90, 50))
    box(m, 5, -1, 0, 7, 0, 10, (130, 90, 50))
    return m


def i_scroll():
    m = {}
    box(m, 0, 0, 0, 14, 1, 16, (240, 225, 180))
    box(m, -1, -1, 15, 15, 2, 17, (200, 170, 120)); box(m, -1, -1, -1, 15, 2, 1, (200, 170, 120))
    for z in range(3, 14, 2): box(m, 2, -1, z, 2 + 4 + (z * 7) % 6, 0, z + 1, (60, 120, 200) if z % 4 == 1 else (80, 60, 40))
    return m


def i_book():
    m = {}
    box(m, 0, 0, 0, 14, 18, 3, (40, 70, 160)); box(m, 1, 1, 3, 13, 17, 4, (240, 235, 220))
    box(m, 1, 1, 4, 13, 17, 5, (40, 70, 160)); box(m, 4, 6, 5, 10, 8, 6, (255, 210, 60))
    return m


def i_palette():
    m = {}
    for x in range(0, 16):
        for y in range(0, 12):
            if ((x - 8) / 8) ** 2 + ((y - 6) / 6) ** 2 < 1 and not ((x - 4) ** 2 + (y - 8) ** 2 < 3): m[(x, y, 0)] = (200, 160, 110)
    for (x, y), c in (((6, 3), (230, 40, 40)), ((9, 2), (40, 160, 230)), ((12, 4), (60, 200, 60)), ((12, 8), (255, 210, 60)), ((9, 9), (170, 60, 200))):
        box(m, x, y, 1, x + 2, y + 2, 2, c)
    return m


def i_menu():
    m = win98()
    for (x, y, z), c in list(m.items()):
        if c in ((0, 128, 128), (192, 192, 192), (0, 0, 128)) and y < 0:
            m[(x, y, z)] = (0, 0, 170) if c == (0, 128, 128) else (230, 100, 40) if z in (9, 10) else (0, 170, 170)
    return m


def i_star():
    m = {}
    pts = [(math.cos(math.pi / 2 + i * math.pi / 5) * (7 if i % 2 == 0 else 3), math.sin(math.pi / 2 + i * math.pi / 5) * (7 if i % 2 == 0 else 3)) for i in range(10)]
    def inside(x, z):
        c = False
        for i in range(10):
            (x1, z1), (x2, z2) = pts[i], pts[(i + 1) % 10]
            if (z1 > z) != (z2 > z) and x < (x2 - x1) * (z - z1) / (z2 - z1) + x1: c = not c
        return c
    for x in range(-8, 9):
        for z in range(-8, 9):
            if inside(x + 0.5, z + 0.5):
                for y in range(0, 2): m[(x, y, z)] = (255, 215, 40) if (x + z) % 3 else (255, 245, 150)
    return m


def pixel_icons():
    """the tested / ported marks for the sound chip table (2D, 12x12 stored 2x)"""
    from PIL import ImageDraw as D2
    def px(name, draw):
        fr = []
        for k in range(6):
            im = Image.new('RGB', (12, 12), KEY); draw(D2.Draw(im), k); fr.append(im.resize((24, 24), Image.NEAREST))
        pal = fr[0].quantize(colors=16)
        q = [f.quantize(palette=pal) for f in fr]
        pl = q[0].getpalette(); ki = min(range(len(pl) // 3), key=lambda i: sum(abs(pl[i * 3 + j] - KEY[j]) for j in range(3)))
        q[0].save(os.path.join(OUT, name), save_all=True, append_images=q[1:], duration=150, loop=0, transparency=ki, disposal=2)
    def ok(d, k):
        d.rectangle([0, 0, 11, 11], fill=(0, 140, 0), outline=(0, 80, 0))
        d.line([(2, 6), (4, 9), (9, 2)], fill=(255, 255, 255) if k % 3 else (200, 255, 200), width=2)
    def lab(d, k):
        d.rectangle([4, 0, 7, 1], fill=(200, 200, 210)); d.rectangle([4, 2, 7, 5], fill=(220, 230, 240))
        d.polygon([(4, 5), (7, 5), (11, 11), (0, 11)], fill=(220, 230, 240))
        d.polygon([(2, 8), (9, 8), (11, 11), (0, 11)], fill=(80, 220, 120))
        d.point([(5 + (k % 3), 9 - (k % 2)), (3 + k % 4, 7 - k % 3)], fill=(255, 255, 255))
    px('ok.gif', ok); px('lab.gif', lab)


def tv():
    m = {}
    box(m, 0, 0, 0, 16, 12, 12, (90, 60, 40))                       # wooden TV
    box(m, 2, -1, 2, 12, 0, 10, (20, 20, 25)); box(m, 3, -2, 3, 11, -1, 9, (40, 160, 230))
    for x, c in ((4, (255, 255, 0)), (5, (255, 255, 0)), (7, (255, 80, 80)), (8, (255, 80, 80)), (9, (255, 80, 80))): m[(x, -3, 6)] = c   # NEW!
    m[(13, -1, 8)] = m[(13, -1, 5)] = (200, 200, 200)
    box(m, 5, 4, 12, 6, 5, 16, (60, 60, 60)); box(m, 10, 4, 12, 11, 5, 16, (60, 60, 60))   # rabbit ears
    return m


def camera():
    m = {}
    box(m, 0, 0, 0, 16, 8, 10, (40, 40, 45)); box(m, 2, 0, 10, 6, 8, 12, (40, 40, 45))
    for x in range(-3, 4):
        for z in range(-3, 4):
            if x * x + z * z <= 10: m[(8 + x, -1, 5 + z)] = (90, 90, 100) if x * x + z * z > 4 else (60, 140, 230)
    m[(13, 0, 10)] = (230, 40, 40); box(m, 12, -1, 7, 15, 0, 9, (220, 220, 230))
    return m


def gift():
    m = {}
    box(m, 0, 0, 0, 12, 12, 10, (220, 40, 60))
    box(m, 5, -1, 0, 7, 13, 11, (255, 210, 40)); box(m, -1, 5, 0, 13, 7, 11, (255, 210, 40))
    box(m, 3, 5, 11, 5, 7, 13, (255, 210, 40)); box(m, 7, 5, 11, 9, 7, 13, (255, 210, 40))
    return m


def toolbox():
    m = {}
    box(m, 0, 0, 0, 18, 8, 7, (210, 40, 40)); box(m, 0, 0, 7, 18, 8, 8, (170, 30, 30))
    box(m, 6, 3, 8, 7, 5, 11, (80, 80, 80)); box(m, 11, 3, 8, 12, 5, 11, (80, 80, 80)); box(m, 6, 3, 11, 12, 5, 12, (80, 80, 80))
    box(m, 8, -1, 4, 10, 0, 6, (200, 200, 200))
    box(m, 2, 2, 8, 3, 3, 14, (160, 160, 170)); box(m, 1, 1, 13, 4, 4, 15, (160, 160, 170))   # wrench sticking out
    return m


def arcade():
    m = {}
    box(m, 0, 0, 0, 12, 10, 24, (40, 40, 150))
    box(m, 1, -1, 13, 11, 0, 20, (20, 20, 20)); box(m, 2, -2, 14, 10, -1, 19, (0, 180, 60))
    for x, z in ((4, 17), (7, 16), (5, 15)): m[(x, -3, z)] = (255, 255, 0)
    box(m, 0, -4, 9, 12, 0, 11, (60, 60, 170))                      # control panel
    box(m, 3, -3, 11, 4, -2, 14, (30, 30, 30)); m[(3, -3, 14)] = (230, 30, 30)
    m[(7, -3, 11)] = (230, 30, 30); m[(9, -3, 11)] = (40, 160, 230)
    box(m, 1, -1, 21, 11, 0, 24, (255, 180, 0))                     # marquee
    return m


def heart():
    m = {}
    for x in range(-7, 8):
        for z in range(-7, 7):
            X, Z = x / 6.0, z / 6.0 + 0.15
            if (X * X + Z * Z - 1) ** 3 - X * X * Z ** 3 < 0:
                for y in range(0, 3): m[(x, y, z)] = (230, 30, 70) if (x + z) % 4 else (255, 110, 140)
    return m


if __name__ == '__main__':
    render(sc55(), (240, 150), scale=8, tilt=0.75, name='vox-sc55.gif')
    render(pc(), (200, 200), scale=7, tilt=0.4, name='vox-pc.gif')
    render(port220(), (200, 200), scale=10, bob=True, name='vox-port220.gif')
    render(gamepad(), (220, 150), scale=8, tilt=0.9, name='vox-pad.gif')
    render(flopsy(), (180, 200), scale=10, bob=True, name='vox-flopsy.gif')
    for fn, sc, t, cap in ((soundcard, 6, 0.55, 'SOUND BLASTER 16|any PCI sound card'), (sc55, 5, 0.75, 'ROLAND SC-55|General MIDI'),
                           (cdrom, 5, 0.7, 'CD AUDIO + IMAGES|ISO / CUE'), (usbstick, 7, 0.75, 'USB STICKS|DOS drive letters'),
                           (gamepad, 5, 0.9, 'USB GAMEPADS|as a DOS joystick'), (keymouse, 5, 0.8, 'USB KEYB + MOUSE|in plain DOS'),
                           (joystick, 6, 0.45, 'GAMEPORT|CH, TM, GrIP'), (win98, 5, 0.35, 'WINDOWS 98|DOS boxes + MIDI')):
        render(fn(), (180, 110), scale=sc, tilt=t, name='vox-part-%s.gif' % fn.__name__, cap=cap)
    render(claude(), (200, 200), scale=7, tilt=0.45, name='vox-claude.gif', cap='CLAUDE|wrote this pack!!')
    for fn, sc, t in ((i_speaker, 6, 0.3), (sc55, 3.6, 0.75), (i_mt32, 3.4, 0.75), (lambda: disc(), 6, 1.0), (gamepad, 3.8, 0.9),
                      (joystick, 5, 0.45), (win98, 4.4, 0.35), (i_menu, 4.4, 0.35), (i_note, 4.2, 0.3), (i_bug, 5.5, 0.9),
                      (i_laptop, 4.2, 0.6), (i_tower, 3.8, 0.35), (i_trophy, 3.8, 0.4), (i_crate, 5.5, 0.6), (i_scroll, 4.2, 0.3),
                      (i_book, 4, 0.9), (i_palette, 4.4, 1.0), (claude, 4, 0.45), (usbstick, 5, 0.75)):
        n = fn.__name__.replace('i_', '') if fn.__name__ != '<lambda>' else 'disc'
        render(fn(), (72, 72), scale=sc, tilt=t, name='icon-%s.gif' % n)
    render(i_star(), (48, 48), frames=16, scale=3.2, tilt=0.15, bob=False, ms=70, name='star.gif', spin=True)
    pixel_icons()
    for fn, sc, t in ((tv, 6, 0.35), (camera, 6, 0.4), (gift, 6.5, 0.55), (toolbox, 5.5, 0.55), (arcade, 4.5, 0.3), (heart, 6.5, 0.2),
                      (i_tower, 5, 0.35), (i_trophy, 7, 0.4), (i_crate, 6.5, 0.6), (i_book, 5, 0.9), (i_scroll, 5.5, 0.3)):
        render(fn(), (150, 150), scale=sc, tilt=t, name='vox-sec-%s.gif' % fn.__name__.replace('i_', ''))
    for f in sorted(os.listdir(OUT)):
        if f.startswith('vox-'): print(f, os.path.getsize(os.path.join(OUT, f)))

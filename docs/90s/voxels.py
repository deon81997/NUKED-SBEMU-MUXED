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


def render(m, size, frames=12, scale=7, tilt=0.5, bg=(0, 0, 32), bob=True, ms=90, name='vox.gif', cap=None):
    size = (size[0] // 2, size[1] // 2); scale = scale / 2         # drawn at half size, stored 2x: chunky pixels like the sprites
    xs, ys, zs = zip(*m)
    cx, cy, cz = (max(xs) + min(xs) + 1) / 2, (max(ys) + min(ys) + 1) / 2, (max(zs) + min(zs) + 1) / 2
    W, H = size
    out = []
    for k in range(frames):
        a = 0.35 * math.sin(2 * math.pi * k / frames)               # a little dance, face stays in view
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
                polys.append((depth, [(W / 2 + p[0] * scale, H / 2 - (p[2] + dy / scale) * scale) for p in pts], shade(c, l)))
        im = Image.new('RGB', (W, H), KEY)
        d = ImageDraw.Draw(im)
        for depth, pts, col in sorted(polys, key=lambda t: -t[0]):
            d.polygon(pts, fill=col, outline=shade(col, 0.75))
        if cap:                                                     # pixel caption under the figure, like the nav buttons
            from PIL import ImageFont
            ft = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf', 8)
            c2 = Image.new('RGB', (W, H + 24), KEY); c2.paste(im, (0, 0)); im = c2
            d = ImageDraw.Draw(im); d.fontmode = '1'
            for j, (line, col) in enumerate(zip(cap.split('|'), ((255, 255, 0), (255, 255, 255)))):
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
    for f in sorted(os.listdir(OUT)):
        if f.startswith('vox-'): print(f, os.path.getsize(os.path.join(OUT, f)))

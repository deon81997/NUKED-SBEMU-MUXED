#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""More 90s page furniture for README.md: animated characters, section icons and a parade strip.

Characters:
  * Freedoom sprites (docs/90s/src/freedoom/*.png) - (c) Contributors to the Freedoom project, BSD licence,
    see src/FREEDOOM-COPYING.txt
  * Tux (src/tux-larry-ewing.gif) - by Larry Ewing, "freely usable as long as you acknowledge Larry as the
    original artist", see src/TUX-COPYING.txt
  * Flopsy and the Canvas Kid - original pack mascots drawn below

    python3 docs/90s/sprites90s.py
"""
import math, os, random
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'src')
FD = os.path.join(SRC, 'freedoom')
FONT = lambda s: ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', s)
random.seed(1997)


# ------------------------------------------------------------------ GIF writer with real transparency
SCALE = 2  # stored at 2x with hard pixel edges


def save_rgba(frames, name, ms=120, scale=SCALE):
    """frames: same-size RGBA images -> transparent looping GIF (index 255 = transparent), scaled up sharp"""
    if scale != 1:
        frames = [f.resize((f.width * scale, f.height * scale), Image.NEAREST) for f in frames]
    w, h = frames[0].size
    strip = Image.new('RGB', (w, h * len(frames)), (0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f.convert('RGB'), (0, i * h), f)
    pal = strip.quantize(colors=255, method=Image.MEDIANCUT)
    out = []
    for f in frames:
        q = f.convert('RGB').quantize(palette=pal, dither=Image.NONE)
        a = np.array(q, dtype=np.uint8)
        a[np.array(f)[:, :, 3] < 128] = 255
        p = Image.fromarray(a, 'P')
        pl = pal.getpalette()[:255 * 3] + [255, 0, 255]
        p.putpalette(pl)
        out.append(p)
    out[0].save(os.path.join(HERE, name), save_all=len(out) > 1, append_images=out[1:], duration=ms,
                loop=0, transparency=255, disposal=2, optimize=False)


def load(n):
    return Image.open(os.path.join(FD, n + '.png')).convert('RGBA')


def scaled(im, s):
    return im.resize((round(im.width * s), round(im.height * s)), Image.NEAREST)


def canvas_of(imgs, pad=2):
    w = max(i.width for i in imgs) + pad * 2
    h = max(i.height for i in imgs) + pad * 2
    out = []
    for i in imgs:
        c = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        c.alpha_composite(i, ((w - i.width) // 2, h - pad - i.height))   # stand on the floor, centred
        out.append(c)
    return out


def fd_anim(name, frames, s=1.0, ms=140):
    save_rgba(canvas_of([scaled(load(f), s) for f in frames]), name, ms)


# ------------------------------------------------------------------ Freedoom characters
def freedoom():
    fd_anim('fd-marine-walk.gif', ['playa1', 'playb1', 'playc1', 'playd1'], 1.0, 150)
    fd_anim('fd-marine-shoot.gif', ['playa1', 'playe1', 'playf1', 'playe1'], 1.0, 160)
    fd_anim('fd-serpentipede.gif', ['trooa1', 'troob1', 'trooc1', 'trood1'], 1.0, 150)
    fd_anim('fd-serpentipede-attack.gif', ['trooe1', 'troof1', 'troog1', 'trooa1'], 1.0, 180)
    fd_anim('fd-zombie.gif', ['possa1', 'possb1', 'possc1', 'possd1'], 1.0, 150)
    fd_anim('fd-worm.gif', ['sarga1', 'sargb1', 'sargc1', 'sargd1'], 1.0, 130)
    fd_anim('fd-trilobite.gif', ['heada1', 'headb1', 'headc1', 'headd1', 'heada1', 'heada1'], 1.0, 170)
    fd_anim('fd-soulsphere.gif', ['soula0', 'soulb0', 'soulc0', 'sould0'], 1.0, 120)
    fd_anim('fd-potion.gif', ['bon1a0', 'bon1b0', 'bon1c0', 'bon1d0'], 1.5, 140)
    fd_anim('fd-keycard.gif', ['bkeya0', 'bkeyb0'], 1.5, 400)
    # the hatchling floats up and down
    sk = [load('skula1'), load('skulb1')]
    frames = []
    for k in range(8):
        c = Image.new('RGBA', (34, 52), (0, 0, 0, 0))
        c.alpha_composite(sk[k % 2], (2, 6 + round(5 * math.sin(k / 8 * 2 * math.pi))))
        frames.append(c)
    save_rgba(frames, 'fd-hatchling.gif', 110)


# ------------------------------------------------------------------ Tux waddles
def tux():
    t = Image.open(os.path.join(SRC, 'tux-larry-ewing.gif')).convert('RGBA')
    t = t.resize((t.width * 150 // t.height, 150), Image.LANCZOS)
    a = np.array(t); a[:, :, 3] = np.where(a[:, :, 3] > 100, 255, 0); t = Image.fromarray(a)
    frames = []
    for ang, dy in ((-7, 0), (0, -3), (7, 0), (0, -3)):
        r = t.rotate(ang, resample=Image.NEAREST, expand=True)
        c = Image.new('RGBA', (190, 172), (0, 0, 0, 0))
        c.alpha_composite(r, ((190 - r.width) // 2, 172 - r.height + dy * 2 - 4))
        frames.append(c)
    save_rgba(frames, 'tux-waddle.gif', 220, scale=1)


# ------------------------------------------------------------------ original mascots
FLOPSY = [
    "..NNNNNNNNNNNN..",
    "..NNSSSSSSNNNN..",
    "..NNSSSNNSNNNN..",
    "..NNSSSSSSNNNN..",
    "..NNNNNNNNNNNN..",
    "..NWWWWWWWWWWN..",
    "..NWWKWWWWKWWN..",
    "..NWWKWWWWKWWN..",
    "..NWWWWRRWWWWN..",
    "..NWWWWWWWWWWN..",
    "..NNNNNNNNNNNN..",
]
LEGS = [["....L......L....", "...L........L...", "..BB........BB.."],
        ["......L..L......", "......L..L......", ".....BB..BB....."]]
ARMS = [{5: (1, 14), 6: (0, 15)}, {7: (1, 14), 8: (0, 15)}]
COL = {'N': (30, 40, 150), 'S': (190, 190, 200), 'W': (255, 255, 255), 'K': (0, 0, 0), 'R': (230, 30, 60),
       'L': (20, 20, 20), 'B': (220, 0, 0), 'A': (30, 40, 150)}


def from_map(rows, s=4):
    h, w = len(rows), max(len(r) for r in rows)
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in COL:
                im.putpixel((x, y), COL[ch] + (255,))
    return im.resize((w * s, h * s), Image.NEAREST)


def flopsy():
    frames = []
    for k in range(4):
        rows = [list(r) for r in FLOPSY + LEGS[k % 2]]
        for y, xs in ARMS[k % 2].items():
            for x in xs:
                rows[y][x] = 'A'
        if k == 3:                                   # blink
            rows[6] = list("..NWWWWWWWWWWN.."); rows[7] = list("..NWWKWWWWKWWN..")
        frames.append(from_map([''.join(r) for r in rows]))
    save_rgba(frames, 'mascot-flopsy.gif', 200)


def canvas_kid():
    frames = []
    font = FONT(9)
    for k in range(6):
        c = Image.new('RGBA', (96, 76), (0, 0, 0, 0))
        d = ImageDraw.Draw(c); d.fontmode = '1'
        d.rectangle([4, 8, 91, 50], fill=(45, 45, 50), outline=(15, 15, 15))           # module body
        d.rectangle([4, 8, 91, 12], fill=(70, 70, 78))
        d.rectangle([10, 17, 58, 42], fill=(120, 190, 60))                              # LCD
        for i in range(8):                                                              # level bars
            hgt = 3 + int(14 * abs(math.sin(k * 0.9 + i * 1.3)))
            d.rectangle([13 + i * 5, 40 - hgt, 16 + i * 5, 40], fill=(30, 60, 20))
        d.ellipse([66, 20, 84, 38], fill=(160, 160, 165), outline=(20, 20, 20))         # big knob
        a = k * 1.1
        d.line([75, 29, 75 + 7 * math.cos(a), 29 + 7 * math.sin(a)], fill=(20, 20, 20), width=2)
        d.rectangle([62, 42, 66, 46], fill=(255, 40, 40) if k % 2 else (90, 0, 0))     # power LED
        d.ellipse([18, 1, 26, 9], fill=(255, 255, 255)); d.ellipse([20 + k % 2 * 2, 3, 24 + k % 2 * 2, 7], fill=(0, 0, 0))
        d.ellipse([70, 1, 78, 9], fill=(255, 255, 255)); d.ellipse([72 + k % 2 * 2, 3, 76 + k % 2 * 2, 7], fill=(0, 0, 0))
        lx = [(28, 64), (68, 64)] if k % 2 else [(34, 64), (62, 64)]
        for (x, y), x0 in zip(lx, (30, 66)):                                           # legs + shoes
            d.line([x0, 50, x, y], fill=(20, 20, 20), width=3)
            d.ellipse([x - 7, y - 2, x + 5, y + 6], fill=(240, 160, 0), outline=(20, 20, 20))
        frames.append(c)
    save_rgba(frames, 'mascot-canvaskid.gif', 170)


# ------------------------------------------------------------------ animated section icons (32x32)
def icon(name, draw, n, ms=180):
    frames = []
    for k in range(n):
        c = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
        d = ImageDraw.Draw(c)
        draw(d, k)
        frames.append(c)
    save_rgba(frames, name, ms)


def icons():
    def news(d, k):
        d.rectangle([3, 5, 28, 27], fill=(250, 250, 240), outline=(0, 0, 0))
        d.rectangle([5, 7, 26, 11], fill=(220, 0, 0) if k % 2 else (0, 0, 0))
        for y in range(14, 26, 3):
            d.line([6, y, 15, y], fill=(80, 80, 80)); d.line([18, y, 25, y], fill=(80, 80, 80))

    def sparkle(d, k):
        r = (6, 10, 14, 10)[k]
        c = [(255, 230, 0), (255, 255, 255), (255, 150, 0), (255, 255, 255)][k]
        d.polygon([(16, 16 - r), (18, 14), (16 + r, 16), (18, 18), (16, 16 + r), (14, 18), (16 - r, 16), (14, 14)], fill=c)

    def chip(d, k):
        d.rectangle([7, 7, 24, 24], fill=(40, 40, 40))
        for i in range(4):
            for p in (9 + i * 4,):
                d.line([p, 2, p, 6], fill=(200, 200, 200)); d.line([p, 25, p, 29], fill=(200, 200, 200))
                d.line([2, p, 6, p], fill=(200, 200, 200)); d.line([25, p, 29, p], fill=(200, 200, 200))
        d.ellipse([12, 12, 19, 19], fill=(0, 255, 0) if k % 2 else (0, 90, 0))

    def rocket(d, k):
        y = 4 - (k % 3)
        d.polygon([(16, y), (21, y + 8), (21, y + 18), (11, y + 18), (11, y + 8)], fill=(220, 220, 230), outline=(0, 0, 0))
        d.ellipse([14, y + 8, 18, y + 12], fill=(0, 120, 255))
        d.polygon([(11, y + 14), (6, y + 20), (11, y + 18)], fill=(220, 0, 0))
        d.polygon([(21, y + 14), (26, y + 20), (21, y + 18)], fill=(220, 0, 0))
        fl = 5 + (k % 3) * 2
        d.polygon([(12, y + 19), (16, y + 19 + fl), (20, y + 19)], fill=(255, 160, 0))

    def pad(d, k):
        d.rounded_rectangle([2, 10, 29, 24], 6, fill=(110, 110, 120), outline=(0, 0, 0))
        d.rectangle([6, 15, 12, 17], fill=(30, 30, 30)); d.rectangle([8, 13, 10, 19], fill=(30, 30, 30))
        for i, (x, y) in enumerate(((22, 13), (25, 16), (19, 16), (22, 19))):
            on = i == k % 4
            d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 255, 0) if on else (200, 0, 0))

    def speaker(d, k):
        d.rectangle([4, 12, 9, 20], fill=(60, 60, 60)); d.polygon([(9, 12), (16, 6), (16, 26), (9, 20)], fill=(60, 60, 60))
        for i in range(k + 1):
            r = 5 + i * 5
            d.arc([16 - r, 16 - r, 16 + r, 16 + r], -45, 45, fill=(0, 120, 255), width=2)

    def camera(d, k):
        d.rectangle([3, 10, 28, 26], fill=(50, 50, 55)); d.rectangle([7, 7, 13, 10], fill=(50, 50, 55))
        d.ellipse([11, 12, 23, 24], fill=(150, 200, 255), outline=(0, 0, 0))
        if k == 1:
            d.polygon([(25, 2), (27, 7), (31, 7), (28, 10), (29, 15), (25, 12), (21, 15), (22, 10), (19, 7), (23, 7)], fill=(255, 255, 120))

    def computer(d, k):
        d.rectangle([3, 3, 28, 21], fill=(220, 215, 200), outline=(0, 0, 0)); d.rectangle([6, 6, 25, 18], fill=(0, 0, 0))
        d.text((7, 6), 'C:\\', fill=(190, 190, 190), font=FONT(8))
        if k % 2: d.rectangle([20, 15, 23, 16], fill=(190, 190, 190))
        d.rectangle([10, 22, 21, 24], fill=(200, 195, 180)); d.rectangle([4, 25, 27, 29], fill=(220, 215, 200), outline=(0, 0, 0))

    def heart(d, k):
        s = (1.0, 1.25, 1.0, 1.1)[k]
        pts = [(16 + 13 * s * (16 * math.sin(t) ** 3) / 16, 15 - 13 * s * (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)) / 16)
               for t in np.linspace(0, 2 * math.pi, 40)]
        d.polygon(pts, fill=(230, 0, 60))

    def globe(d, k):
        d.ellipse([3, 3, 28, 28], fill=(30, 90, 220), outline=(0, 0, 0))
        for i in range(3):
            x = 3 + ((k * 4 + i * 9) % 25)
            d.ellipse([x, 8 + i * 5, x + 6, 13 + i * 5], fill=(40, 180, 60))
        d.ellipse([3, 3, 28, 28], outline=(0, 0, 0))

    def disc(d, k):
        d.ellipse([2, 2, 29, 29], fill=(200, 200, 215), outline=(80, 80, 90))
        a = k * math.pi / 3
        for j in range(3):
            aa = a + j * 2.1
            d.pieslice([2, 2, 29, 29], math.degrees(aa), math.degrees(aa) + 25, fill=(rainbow_c(j)))
        d.ellipse([12, 12, 19, 19], fill=(255, 255, 255), outline=(80, 80, 90))

    icon('ic-news.gif', news, 2, 400)
    icon('ic-sparkle.gif', sparkle, 4, 150)
    icon('ic-chip.gif', chip, 2, 350)
    icon('ic-rocket.gif', rocket, 3, 120)
    icon('ic-pad.gif', pad, 4, 250)
    icon('ic-speaker.gif', speaker, 3, 250)
    icon('ic-camera.gif', camera, 4, 250)
    icon('ic-computer.gif', computer, 2, 450)
    icon('ic-heart.gif', heart, 4, 160)
    icon('ic-globe.gif', globe, 6, 180)
    icon('ic-cd.gif', disc, 6, 100)


def rainbow_c(j):
    return [(255, 120, 200), (120, 255, 220), (255, 240, 120)][j % 3]


# ------------------------------------------------------------------ the parade: a marquee made of sprites
def parade():
    w, h = 640, 90
    cast = [
        ([scaled(load(f), 1) for f in ('playa1', 'playb1', 'playc1', 'playd1')], 0),
        ([scaled(load(f), 1) for f in ('trooa1', 'troob1', 'trooc1', 'trood1')], 70),
        ([scaled(load(f), 1) for f in ('skula1', 'skulb1')], 140),
        ([scaled(load(f), 1) for f in ('sarga1', 'sargb1', 'sargc1', 'sargd1')], 200),
        ([scaled(load(f), 1) for f in ('possa1', 'possb1', 'possc1', 'possd1')], 280),
    ]
    flop = Image.open(os.path.join(HERE, 'mascot-flopsy.gif'))
    fl = []
    for i in range(flop.n_frames):
        flop.seek(i); f2 = flop.convert('RGBA'); fl.append(f2.resize((f2.width // 2, f2.height // 2), Image.NEAREST))
    cast.append((fl, 350))
    step, nfr = 8, 80
    span = w + 400
    frames = []
    font = FONT(12)
    for k in range(nfr):
        c = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(c)
        d.line([0, h - 3, w, h - 3], fill=(120, 120, 120), width=2)
        for imgs, off in cast:
            x = (k * step * span // (nfr * step) * 1 + w - off) % span - 200
            x = int(((k / nfr) * span + w - off) % span) - 200
            fr = imgs[(k // 2) % len(imgs)]
            y = h - 4 - fr.height
            if imgs is cast[2][0]:
                y = 10 + int(6 * math.sin(k / 3))
            c.alpha_composite(fr, (x - fr.width // 2, y)) if -fr.width < x - fr.width // 2 < w else None
        frames.append(c)
    # the cast walks right: flip so they face their direction? Freedoom front sprites face the viewer - fine.
    save_rgba(frames, 'parade.gif', 70)


if __name__ == '__main__':
    freedoom(); tux(); flopsy(); canvas_kid(); icons(); parade()
    for f in sorted(os.listdir(HERE)):
        if f.endswith('.gif') and (f.startswith(('fd-', 'tux', 'mascot', 'ic-', 'parade'))):
            print(f, os.path.getsize(os.path.join(HERE, f)))

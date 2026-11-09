#!/usr/bin/env python3
"""Draws the 90s home-page graphics used by README.md (all original, made with Pillow).

    python3 docs/90s/make90s.py      -> writes the GIFs next to this script
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.dirname(os.path.abspath(__file__))
FD = '/usr/share/fonts/truetype/dejavu/'
F = lambda n, s: ImageFont.truetype(FD + n, s)
random.seed(1996)


def save(frames, name, ms=120):
    frames = [f.convert('P', palette=Image.ADAPTIVE, colors=64) if f.mode != 'P' else f for f in frames]
    frames[0].save(os.path.join(OUT, name), save_all=len(frames) > 1, append_images=frames[1:],
                   duration=ms, loop=0, optimize=True, disposal=2)


def crisp(d):
    d.fontmode = '1'  # no anti-aliasing: real 1997 pixels
    return d


def rainbow(t):
    """hue 0..1 -> RGB"""
    i = int(t * 6) % 6
    f = t * 6 - int(t * 6)
    q, p = int(255 * (1 - f)), int(255 * f)
    return [(255, p, 0), (q, 255, 0), (0, 255, p), (0, q, 255), (p, 0, 255), (255, 0, q)][i]


def starfield(w, h, n=60):
    return [(random.randrange(w), random.randrange(h), random.choice([1, 1, 1, 2])) for _ in range(n)]


# ---------------------------------------------------------------- WordArt title with twinkling stars
def title():
    w, h = 720, 120
    stars = starfield(w, h, 90)
    font = F('DejaVuSerif-Bold.ttf', 64)
    text = 'NUKED SB-EMU'
    frames = []
    for k in range(12):
        im = Image.new('RGB', (w, h), (0, 0, 32))
        d = crisp(ImageDraw.Draw(im))
        for i, (x, y, s) in enumerate(stars):
            if (i + k) % 5:
                c = 255 if (i + k) % 3 else 140
                d.rectangle([x, y, x + s - 1, y + s - 1], fill=(c, c, c))
        tw = d.textlength(text, font=font)
        x0 = (w - tw) / 2
        x = x0
        for j, ch in enumerate(text):
            y = 20 + 8 * math.sin((j + k) * 0.6)
            d.text((x + 4, y + 4), ch, font=font, fill=(60, 0, 90))            # drop shadow
            d.text((x, y), ch, font=font, fill=rainbow(((j + k) / 12) % 1))
            x += d.textlength(ch, font=font)
        frames.append(im)
    save(frames, 'title.gif', 110)


# ---------------------------------------------------------------- "under construction" sign
def construction():
    w, h = 300, 60
    font = F('DejaVuSans-Bold.ttf', 15)
    frames = []
    for k in range(8):
        im = Image.new('RGB', (w, h), (255, 220, 0))
        d = crisp(ImageDraw.Draw(im))
        for s in range(-h, w + h, 20):           # moving hazard stripes top and bottom
            for y0 in (0, h - 12):
                xs = s + k * 5
                d.polygon([(xs, y0), (xs + 10, y0), (xs + 22, y0 + 12), (xs + 12, y0 + 12)], fill=(0, 0, 0))
        d.rectangle([0, 14, w, h - 15], fill=(255, 220, 0))
        msg = 'UNDER CONSTRUCTION'
        d.text(((w - d.textlength(msg, font=font)) / 2, 20), msg, font=font,
               fill=(0, 0, 0) if k % 2 else (200, 0, 0))
        # little worker: hard hat + shovel
        d.ellipse([12, 17, 30, 35], fill=(255, 120, 0), outline=(0, 0, 0))
        d.line([w - 30, 18, w - 16 + (k % 2) * 3, 40], fill=(90, 50, 0), width=3)
        d.rectangle([w - 20 + (k % 2) * 3, 38, w - 12 + (k % 2) * 3, 44], fill=(120, 120, 120))
        frames.append(im)
    save(frames, 'construction.gif', 150)


# ---------------------------------------------------------------- blinking NEW!
def new():
    w, h = 48, 20
    font = F('DejaVuSans-Bold.ttf', 13)
    frames = []
    for k in range(2):
        im = Image.new('RGB', (w, h), (255, 255, 255))
        d = crisp(ImageDraw.Draw(im))
        if k == 0:
            d.polygon([(0, 10), (6, 0), (42, 0), (48, 10), (42, 19), (6, 19)], fill=(255, 0, 0))
            d.text((7, 2), 'NEW!', font=font, fill=(255, 255, 0))
        frames.append(im)
    save(frames, 'new.gif', 450)


# ---------------------------------------------------------------- 88x31 buttons
def button(name, top, bottom, bg, fg, left=None, anim=None):
    frames = []
    nf = 1 if anim is None else 6
    small, big = F('DejaVuSansCondensed-Bold.ttf', 9), F('DejaVuSansCondensed-Bold.ttf', 11)
    for k in range(nf):
        im = Image.new('RGB', (88, 31), bg)
        d = crisp(ImageDraw.Draw(im))
        d.rectangle([0, 0, 87, 30], outline=(255, 255, 255))
        d.line([(87, 0), (87, 30), (0, 30)], fill=(64, 64, 64))
        x = 3
        if left:
            d.rectangle([3, 3, 26, 27], fill=left[0])
            d.text((15 - d.textlength(left[1], font=big) / 2, 8), left[1], font=big, fill=left[2])
            x = 30
        cw = 88 - x - 3
        col = fg if anim is None else rainbow(k / nf)
        d.text((x + (cw - d.textlength(top, font=small)) / 2, 4), top, font=small, fill=col)
        d.text((x + (cw - d.textlength(bottom, font=big)) / 2, 15), bottom, font=big, fill=fg)
        frames.append(im)
    save(frames, name, 200)


def buttons():
    button('btn-ie.gif', 'viewed with', 'IE 4.0', (0, 0, 128), (255, 255, 255), ((0, 102, 204), 'e', (255, 255, 0)))
    button('btn-800.gif', 'best viewed at', '800 x 600', (0, 0, 0), (0, 255, 0))
    button('btn-notepad.gif', 'made with', 'NOTEPAD', (255, 255, 255), (0, 0, 0), ((0, 0, 128), 'N', (255, 255, 255)))
    button('btn-dos.gif', 'powered by', 'DOS 7.1', (0, 0, 0), (192, 192, 192), ((128, 128, 128), 'C:\\', (0, 0, 0)))
    button('btn-sc55.gif', 'SC-55 synth', 'INSIDE', (40, 40, 40), (255, 160, 0), ((0, 0, 0), '55', (255, 160, 0)), anim=True)
    button('btn-gpl.gif', 'free software', 'GPL 2.0+', (128, 0, 0), (255, 255, 255))
    button('btn-download.gif', 'click here to', 'DOWNLOAD!', (0, 128, 0), (255, 255, 0), anim=True)
    button('btn-sb16.gif', 'sound by', 'SB16', (0, 0, 0), (255, 64, 64), ((200, 0, 0), 'SB', (255, 255, 255)))


# ---------------------------------------------------------------- odometer "counter" (it counts releases)
def counter(n=71):
    digits = '%06d' % n
    font = F('DejaVuSansMono-Bold.ttf', 18)
    w, h = 6 * 16 + 4, 26
    im = Image.new('RGB', (w, h), (0, 0, 0))
    d = crisp(ImageDraw.Draw(im))
    for i, c in enumerate(digits):
        x = 2 + i * 16
        d.rectangle([x, 2, x + 14, h - 3], fill=(30, 30, 30), outline=(90, 90, 90))
        d.line([x, h // 2, x + 14, h // 2], fill=(0, 0, 0))
        d.text((x + 2, 1), c, font=font, fill=(0, 255, 0))
    save([im], 'counter.gif')


# ---------------------------------------------------------------- rainbow divider bar
def bar():
    w, h = 600, 6
    frames = []
    for k in range(10):
        im = Image.new('RGB', (w, h))
        d = ImageDraw.Draw(im)
        for x in range(w):
            d.line([x, 0, x, h], fill=rainbow(((x / w) + k / 10) % 1))
        frames.append(im)
    save(frames, 'bar.gif', 100)


# ---------------------------------------------------------------- spinning-ish floppy and mailbox icons
def floppy():
    frames = []
    for k in range(4):
        im = Image.new('RGB', (32, 32), (255, 255, 255))
        d = ImageDraw.Draw(im)
        d.polygon([(2, 2), (27, 2), (30, 5), (30, 30), (2, 30)], fill=(30, 30, 140))
        d.rectangle([8, 2, 24, 11], fill=(200, 200, 200))
        d.rectangle([18 - k % 2 * 4, 3, 21 - k % 2 * 4, 10], fill=(30, 30, 140))
        d.rectangle([6, 16, 26, 30], fill=(255, 255, 255))
        for y in (19, 22, 25):
            d.line([8, y, 24, y], fill=(255, 0, 0) if (y + k) % 2 else (0, 0, 255))
        frames.append(im)
    save(frames, 'floppy.gif', 300)


def mailbox():
    frames = []
    for k in range(2):
        im = Image.new('RGB', (32, 32), (255, 255, 255))
        d = ImageDraw.Draw(im)
        d.rectangle([14, 18, 17, 31], fill=(110, 70, 30))
        d.rounded_rectangle([3, 6, 28, 19], 5, fill=(30, 60, 200))
        d.rectangle([3, 12, 10, 19], fill=(20, 40, 150))
        d.line([24, 4, 24, 10 if k else 6], fill=(255, 0, 0), width=2)
        d.rectangle([21, 3 if k else 1, 27, 5 if k else 3], fill=(255, 0, 0))
        frames.append(im)
    save(frames, 'mail.gif', 500)


if __name__ == '__main__':
    title(); construction(); new(); buttons(); counter(); bar(); floppy(); mailbox()
    for f in sorted(os.listdir(OUT)):
        if f.endswith('.gif'):
            print(f, os.path.getsize(os.path.join(OUT, f)))

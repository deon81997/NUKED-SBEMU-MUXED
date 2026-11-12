#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""Hand-made looking decorations for README.md (all original, made with Pillow): sticky notes in wobbly
marker writing, doodled arrows, and spinning ball bullets.

    python3 docs/90s/decor.py      -> writes note-*.gif, doodle-*.gif and ball-*.gif next to this script
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.dirname(os.path.abspath(__file__))
KEY = (255, 0, 255)
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf'


def save(frames, name, ms=120):
    sheet = Image.new('RGB', (frames[0].width * len(frames), frames[0].height))
    for i, f in enumerate(frames): sheet.paste(f, (i * f.width, 0))
    pal = sheet.quantize(colors=48, dither=Image.NONE)
    p = pal.getpalette()
    ki = min(range(len(p) // 3), key=lambda i: sum(abs(p[i * 3 + j] - KEY[j]) for j in range(3)))
    q = [f.quantize(palette=pal, dither=Image.NONE) for f in frames]
    q[0].save(os.path.join(OUT, name), save_all=len(q) > 1, append_images=q[1:], duration=ms, loop=0, transparency=ki, disposal=2)


def marker(d, xy, text, size, fill, seed, wobble=1.0):
    """'handwriting': every letter a bit bigger or smaller, up or down, and leaning its own way"""
    r = random.Random(seed)
    x, y = xy
    for ch in text:
        f = ImageFont.truetype(FONT, int(size * (0.9 + r.random() * 0.25)))
        g = Image.new('L', (size * 2, size * 2), 0)
        ImageDraw.Draw(g).text((size // 2, size // 3), ch, font=f, fill=255)
        g = g.rotate(r.uniform(-9, 9) * wobble, resample=Image.BICUBIC)
        d._image.paste(Image.new('RGB', g.size, fill), (int(x - size // 2), int(y - size // 3 + r.uniform(-1.5, 1.5) * wobble)), g.point(lambda v: 255 if v > 110 else 0))
        x += f.getlength(ch) * (0.95 + r.random() * 0.1)
    return x


def note(lines, name, paper=(255, 240, 120), ink=(30, 40, 160), angle=-4, seed=1, w=None):
    """sticky note with a strip of tape; it flutters a little"""
    lh = 26
    w = w or 40 + int(max(ImageFont.truetype(FONT, 21).getlength(l) for l in lines))
    h = 30 + lh * len(lines)
    frames = []
    for k in range(4):
        im = Image.new('RGB', (w, h), paper)
        d = ImageDraw.Draw(im)
        for y in range(h): d.line([(0, y), (w, y)], fill=tuple(max(0, c - y // 12) for c in paper))   # a bit darker at the bottom
        d.polygon([(w - 22, h), (w, h - 22), (w, h)], fill=tuple(c - 50 for c in paper))           # curled corner
        for i, ln in enumerate(lines):
            marker(d, (14, 16 + i * lh), ln, 19, ink, seed * 100 + i)
        tape = Image.new('RGB', (70, 18), (235, 235, 220))
        big = Image.new('RGB', (w + 40, h + 40), KEY)
        big.paste(im, (20, 26))
        big.paste(tape, (w // 2 - 15, 16))
        a = angle + (0.8 if k in (1, 2) else 0)
        big = big.rotate(a, resample=Image.NEAREST, fillcolor=KEY, expand=False)
        frames.append(big)
    save(frames, name, 260)


def doodle(text, name, flip=False, ink=(255, 80, 200), seed=3):
    """scribbled arrow with a word next to it, drawn in two strokes (it 'wiggles')"""
    W, H = 220, 90
    frames = []
    for k in range(3):
        r = random.Random(seed + k)
        im = Image.new('RGB', (W, H), KEY)
        d = ImageDraw.Draw(im)
        pts = [(20 + t * 1.6 + r.uniform(-1, 1), 60 - 30 * math.sin(t / 110 * math.pi) + r.uniform(-1, 1)) for t in range(0, 111, 6)]
        d.line(pts, fill=ink, width=4, joint='curve')
        ex, ey = pts[-1]
        d.line([(ex, ey), (ex - 14, ey - 10 + r.uniform(-2, 2))], fill=ink, width=4)
        d.line([(ex, ey), (ex - 6, ey - 18 + r.uniform(-2, 2))], fill=ink, width=4)
        if flip: im = im.transpose(Image.FLIP_LEFT_RIGHT); d = ImageDraw.Draw(im)
        marker(d, (10 if not flip else 60, 4), text, 20, ink, seed, wobble=1.3)
        frames.append(im)
    save(frames, name, 180)


def ball(name, col):
    """the classic spinning 3D ball bullet"""
    frames = []
    for k in range(8):
        im = Image.new('RGB', (14, 14), KEY)
        d = ImageDraw.Draw(im)
        d.ellipse([0, 0, 13, 13], fill=tuple(c // 2 for c in col))
        d.ellipse([1, 1, 11, 11], fill=col)
        x = 3 + int(3 * math.cos(k / 8 * 2 * math.pi))
        d.ellipse([x, 3, x + 3, 6], fill=(255, 255, 255))
        d.arc([2, 2, 11, 11], 200 + k * 45, 260 + k * 45, fill=tuple(min(255, c + 80) for c in col))
        frames.append(im.resize((28, 28), Image.NEAREST))
    save(frames, name, 90)


if __name__ == '__main__':
    note(['new stuff almost', 'every day!! check', 'back soon :)'], 'note-news.gif', seed=1, angle=-5)
    note(['my 366 MHz Celeron', 'runs it!!', '(the SC-55 barely)'], 'note-run.gif', paper=(170, 230, 255), seed=2, angle=4)
    note(['unzip it, run SETUP,', 'done. REALLY!!'], 'note-start.gif', paper=(190, 255, 170), seed=3, angle=-3)
    note(['my 360 pad works', 'in DOOM!!! no joke'], 'note-pads.gif', paper=(255, 190, 220), seed=4, angle=5)
    note(['all real screens,', 'no photoshop :)'], 'note-shots.gif', seed=5, angle=3)
    note(['it\'s FREE!!', 'tell ur friends'], 'note-download.gif', paper=(255, 210, 150), seed=6, angle=-6)
    note(['pls sign it,', 'I read every one!!'], 'note-guest.gif', paper=(170, 230, 255), seed=7, angle=4)
    note(['couldn\'t have done', 'it without them <3'], 'note-thanks.gif', paper=(255, 190, 220), seed=8, angle=-4)
    note(['take them!! that\'s', 'what GPL is for'], 'note-drivers.gif', paper=(190, 255, 170), seed=9, angle=3)
    doodle('click me!!', 'doodle-click.gif', seed=11)
    doodle('click the parts!', 'doodle-parts.gif', flip=True, ink=(80, 220, 255), seed=12)
    doodle('the new one!!', 'doodle-new.gif', ink=(255, 200, 40), seed=13)
    for n, c in (('red', (220, 30, 30)), ('blue', (40, 90, 230)), ('green', (30, 170, 60)), ('gold', (230, 170, 20))):
        ball('ball-%s.gif' % n, c)

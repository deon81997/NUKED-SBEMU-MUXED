#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""The README's site-map buttons, the "back to top" button and the scrolling news ticker (original, Pillow).

    python3 docs/90s/nav90s.py       -> nav-*.gif, btn-top.gif, ticker-hd.gif (and the counter) next to this script
"""
import os
from PIL import Image, ImageDraw
import make90s as m

# file, small line, big line, background, text colour, (icon box colour, icon text, icon colour), rainbow?
NAV = [
    ('nav-news.gif',     "what's",      'NEW!!',      (128, 0, 0),    (255, 255, 0),   ((255, 255, 0), '!', (200, 0, 0)), True),
    ('nav-what.gif',     'what is',     'THIS??',     (0, 0, 128),    (255, 255, 255), ((255, 255, 255), '?', (0, 0, 128)), False),
    ('nav-features.gif', 'cool',        'FEATURES',   (0, 96, 0),     (255, 255, 255), ((255, 215, 0), '*', (0, 96, 0)), False),
    ('nav-run.gif',      'will it',     'RUN??',      (64, 0, 96),    (255, 200, 255), ((0, 0, 0), 'CPU', (0, 255, 0)), False),
    ('nav-lab.gif',      'my test',     'LAB',        (0, 64, 96),    (160, 255, 255), ((192, 192, 192), 'PC', (0, 0, 0)), False),
    ('nav-start.gif',    'getting',     'STARTED',    (0, 0, 0),      (192, 192, 192), ((128, 128, 128), 'C:\\', (0, 0, 0)), False),
    ('nav-pads.gif',     'USB',         'GAMEPADS',   (32, 32, 32),   (120, 255, 120), ((60, 160, 60), '+', (255, 255, 255)), False),
    ('nav-chips.gif',    'sound',       'CHIPS',      (0, 80, 40),    (255, 255, 160), ((0, 0, 0), 'PCI', (255, 200, 0)), False),
    ('nav-shots.gif',    'screen',      'SHOTS!!',    (96, 48, 0),    (255, 230, 160), ((40, 40, 40), '[ ]', (255, 255, 255)), False),
    ('nav-fame.gif',     'hall of',     'FAME',       (120, 90, 0),   (255, 255, 200), ((255, 215, 0), '#1', (120, 60, 0)), True),
    ('nav-claude.gif',   'built with',  'CLAUDE',     (96, 40, 20),   (255, 220, 190), ((217, 119, 87), '*', (255, 255, 255)), False),
    ('nav-home.gif',     'made for',    'THIS PACK',   (0, 72, 72),    (255, 255, 255), ((255, 255, 255), '%', (0, 72, 72)), False),
    ('nav-download.gif', 'download',    'ZONE',       (0, 128, 0),    (255, 255, 0),   ((255, 255, 0), 'v', (0, 128, 0)), True),
    ('nav-guest.gif',    'sign my',     'G-BOOK',  (128, 0, 64),   (255, 200, 230), ((255, 255, 255), '@', (128, 0, 64)), False),
    ('nav-source.gif',   'for nerds:',  'SOURCE',     (0, 0, 0),      (0, 255, 0),     ((0, 64, 0), '</>', (0, 255, 0)), False),
    ('nav-thanks.gif',   'thanks to my', 'FRIENDS',   (90, 0, 90),    (255, 255, 255), ((255, 80, 160), '<3', (255, 255, 255)), False),
]


def ticker(text, width=1200, height=64, step=20):
    """an LED news ticker, drawn at 4x (1200x64, shown 600 wide): smooth amber letters behind a dot-matrix
    grid, a soft glow, scrolling right to left"""
    from PIL import ImageFilter, ImageChops
    font = m.F('DejaVuSansMono-Bold.ttf', 52)
    probe = ImageDraw.Draw(Image.new('L', (1, 1)))
    tw = int(probe.textlength(text, font=font))
    strip = Image.new('L', (tw + width, height), 0)
    ImageDraw.Draw(strip).text((width, height // 2), text, font=font, fill=255, anchor='lm')
    glow = strip.filter(ImageFilter.GaussianBlur(3))
    # LED grid: 4x4 cells, square 3x3 dots, 1-pixel dark gaps (big dots keep the text easy to read)
    cell = Image.new('L', (4, 4), 0)
    for yy in range(3):
        for xx in range(3):
            cell.putpixel((xx, yy), 255)
    grid = Image.new('L', (width, height), 0)
    for yy in range(0, height, 4):
        for xx in range(0, width, 4):
            grid.paste(cell, (xx, yy))
    amber = Image.new('RGB', (width, height), (255, 196, 40))
    dim = Image.new('RGB', (width, height), (22, 12, 0))   # unlit LEDs, faintly visible
    black = Image.new('RGB', (width, height), (0, 0, 0))
    frames = []
    for x in range(0, tw + width - width // 3, step):
        lit = strip.crop((x, 0, x + width, height))
        dots = Image.composite(Image.composite(amber, dim, lit), black, grid)   # light shows through the dots only
        halo = Image.composite(Image.new('RGB', (width, height), (70, 32, 0)), black,
                               glow.crop((x, 0, x + width, height)))
        im = ImageChops.add(dots, halo)
        ImageDraw.Draw(im).rectangle([0, 0, width - 1, height - 1], outline=(90, 90, 90), width=2)
        frames.append(im)
    pal = frames[len(frames) // 2].quantize(colors=16, method=Image.MEDIANCUT)
    frames = [f.quantize(palette=pal, dither=Image.NONE) for f in frames]
    frames[0].save(os.path.join(m.OUT, 'ticker-hd.gif'), save_all=True, append_images=frames[1:],
                   duration=50, loop=0, optimize=True, disposal=2)


if __name__ == '__main__':
    for f, top, big, bg, fg, icon, anim in NAV:
        m.button(f, top, big, bg, fg, icon, anim=True if anim else None)
    m.button('btn-top.gif', 'back to', 'TOP ^', (0, 0, 96), (255, 255, 255), ((255, 255, 0), '^', (0, 0, 96)))
    m.counter(85)
    ticker("*** NEW!! beta.92: an empty CD drive no longer freezes the PC *** CD images from ANY drive, with a home folder *** "
           "MIDPLAY plays folders with thousands of MIDI files *** Port 220 says hi!! ***")
    for f in sorted(os.listdir(m.OUT)):
        if f.startswith(('nav-', 'btn-top', 'ticker', 'counter')):
            p = os.path.join(m.OUT, f); print(f, Image.open(p).size, os.path.getsize(p))

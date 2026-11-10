#!/usr/bin/env python3
"""The README's site-map buttons, the "back to top" button and the scrolling news ticker (original, Pillow).

    python3 docs/90s/nav90s.py       -> nav-*.gif, btn-top.gif, ticker.gif (and the counter) next to this script
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


def ticker(text, width=300, step=5):
    """an LED news ticker: amber dot-matrix-ish letters scrolling right to left"""
    font = m.F('DejaVuSansMono-Bold.ttf', 11)
    probe = ImageDraw.Draw(Image.new('RGB', (1, 1)))
    tw = int(probe.textlength(text, font=font))
    strip = Image.new('RGB', (tw + width, 16), (0, 0, 0))
    d = m.crisp(ImageDraw.Draw(strip))
    d.text((width, 1), text, font=font, fill=(255, 176, 0))
    frames = []
    for x in range(0, tw + width - width // 3, step):
        im = Image.new('RGB', (width, 16), (0, 0, 0))
        im.paste(strip.crop((x, 0, x + width, 16)), (0, 0))
        px = im.load()
        for yy in range(1, 16, 2):               # LED look: dark lines between the rows of dots
            for xx in range(width):
                r, g, b = px[xx, yy]
                px[xx, yy] = (r // 3, g // 3, b // 3)
        dd = ImageDraw.Draw(im)
        dd.rectangle([0, 0, width - 1, 15], outline=(90, 90, 90))
        frames.append(im)
    m.save(frames, 'ticker.gif', 50)


if __name__ == '__main__':
    for f, top, big, bg, fg, icon, anim in NAV:
        m.button(f, top, big, bg, fg, icon, anim=True if anim else None)
    m.button('btn-top.gif', 'back to', 'TOP ^', (0, 0, 96), (255, 255, 255), ((255, 255, 0), '^', (0, 0, 96)))
    m.counter(85)
    ticker("*** NEW!! beta.85: MIDPLAY's 3D view 3x smoother *** beta.84: 3D games safe from SBEMU's FPU bug *** "
           "beta.83: MIDPLAY made over *** virtual CDs work like a real disc *** Port 220 says hi!! ***")
    for f in sorted(os.listdir(m.OUT)):
        if f.startswith(('nav-', 'btn-top', 'ticker', 'counter')):
            p = os.path.join(m.OUT, f); print(f, Image.open(p).size, os.path.getsize(p))

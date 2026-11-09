#!/usr/bin/env python3
"""Port 220 - the NUKED SB-EMU mascot (original character): a little ISA sound card standing on its gold
edge connector, bobbing to the music, with notes coming out of its jacks. Named after the Sound Blaster's
usual I/O address.

    python3 docs/90s/port220.py      -> docs/90s/mascot-port220.gif
"""
import math, os
from PIL import Image, ImageDraw
import sprites90s as s90

W, H = 60, 50                      # drawn at 1x, then scaled 3x here and 2x by save_rgba (6x in total)
PCB, PCB_D, PCB_L = (24, 128, 64), (12, 70, 34), (60, 170, 90)
GOLD, GOLD_D = (240, 190, 40), (160, 110, 10)
CHIP, SILK = (28, 28, 34), (250, 240, 140)
METAL, METAL_D = (170, 175, 185), (100, 105, 115)

DIGITS = {'2': ["111", "001", "111", "100", "111"], '0': ["111", "101", "101", "101", "111"]}


def digits(d, x, y, text, col):
    for ch in text:
        for r, row in enumerate(DIGITS[ch]):
            for c, b in enumerate(row):
                if b == '1':
                    d.point((x + c, y + r), fill=col)
        x += 4


def note(d, x, y, col):
    d.rectangle([x, y + 3, x + 2, y + 5], fill=col)        # head
    d.line([x + 2, y, x + 2, y + 4], fill=col)             # stem
    d.line([x + 3, y, x + 4, y + 1], fill=col)             # flag


def frame(k, n, scale=3, notes=True):
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bob = int(round(1.5 * math.sin(2 * math.pi * k / n)))     # up and down with the beat
    bx, by, bw, bh = 12, 10 + bob, 38, 26                     # the board

    # gold edge connector = feet, tapping in turn
    for i in range(2):
        fx = bx + 6 + i * 18
        lift = 2 if (k // 2) % 2 == i else 0
        d.rectangle([fx, by + bh, fx + 9, by + bh + 5 - lift], fill=GOLD, outline=GOLD_D)
        for j in range(1, 9, 2):
            d.line([fx + j, by + bh + 1, fx + j, by + bh + 4 - lift], fill=GOLD_D)

    # metal bracket on the left with the jacks and the game port
    d.rectangle([bx - 6, by - 3, bx - 1, by + bh + 2], fill=METAL, outline=METAL_D)
    for i, c in enumerate([(60, 200, 90), (230, 120, 170), (80, 150, 240)]):
        d.ellipse([bx - 5, by + 2 + i * 5, bx - 2, by + 5 + i * 5], fill=c, outline=(40, 40, 40))
    d.rectangle([bx - 5, by + 18, bx - 2, by + 24], fill=(40, 40, 50))          # game port
    d.line([bx - 4, by + 19, bx - 4, by + 23], fill=GOLD)

    # the board
    d.rectangle([bx, by, bx + bw, by + bh], fill=PCB, outline=PCB_D)
    d.line([bx + 1, by + 1, bx + bw - 1, by + 1], fill=PCB_L)
    for t in range(3):                                                          # traces
        d.line([bx + 3, by + 21 + t * 2, bx + 9, by + 21 + t * 2], fill=PCB_D)
    d.rectangle([bx + 26, by + 18, bx + 34, by + 23], fill=CHIP)                # a chip with legs
    for j in range(bx + 27, bx + 34, 2):
        d.point((j, by + 17), fill=METAL); d.point((j, by + 24), fill=METAL)
    digits(d, bx + 12, by + 19, "220", SILK)                                    # silkscreen "220"

    # face
    blink = k == n - 2
    for ex in (bx + 9, bx + 23):
        if blink:
            d.line([ex, by + 9, ex + 5, by + 9], fill=(20, 20, 20))
        else:
            d.ellipse([ex, by + 5, ex + 5, by + 12], fill=(255, 255, 255), outline=(20, 20, 20))
            px = ex + 2 + (1 if (k // 3) % 2 else 0)
            d.rectangle([px, by + 8, px + 1, by + 10], fill=(10, 10, 10))
    d.arc([bx + 13, by + 9, bx + 22, by + 16], 20, 160, fill=(20, 20, 20))     # smile
    d.point((bx + 8, by + 13), fill=(255, 140, 150)); d.point((bx + 29, by + 13), fill=(255, 140, 150))

    # power LED
    d.rectangle([bx + bw - 4, by + 3, bx + bw - 2, by + 5], fill=(255, 60, 60) if k % 2 else (110, 20, 20))

    # notes floating up out of the jacks
    for i, c in enumerate([] if not notes else [(255, 220, 60), (120, 230, 255), (255, 120, 220)]):
        t = ((k + i * n / 3) % n) / n
        nx = int(bx - 10 - 6 * t + 2 * math.sin(t * 6 + i))
        ny = int(by + 4 + i * 5 - 22 * t)
        if 0 <= ny < H - 6 and nx >= 0:
            note(d, nx, ny, c)
    return im.resize((W * scale, H * scale), Image.NEAREST) if scale != 1 else im


def hello(n=12):
    """Port 220 with a speech bubble (the README's header)"""
    from PIL import ImageFont
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15)
    out = []
    for k in range(n):
        c = Image.new('RGBA', (440, 150), (0, 0, 0, 0))
        c.alpha_composite(frame(k, n), (0, 0))
        d = ImageDraw.Draw(c); d.fontmode = '1'
        d.rounded_rectangle([182, 14, 432, 96], 10, fill=(255, 255, 255), outline=(20, 20, 20), width=3)
        d.polygon([(186, 70), (160, 92), (200, 82)], fill=(255, 255, 255), outline=(20, 20, 20))
        d.line([(186, 72), (198, 82)], fill=(255, 255, 255), width=3)
        lines = ["Hi!! I'm Port 220,", "the NUKED SB-EMU", "mascot!" + (" \u266a" if k % 4 < 2 else "")]
        for i, t in enumerate(lines):
            d.text((198, 22 + i * 22), t, font=font, fill=(200, 30, 120) if i == 0 else (20, 20, 60))
        out.append(c)
    return out


def hop(n=48, width=300):
    """Port 220 hopping across a strip, notes behind it (1x, saved 2x)"""
    out = []
    small = [frame(k, 12, scale=1, notes=False) for k in range(12)]
    for k in range(n):
        c = Image.new('RGBA', (width, 56), (0, 0, 0, 0))
        d = ImageDraw.Draw(c)
        x = int(-W + (width + W) * k / n)
        y = 4 - int(5 * abs(math.sin(math.pi * k / 6)))
        for j in range(5):                                    # trail of notes
            nx = x - 8 - j * 14
            if 0 <= nx < width - 6:
                note(d, nx, 30 - int(6 * math.sin((k + j * 3) / 2.0)), [(255, 220, 60), (120, 230, 255), (255, 120, 220)][j % 3])
        c.alpha_composite(small[k % 12], (x, max(0, y))) if -W < x < width else None
        out.append(c)
    return out


if __name__ == '__main__':
    n = 12
    s90.save_rgba([frame(k, n) for k in range(n)], 'mascot-port220.gif', 110)
    s90.save_rgba([frame(k, n, scale=1) for k in range(n)], 'port220-small.gif', 110)
    s90.save_rgba(hello(), 'port220-hello.gif', 110, scale=1)
    s90.save_rgba(hop(), 'port220-hop.gif', 70)
    for f in ('mascot-port220.gif', 'port220-small.gif', 'port220-hello.gif', 'port220-hop.gif'):
        p = os.path.join(s90.HERE, f); print(f, Image.open(p).size, os.path.getsize(p))

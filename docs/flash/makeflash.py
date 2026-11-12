#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""Draws the 2001 "Flash intro" graphics used by README.md (all original, made with Pillow).

    python3 docs/flash/makeflash.py      -> writes the GIFs next to this script
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT = os.path.dirname(os.path.abspath(__file__))
FD = '/usr/share/fonts/truetype/dejavu/'
F = lambda n, s: ImageFont.truetype(FD + n, s)
BOLD, COND, MONO = 'DejaVuSans-Bold.ttf', 'DejaVuSansCondensed-Bold.ttf', 'DejaVuSansMono-Bold.ttf'
random.seed(2001)
BG = (12, 14, 20)
CYAN, ORANGE = (0, 200, 255), (255, 140, 0)


def save(frames, name, ms=80, colors=128, last=None):
    pal = [f.convert('RGB').convert('P', palette=Image.ADAPTIVE, colors=colors) for f in frames]
    dur = [ms] * len(pal)
    if last: dur[-1] = last
    pal[0].save(os.path.join(OUT, name), save_all=len(pal) > 1, append_images=pal[1:], duration=dur, loop=0,
                optimize=True, disposal=1)


def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def ease(t): return 1 - (1 - t) ** 3


def backdrop(w, h, phase=0.0):
    """dark tech panel: gradient, thin grid, a sweeping light band, corner brackets"""
    im = Image.new('RGB', (w, h), BG)
    d = ImageDraw.Draw(im)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp((22, 26, 38), (6, 7, 10), y / h))
    for x in range(0, w, 16): d.line([(x, 0), (x, h)], fill=(24, 30, 44))
    for y in range(0, h, 16): d.line([(0, y), (w, y)], fill=(24, 30, 44))
    bx = int(-w * 0.4 + (w * 1.8) * (phase % 1.0))
    band = Image.new('L', (w, h), 0)
    ImageDraw.Draw(band).polygon([(bx, 0), (bx + 60, 0), (bx + 20, h), (bx - 40, h)], fill=38)
    band = band.filter(ImageFilter.GaussianBlur(14))
    im = Image.composite(Image.new('RGB', (w, h), (120, 200, 255)), im, band)
    d = ImageDraw.Draw(im)
    for (x, y, sx, sy) in ((6, 6, 1, 1), (w - 7, 6, -1, 1), (6, h - 7, 1, -1), (w - 7, h - 7, -1, -1)):
        d.line([(x, y), (x + 18 * sx, y)], fill=CYAN, width=2)
        d.line([(x, y), (x, y + 18 * sy)], fill=CYAN, width=2)
    return im


def chrome_text(text, font, glow=CYAN):
    """metallic text with a dark outline and a soft glow, as an RGBA image"""
    l, t, r, b = font.getbbox(text)
    w, h = r - l + 24, b - t + 24
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((12 - l, 12 - t), text, font=font, fill=255)
    grad = Image.new('RGB', (w, h))
    gd = ImageDraw.Draw(grad)
    for y in range(h):
        u = y / h
        c = lerp((255, 255, 255), (150, 165, 190), u / 0.5) if u < 0.5 else lerp((70, 80, 100), (230, 235, 245), (u - 0.5) / 0.5)
        gd.line([(0, y), (w, y)], fill=c)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    g = mask.filter(ImageFilter.GaussianBlur(6))
    out.paste(Image.new('RGBA', (w, h), glow + (255,)), (0, 0), g)
    edge = mask.filter(ImageFilter.MaxFilter(3))
    out.paste(Image.new('RGBA', (w, h), (10, 12, 18, 255)), (0, 0), edge)
    out.paste(grad, (0, 0), mask)
    return out


def flare(im, x, y, s=1.0):
    """lens flare: glow, star streaks, a few rings along the axis"""
    w, h = im.size
    lay = Image.new('RGB', (w, h), (0, 0, 0))
    d = ImageDraw.Draw(lay)
    r = int(26 * s)
    d.ellipse([x - r, y - r, x + r, y + r], fill=(160, 200, 255))
    d.line([(x - 140 * s, y), (x + 140 * s, y)], fill=(200, 230, 255), width=2)
    d.line([(x, y - 40 * s), (x, y + 40 * s)], fill=(200, 230, 255), width=1)
    lay = lay.filter(ImageFilter.GaussianBlur(5))
    d = ImageDraw.Draw(lay)
    d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(255, 255, 255))
    cx, cy = w / 2, h / 2
    for k, (rr, col) in enumerate(((12, (60, 90, 40)), (22, (40, 60, 90)), (8, (90, 50, 30)))):
        t = 0.6 + 0.5 * k
        px, py = x + (cx - x) * t * 2, y + (cy - y) * t * 2
        d.ellipse([px - rr * s, py - rr * s, px + rr * s, py + rr * s], outline=col, width=2)
    return _add(im, lay)


def _add(a, b):
    from PIL import ImageChops
    return ImageChops.add(a, b)


PIX = lambda n: F('DejaVuSansMono.ttf', n)


def hud(im, i, sticker=True):
    """the bits a 2002 site-builder piled on: tiny pixel text, an equalizer, a tilted version sticker, scanlines"""
    W, H = im.size
    d = ImageDraw.Draw(im)
    d.fontmode = '1'
    for k in range(14):                                         # equalizer (it is a sound site after all)
        v = abs(math.sin(i * 0.7 + k * 1.3) * math.cos(i * 0.31 + k * 0.5))
        hgt = 4 + int(v * 30)
        for y in range(0, hgt, 3):
            d.rectangle([24 + k * 7, H - 26 - y, 28 + k * 7, H - 25 - y], fill=lerp((0, 120, 160), ORANGE, y / 34))
    d.text((24, 16), 'NSB_v3.0 // build 2002.11 // sfx: ON', font=PIX(10), fill=(70, 150, 190))
    d.text((W - 24, 16), '[ best @ 1024x768 ]', font=PIX(10), fill=(70, 90, 120), anchor='ra')
    d.text((130, H - 34), 'sound canvas: online_', font=PIX(10), fill=(110, 130, 150))
    if sticker:
        st = Image.new('RGBA', (120, 30), (0, 0, 0, 0))
        sd = ImageDraw.Draw(st)
        sd.rectangle([0, 0, 119, 29], fill=ORANGE + (255,))
        sd.text((60, 15), 'v3.0 BETA!!', font=F(BOLD, 15), fill=(20, 20, 20), anchor='mm')
        st = st.rotate(14, expand=True, resample=Image.BICUBIC)
        im.paste(st, (W - 150, 40), st)
    sl = Image.new('L', im.size, 0)
    sd = ImageDraw.Draw(sl)
    for y in range(0, H, 2): sd.line([(0, y), (W, y)], fill=40)
    return Image.composite(Image.new('RGB', im.size, (0, 0, 0)), im, sl)


def glitch(im, seed):
    """a frame of shifted slices, as when the .swf was still streaming in"""
    r = random.Random(seed)
    out = im.copy()
    for _ in range(7):
        y = r.randrange(0, im.height - 12); h = r.randrange(3, 12); dx = r.randrange(-30, 30)
        out.paste(im.crop((0, y, im.width, y + h)), (dx, y))
    return out


def intro():
    """preloader 0..100%, the logo flies in with motion blur, a flare sweeps, ENTER pulses (loops)"""
    W, H = 720, 300
    fr, durs = [], []
    small, mono = F(COND, 15), F(MONO, 13)
    # 1. preloader
    for i in range(14):
        p = min(100, int(ease(i / 13) * 100))
        im = backdrop(W, H, i / 40)
        d = ImageDraw.Draw(im)
        d.text((W // 2, 118), 'LOADING', font=F(COND, 20), fill=(180, 200, 220), anchor='mm')
        d.rectangle([210, 140, 510, 156], outline=(90, 110, 140))
        d.rectangle([213, 143, 213 + int(294 * p / 100), 153], fill=CYAN)
        d.text((W // 2, 176), '%3d%%' % p, font=mono, fill=(120, 140, 170), anchor='mm')
        d.text((W // 2, 214), 'please wait... loading the sound canvas', font=F(MONO, 11), fill=(60, 70, 90), anchor='mm')
        fr.append(hud(im, i, False)); durs.append(70)
    logo = chrome_text('NUKED SB-EMU', F(BOLD, 64))
    sub = chrome_text('ROLAND SC-55 SOUND FOR DOS GAMES', F(COND, 20), glow=ORANGE)
    # 2. fly-in: from huge + blurred to resting size
    for i in range(12):
        t = ease(i / 11)
        sc = 3.0 - 2.0 * t
        im = backdrop(W, H, 0.35 + i / 40)
        lg = logo.resize((max(1, int(logo.width * sc)), max(1, int(logo.height * sc))), Image.BICUBIC)
        if i < 10: lg = lg.filter(ImageFilter.GaussianBlur((1 - t) * 6))
        a = lg.split()[3].point(lambda v: int(v * min(1, 0.15 + t)))
        lg.putalpha(a)
        im.paste(lg, (W // 2 - lg.width // 2, 110 - lg.height // 2), lg)
        im = hud(im, 14 + i, False)
        if i in (9, 11): im = glitch(im, i)
        fr.append(im); durs.append(50)
    # 3. tagline slides in, flare sweeps across the logo
    for i in range(16):
        t = ease(min(1, i / 8))
        im = backdrop(W, H, 0.65 + i / 40)
        im.paste(logo, (W // 2 - logo.width // 2, 110 - logo.height // 2), logo)
        sx = int(-sub.width + (W // 2 - sub.width // 2 + sub.width) * t)
        im.paste(sub, (sx, 168), sub)
        im = flare(im, int(150 + 420 * i / 15), 104, 0.8 + 0.3 * math.sin(i / 3))
        fr.append(hud(im, 26 + i)); durs.append(60)
    # 4. ENTER / SKIP INTRO pulse
    for i in range(10):
        im = backdrop(W, H, 0.05 + i / 30)
        im.paste(logo, (W // 2 - logo.width // 2, 110 - logo.height // 2), logo)
        im.paste(sub, (W // 2 - sub.width // 2, 168), sub)
        d = ImageDraw.Draw(im)
        g = 0.5 + 0.5 * math.sin(i / 10 * 2 * math.pi)
        d.rounded_rectangle([300, 226, 420, 254], 13, outline=lerp((40, 80, 110), CYAN, g), width=2)
        d.text((W // 2, 240), '[ ENTER ]', font=F(COND, 16), fill=lerp((120, 140, 160), (255, 255, 255), g), anchor='mm')
        d.text((W - 20, H - 16), 'skip intro >>', font=F(COND, 11), fill=(90, 110, 140), anchor='rm')
        fr.append(hud(im, 42 + i)); durs.append(90)
    durs[-1] = 1800
    pal = [f.convert('P', palette=Image.ADAPTIVE, colors=128) for f in fr]
    pal[0].save(os.path.join(OUT, 'intro.gif'), save_all=True, append_images=pal[1:], duration=durs, loop=0, optimize=True)


def button(label, name, w=150, h=34, accent=CYAN):
    """glossy pill button; a glint runs across it"""
    fr = []
    font = F(COND, 14)
    for i in range(12):
        im = Image.new('RGB', (w, h), BG)
        d = ImageDraw.Draw(im)
        body = Image.new('RGB', (w, h))
        bd = ImageDraw.Draw(body)
        for y in range(h):
            u = y / h
            bd.line([(0, y), (w, y)], fill=lerp((95, 110, 135), (40, 46, 60), u / 0.5) if u < 0.5 else lerp((22, 26, 34), (55, 64, 82), (u - 0.5) / 0.5))
        m = Image.new('L', (w, h), 0)
        ImageDraw.Draw(m).rounded_rectangle([1, 1, w - 2, h - 2], h // 2 - 1, fill=255)
        im.paste(body, (0, 0), m)
        gx = int(-40 + (w + 80) * i / 11)
        gl = Image.new('L', (w, h), 0)
        ImageDraw.Draw(gl).polygon([(gx, 0), (gx + 18, 0), (gx + 6, h), (gx - 12, h)], fill=90)
        gl = Image.composite(gl.filter(ImageFilter.GaussianBlur(4)), Image.new('L', (w, h), 0), m)
        im = Image.composite(Image.new('RGB', (w, h), (255, 255, 255)), im, gl)
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([1, 1, w - 2, h - 2], h // 2 - 1, outline=accent, width=1)
        d.ellipse([12, h // 2 - 3, 18, h // 2 + 3], fill=accent)
        d.text((w // 2 + 6, h // 2), label.lower() if len(label) < 13 else label, font=font, fill=(235, 240, 250), anchor='mm')
        d.text((w - 16, h // 2), '>>', font=F(COND, 10), fill=accent, anchor='mm')
        fr.append(im)
    save(fr, name, ms=70, colors=64, last=2200)


def divider():
    """thin line with a light pulse running along it"""
    W, H = 720, 14
    fr = []
    for i in range(16):
        im = Image.new('RGB', (W, H), BG)
        d = ImageDraw.Draw(im)
        d.line([(0, 7), (W, 7)], fill=(40, 60, 80))
        x = int(W * i / 15)
        for k in range(60):
            d.line([(x - k * 3, 7), (x - k * 3 - 3, 7)], fill=lerp(CYAN, (40, 60, 80), k / 60), width=2)
        d.ellipse([x - 3, 4, x + 3, 10], fill=(220, 245, 255))
        for k in range(0, W, 120): d.rectangle([k, 5, k + 4, 9], fill=ORANGE)
        fr.append(im)
    save(fr, 'divider.gif', ms=60, colors=32)


def badge():
    """'best viewed' badge, 88x31 style, original"""
    fr = []
    for i in range(8):
        im = Image.new('RGB', (176, 62), (20, 20, 26))
        d = ImageDraw.Draw(im)
        d.rectangle([0, 0, 175, 61], outline=(120, 130, 150), width=2)
        d.rounded_rectangle([6, 6, 54, 55], 8, fill=lerp((0, 70, 110), (0, 150, 210), (math.sin(i / 8 * 2 * math.pi) + 1) / 2))
        d.polygon([(22, 18), (22, 44), (44, 31)], fill=(255, 255, 255))
        d.text((62, 14), 'BEST VIEWED', font=F(COND, 13), fill=(230, 230, 240))
        d.text((62, 30), 'WITH A FAST', font=F(COND, 13), fill=(230, 230, 240))
        d.text((62, 44), 'PLUG-IN  v5', font=F(COND, 11), fill=ORANGE)
        fr.append(im)
    save(fr, 'badge-plugin.gif', ms=120, colors=32)


if __name__ == '__main__':
    intro()
    divider()
    badge()
    for label, name in (('NEWS', 'b-news'), ('ABOUT', 'b-what'), ('FEATURES', 'b-features'), ('WILL IT RUN?', 'b-run'),
                        ('TEST LAB', 'b-lab'), ('GET STARTED', 'b-start'), ('USB PADS', 'b-pads'), ('SOUND CHIPS', 'b-chips'),
                        ('SCREENSHOTS', 'b-shots'), ('HALL OF FAME', 'b-fame'), ('CLAUDE', 'b-claude'), ('EXTRAS', 'b-home'),
                        ('GUESTBOOK', 'b-guest'), ('SOURCE', 'b-source'), ('THANKS', 'b-thanks')):
        button(label, name + '.gif')
    button('DOWNLOAD', 'b-download.gif', w=200, h=40, accent=ORANGE)
    button('SKIP INTRO >>', 'b-skip.gif', w=150, h=28)
    button('TOP ^', 'b-top.gif', w=100, h=26)

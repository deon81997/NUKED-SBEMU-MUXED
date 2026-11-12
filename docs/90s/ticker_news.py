#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""Builds the README's news ticker from the newest "What's NEW!!" entries in README.md.

    python3 docs/90s/ticker_news.py OUTDIR [count]     (default: the 3 newest entries)

Writes OUTDIR/ticker-hd.gif and OUTDIR/ticker-hd.txt (the text it was made from); prints "unchanged" and does
nothing when OUTDIR/ticker-hd.txt already holds the same text. .github/workflows/ticker.yml runs it on every
README.md change and uploads the result to the "news-ticker" pre-release (the Actions bot may not push branches);
the README shows https://github.com/deon81997/NUKED-SBEMU-MUXED/releases/download/news-ticker/ticker-hd.gif
"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
README = os.path.join(ROOT, 'README.md')


def news(count):
    s = open(README, encoding='utf-8').read()
    a = s.index("## ★ What's NEW!! ★")
    b = s.index('<details>', a)
    out = []
    # <img ...> **11/11/2026: beta.92.** **An empty CD drive no longer freezes the PC!!** ...
    for m in re.finditer(r'\*\*[0-9/]+: ([^*]+?)\.\*\* \*\*([^*]+?)\*\*', s[a:b]):
        ver, head = m.group(1).strip(), m.group(2).strip()
        head = re.sub(r'\s+', ' ', head).replace('`', '')
        out.append('%s: %s' % (ver, head))
        if len(out) >= count:
            break
    return out


def main():
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else HERE
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    stamp = os.path.join(out, 'ticker-hd.txt')
    items = news(count)
    if not items:
        sys.exit('no news entries found in README.md')
    text = '*** NEW!! ' + ' *** '.join(items) + ' *** Port 220 says hi!! ***'
    old = open(stamp, encoding='utf-8').read().strip() if os.path.exists(stamp) else ''
    if text == old:
        print('unchanged'); return
    import make90s, nav90s
    os.makedirs(out, exist_ok=True)
    make90s.OUT = out
    nav90s.m.OUT = out
    nav90s.ticker(text)
    open(stamp, 'w', encoding='utf-8').write(text + '\n')
    print('ticker:', text)


if __name__ == '__main__':
    main()

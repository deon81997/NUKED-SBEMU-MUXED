#!/usr/bin/env python3
"""Rebuilds the README's news ticker (docs/90s/ticker-hd.gif) from the newest "What's NEW!!" entries in README.md,
and the ticker's alt text. Run by .github/workflows/ticker.yml on every push that changes README.md; prints
"unchanged" when the text is the same as last time (the GIF is then left alone).

    python3 docs/90s/ticker_news.py [count]      (default: the 3 newest entries)
"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
README = os.path.join(ROOT, 'README.md')
STAMP = os.path.join(HERE, 'ticker-hd.txt')       # the text the GIF was made from


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
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    items = news(count)
    if not items:
        sys.exit('no news entries found in README.md')
    text = '*** NEW!! ' + ' *** '.join(items) + ' *** Port 220 says hi!! ***'
    old = open(STAMP, encoding='utf-8').read().strip() if os.path.exists(STAMP) else ''
    if text == old:
        print('unchanged'); return
    import nav90s
    nav90s.ticker(text)
    open(STAMP, 'w', encoding='utf-8').write(text + '\n')
    alt = text.replace('"', "'").replace('*** ', '').replace(' ***', '').strip()
    s = open(README, encoding='utf-8').read()
    s = re.sub(r'(<img src="docs/90s/ticker-hd\.gif" width="600" alt=")[^"]*(")', lambda m: m.group(1) + alt + m.group(2), s)
    open(README, 'w', encoding='utf-8').write(s)
    print('ticker:', text)


if __name__ == '__main__':
    main()

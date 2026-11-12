#!/usr/bin/env python3
"""notetest.py RESULT.TXT: one line per song - bytes MIDPLAY sent vs bytes SBEMU received, dropped bytes, note-ons, late."""
import re, sys
txt = open(sys.argv[1], encoding='latin-1').read()
bad = 0
for blk in txt.split('==== ')[1:]:
    name = blk.split('\n', 1)[0].strip()
    sent = re.search(r'sent: (\d+) MIDI bytes, (\d+) note-ons(?:, synth busy (\d+))?', blk)
    got = re.search(r'([\d.]+) bytes per second from the game, (\d+) dropped', blk)
    secs = re.search(r'Measured ([\d.]+) s', blk)
    late = re.search(r'late avg ([\d.]+) ms max ([\d.]+) ms', blk)
    s = int(sent.group(1)) if sent else -1
    line = '%-28s sent %7s bytes %6s notes' % (name[:28], s, sent.group(2) if sent else '?')
    if got:
        line += ' | SBEMU got ~%d, dropped %s' % (float(got.group(1)) * float(secs.group(1)) if secs else -1, got.group(2))
        if int(got.group(2)): bad += 1
    else: line += ' | no SBDIAG report'
    if sent and sent.group(3): line += ' | busy %s' % sent.group(3)
    if late: line += ' | late avg %s max %s ms' % late.groups()
    print(line)
print('RESULT:', 'notes dropped in %d song(s)' % bad if bad else 'no bytes dropped')

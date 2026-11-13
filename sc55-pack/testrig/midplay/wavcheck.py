#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""wavcheck.py [--metro N] NAME=FILE.WAV ... : checks recordings made with notetest.sh WAV=... (one per synth / rate).
Per file: seconds of sound, loudness, peak, clipped samples, dropouts (sound cut off in the middle of the
music: loud to almost nothing within 10 ms, as a buffer underrun does) and clicks (jumps between neighbouring samples far above the music's own).
--metro N: the recording starts with N notes one second apart (METRO.MID, made by fmtcheck.py's folder or by hand):
counts them and checks the spacing (1.00 s +- 30 ms = the synth plays in time, nothing lost or bunched up).
Exit code 1 if any file is silent, clips, drops out or (--metro) has the wrong beats."""
import sys, wave, array, math, cmath

def load(f):
    w = wave.open(f); r = w.getframerate(); ch = w.getnchannels()
    d = array.array('h', w.readframes(w.getnframes()))
    return r, [(d[i] + d[i + 1]) / 2 for i in range(0, len(d) - 1, 2)] if ch == 2 else list(d)

bad = 0; metro = 0; args = sys.argv[1:]
if args[:1] == ['--metro']: metro = int(args[1]); args = args[2:]
def beats(lv):
    # onsets: the 10 ms level rises above 30% of the loudest after being under 15% (lv = 10 ms blocks)
    top = max(lv) or 1; on = []; armed = True
    for i, v in enumerate(lv):
        if armed and v > 0.3 * top and (not on or i / 100 - on[-1] >= 0.5): on.append(i / 100); armed = False
        elif v < 0.15 * top: armed = True
    return on
for arg in args:
    name, f = arg.split('=', 1)
    try: r, x = load(f)
    except Exception as e: print(f'{name:20s} cannot read: {e}'); bad += 1; continue
    blk = r // 100                                          # 10 ms blocks
    lv = [math.sqrt(sum(v * v for v in x[i:i + blk]) / blk) for i in range(0, len(x) - blk, blk)]
    on = [i for i, v in enumerate(lv) if v > 30]
    secs = len(on) / 100
    peak = max((abs(v) for v in x), default=0); clip = sum(1 for v in x if abs(v) >= 32767)
    drops = 0
    if on:   # a dropout = the sound cut off at once (loud -> almost nothing in 10 ms); a note's release fades over
             # several blocks. Up to the last loud part (a song stopped by /Q is cut on purpose).
        top = max(lv[i] for i in on); loud_end = max(i for i in on if lv[i] > 0.1 * top)
        drops = sum(1 for i in range(on[0] + 1, loud_end) if lv[i - 1] > 0.2 * top and lv[i] < 0.01 * top)
    d = [abs(x[i + 1] - x[i]) for i in range(len(x) - 1)]
    typ = sorted(d)[int(len(d) * 0.999)] if d else 0
    clicks = sum(1 for v in d if typ and v > 6 * typ and v > 4000)
    loud = 20 * math.log10(max(1, sum(lv[i] for i in on) / max(1, len(on))) / 32768)
    ok = secs > 2 and not clip and not drops
    mtxt = ''
    if metro:
        on = beats(lv)[:metro]; gaps = [b - a for a, b in zip(on, on[1:])]
        mok = len(on) == metro and all(abs(g - 1) <= 0.03 for g in gaps)
        mtxt = f'  beats {len(on)}/{metro} gap {min(gaps, default=0):.3f}-{max(gaps, default=0):.3f} s'
        ok = ok and mok
    bad += not ok
    print(f'{name:20s} {r} Hz  sound {secs:5.1f} s  level {loud:6.1f} dBFS  peak {int(peak):5d}  clipped {clip}  dropouts {drops}  clicks {clicks}{mtxt}  {"OK" if ok else "CHECK"}')
sys.exit(1 if bad else 0)

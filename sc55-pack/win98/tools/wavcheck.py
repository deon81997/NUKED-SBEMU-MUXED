import sys, wave, numpy as np
w = wave.open(sys.argv[1]); r = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(float) / 32768
m = x.mean(axis=1)
def seg(a, b, label):
    s = m[int(a*r):int(b*r)]
    rms = np.sqrt((s**2).mean()) if len(s) else 0
    if rms < 1e-4: print(f'{label:24s} {a:4.1f}-{b:4.1f}s  rms {rms:.5f}  (silent)'); return
    sp = np.abs(np.fft.rfft(s * np.hanning(len(s)))); f = np.fft.rfftfreq(len(s), 1/r)
    sp[f < 60] = 0
    pk = f[np.argmax(sp)]
    print(f'{label:24s} {a:4.1f}-{b:4.1f}s  rms {rms:.4f}  peak {pk:7.1f} Hz')
for a, b, l in [(0.0,0.9,'boot/silence'),(1.05,1.4,'note C4 261.6'),(1.55,1.9,'note E4 329.6'),(2.05,2.4,'note G4 392.0'),(2.55,2.9,'note C5 523.3'),
                (3.6,4.4,'OPL tone'),(5.1,5.9,'SB PCM 1 kHz'),(6.2,6.9,'tail')]:
    seg(a, b, l)

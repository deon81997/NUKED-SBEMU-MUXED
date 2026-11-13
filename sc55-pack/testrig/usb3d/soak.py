# soak.py SECONDS: MIDPLAY 3D (V/C/J, arrows, mouse) -> Esc -> MENU (F8 3D, F3/F6/F4, arrows, mouse) -> F8, Esc ... loop
import random, sys, time
from drv import mon, shot
r = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
end = time.time() + float(sys.argv[1]); cyc = 0
def burst(keys, secs):
    t = time.time() + secs
    while time.time() < t:
        if r.random() < 0.4: mon('mouse_move %d %d' % (r.randint(-40, 40), r.randint(-30, 30)), 0.01)
        else: mon('sendkey ' + r.choice(keys), 0.01)
        time.sleep(r.choice([0.02, 0.05, 0.1, 0.3]))
while time.time() < end:
    cyc += 1
    burst(['v', 'c', 'j', 'left', 'right', 'up', 'down', 'v', 'c', 'j', 'j'], 25)
    shot('m%03d' % (cyc % 1000)); mon('sendkey esc'); time.sleep(4)
    mon('sendkey esc'); time.sleep(1); mon('sendkey f8'); time.sleep(4)
    burst(['f3', 'f6', 'f4', 'left', 'right', 'up', 'down', 'f3', 'f3', 'pgdn', 'pgup'], 25)
    shot('s%03d' % (cyc % 1000)); mon('sendkey f8'); time.sleep(2); mon('sendkey esc'); time.sleep(5)
    print('cycle', cyc, flush=True)

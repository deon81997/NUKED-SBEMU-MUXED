import socket, sys, time
# qk.py SOCK item... ; item: "t:text" types text, "k:key" sends a key combo, "s:sec" sleeps, "shot:file"
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(5)
def rd():
    b = b''
    while not b.endswith(b'(qemu) '):
        try: b += s.recv(4096)
        except socket.timeout: break
    return b
rd()
def cmd(c): s.send(c.encode() + b'\n'); rd()
M = {' ': 'spc', '.': 'dot', ',': 'comma', '-': 'minus', '\\': 'backslash', '/': 'slash', ':': 'shift-semicolon',
     ';': 'semicolon', '=': 'equal', '_': 'shift-minus', '\n': 'ret', '"': 'shift-apostrophe', "'": 'apostrophe',
     '*': 'shift-8', '>': 'shift-dot', '<': 'shift-comma', '|': 'shift-backslash', '%': 'shift-5', '&': 'shift-7',
     '(': 'shift-9', ')': 'shift-0', '!': 'shift-1', '@': 'shift-2', '#': 'shift-3', '$': 'shift-4', '+': 'shift-equal'}
for it in sys.argv[2:]:
    k, v = it.split(':', 1)
    if k == 't':
        for ch in v:
            if ch in M: cmd('sendkey ' + M[ch])
            elif ch.isupper(): cmd('sendkey shift-' + ch.lower())
            else: cmd('sendkey ' + ch)
            time.sleep(0.03)
    elif k == 'k': cmd('sendkey ' + v); time.sleep(0.2)
    elif k == 's': time.sleep(float(v))
    elif k == 'shot':
        cmd('screendump /tmp/_q.ppm'); time.sleep(0.5)
        from PIL import Image; Image.open('/tmp/_q.ppm').save(v)
    elif k == 'm': cmd(v)
    elif k == 'mv':                       # absolute-ish: home to the top left, then small steps (no acceleration)
        x, y = [int(a) for a in v.split(',')]
        for _ in range(40): cmd('mouse_move -50 -50')
        while x > 0 or y > 0:
            dx, dy = min(x, 3), min(y, 3); cmd('mouse_move %d %d' % (dx, dy)); x -= dx; y -= dy
    elif k == 'click': cmd('mouse_button 1'); time.sleep(0.15); cmd('mouse_button 0')
    elif k == 'rclick': cmd('mouse_button 2'); time.sleep(0.15); cmd('mouse_button 0')

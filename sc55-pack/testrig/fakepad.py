#!/usr/bin/env python3
"""fakepad.py - a pretend USB Xbox 360 pad for QEMU (usbredir protocol, device side).

   python3 fakepad.py PORT SCRIPT     listens on 127.0.0.1:PORT; QEMU connects with
       -chardev socket,id=pad,host=127.0.0.1,port=PORT -device usb-redir,chardev=pad,bus=<uhci>.0
   SCRIPT: lines "seconds  field=value ..." (fields: lx ly rx ry lt rt buttons, buttons as hex XPAD_* bits),
   times counted from the moment the guest starts polling the pad. '#' comments.
   Logs everything it is asked to fakepad.log next to this file (control requests, LED command)."""
import os, socket, struct, sys, time, select

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = open(os.path.join(HERE, 'fakepad.log'), 'w')
def log(*a):
    LOG.write('%8.3f ' % (time.time() - T0) + ' '.join(str(x) for x in a) + '\n'); LOG.flush()
T0 = time.time()

DEV = bytes([0x12, 0x01, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x08, 0x5E, 0x04, 0x8E, 0x02, 0x14, 0x01, 0x01, 0x02, 0x03, 0x01])
IF0 = bytes([9, 4, 0, 0, 2, 0xFF, 0x5D, 0x01, 0]) + \
      bytes([0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0, 0, 0, 0, 0x13, 0x01, 0x08, 0, 0]) + \
      bytes([7, 5, 0x81, 3, 0x20, 0, 4]) + bytes([7, 5, 0x01, 3, 0x20, 0, 8])
IF1 = bytes([9, 4, 1, 0, 2, 0xFF, 0x5D, 0x03, 0]) + bytes([7, 5, 0x82, 3, 0x20, 0, 2]) + bytes([7, 5, 0x02, 3, 0x20, 0, 4])
body = IF0 + IF1
CFG = bytes([9, 2]) + struct.pack('<H', 9 + len(body)) + bytes([2, 1, 0, 0xA0, 0xFA]) + body
STR = {0: bytes([4, 3, 0x09, 0x04]), 1: 'Microsoft'.encode('utf-16-le'), 2: 'Controller'.encode('utf-16-le'), 3: '08B3C5A'.encode('utf-16-le')}

HIGH = os.environ.get('FAKEPAD_SPEED') == 'high'   # test only: announce a high-speed device
if HIGH:
    DEV = DEV[:7] + bytes([64]) + DEV[8:]

def strdesc(i):
    s = STR.get(i, b'')
    return s if i == 0 else bytes([2 + len(s), 3]) + s

class Pad:
    def __init__(s, script):
        s.st = dict(lx=0, ly=0, rx=0, ry=0, lt=0, rt=0, buttons=0)
        s.events = []
        for ln in open(script):
            ln = ln.split('#')[0].split()
            if not ln: continue
            t = float(ln[0]); ch = {}
            for kv in ln[1:]:
                k, v = kv.split('=')
                ch[k] = int(v, 16) if k == 'buttons' else int(v)
            s.events.append((t, ch))
        s.start = None; s.sent = None
    def report(s):
        b = s.st['buttons']
        return struct.pack('<BBBBBBhhhh6x', 0, 0x14, b & 0xFF, b >> 8, s.st['lt'], s.st['rt'], s.st['lx'], s.st['ly'], s.st['rx'], s.st['ry'])
    def update(s):
        if s.start is None: return
        t = time.time() - s.start
        while s.events and s.events[0][0] <= t:
            s.st.update(s.events.pop(0)[1]); log('state', s.st)

def main():
    port, script = int(sys.argv[1]), sys.argv[2]
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port)); srv.listen(1)
    log('listening', port)
    while True:                         # every connection is a new plug-in (QEMU device_add / device_del)
        c, _ = srv.accept(); log('qemu connected')
        try: session(c, Pad(script))
        except (ConnectionError, OSError) as e: log('connection ended', e)
        c.close()

def session(c, pad):
    # usbredir: 32-bit packet ids until both hellos are exchanged, 64-bit after that when both sides
    # have the capability (QEMU's xHCI insists on 64-bit ids, 32-bit bulk lengths, ep max packet size)
    wide = [False]
    def send(typ, pid, hdr, data=b''):
        h = struct.pack('<IIQ' if wide[0] else '<III', typ, len(hdr) + len(data), pid)
        c.sendall(h + hdr + data)
    caps = (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)
    send(0, 0, b'fakepad 1.0'.ljust(64, b'\0') + struct.pack('<I', caps))
    buf = b''
    while True:                         # wait for QEMU's hello
        buf += c.recv(65536)
        if len(buf) >= 12:
            typ, ln, pid = struct.unpack('<III', buf[:12])
            if len(buf) >= 12 + ln: break
    peer = struct.unpack('<I', buf[12 + 64:12 + 68])[0] if ln >= 68 else 0
    log('hello from qemu', buf[12:12 + 64].rstrip(b'\0'), 'caps %x' % peer)
    buf = buf[12 + ln:]
    wide[0] = bool(peer & (1 << 5))
    def device_info():
        n = 2
        ifs = bytes([0, 1] + [0] * 30) + bytes([0xFF, 0xFF] + [0] * 30) + bytes([0x5D, 0x5D] + [0] * 30) + bytes([1, 3] + [0] * 30)
        send(4, 0, struct.pack('<I', n) + ifs)
        typ = [255] * 32; itv = [0] * 32; itf = [0] * 32; mps = [0] * 32
        def ep(addr, t, i, intf, m):
            k = ((addr & 0x80) >> 3) | (addr & 0x0F); typ[k] = t; itv[k] = i; itf[k] = intf; mps[k] = m
        ep(0x00, 0, 0, 0, 64 if HIGH else 8); ep(0x80, 0, 0, 0, 64 if HIGH else 8)
        ep(0x81, 3, 4, 0, 32); ep(0x01, 3, 8, 0, 32); ep(0x82, 3, 2, 1, 32); ep(0x02, 3, 4, 1, 32)
        send(5, 0, bytes(typ) + bytes(itv) + bytes(itf) + struct.pack('<32H', *mps) + bytes(32 * 4))  # + max_streams
    device_info()
    send(1, 0, struct.pack('<BBBBHHH', 2 if HIGH else 1, 0xFF, 0xFF, 0xFF, 0x045E, 0x028E, 0x0114))
    log('device connect sent')
    irq_on = False; cfg = 0; last = 0; nid = 1
    hl = 16 if wide[0] else 12
    while True:
        r, _, _ = select.select([c], [], [], 0.004)
        if r:
            d = c.recv(65536)
            if not d: log('qemu closed'); return
            buf += d
        while len(buf) >= hl:
            typ, ln, pid = struct.unpack('<IIQ' if hl == 16 else '<III', buf[:hl])
            if len(buf) < hl + ln: break
            p = buf[hl:hl + ln]; buf = buf[hl + ln:]
            if typ == 0: log('hello from qemu', p[:64].rstrip(b'\0'))
            elif typ == 3: log('reset')
            elif typ == 6:
                cfg = p[0]; log('set_configuration', cfg)
                send(8, pid, bytes([0, cfg])); device_info()
            elif typ == 7: send(8, pid, bytes([0, cfg]))
            elif typ == 9: send(11, pid, bytes([0, p[0], 0]))
            elif typ == 10: send(11, pid, bytes([0, p[0], 0]))
            elif typ == 15:
                log('start_interrupt_receiving %02x' % p[0]); send(17, pid, bytes([0, p[0]]))
                if p[0] == 0x81: irq_on = True; pad.start = pad.start or time.time()
            elif typ == 16: log('stop_interrupt_receiving %02x' % p[0]); send(17, pid, bytes([0, p[0]])); irq_on = False
            elif typ == 100:
                ep, req, rt, st, val, idx, length = struct.unpack('<BBBBHHH', p[:10])
                data = p[10:]
                out = b''; status = 0
                if rt == 0x80 and req == 6:
                    dt, di = val >> 8, val & 0xFF
                    out = DEV if dt == 1 else CFG if dt == 2 else strdesc(di) if dt == 3 else b''
                    if not out: status = 4
                    out = out[:length]
                elif rt == 0xC1 or rt == 0xC0: out = bytes(length)
                log('control rt=%02x req=%02x val=%04x idx=%04x len=%d -> %d bytes st %d %s' % (rt, req, val, idx, length, len(out), status, data.hex()))
                send(100, pid, struct.pack('<BBBBHHH', ep, req, rt, status, val, idx, len(out)), out)
            elif typ == 103:
                ep, st, length = struct.unpack('<BBH', p[:4])
                log('interrupt OUT ep %02x: %s' % (ep, p[4:].hex()))
                send(103, pid, struct.pack('<BBH', ep, 0, length))
            else: log('ignored type', typ, p.hex())
        pad.update()
        if irq_on:
            rep = pad.report(); now = time.time()
            if rep != pad.sent or now - last > 0.25:   # like the real pad: on change (plus a keep-alive here)
                send(103, 0x10000 + nid, struct.pack('<BBH', 0x81, 0, len(rep)), rep); nid += 1
                pad.sent = rep; last = now

if __name__ == '__main__':
    main()

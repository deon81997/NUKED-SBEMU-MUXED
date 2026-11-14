#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
"""fakemidi.py - a pretend class-compliant USB MIDI interface (like the M-Audio Uno, 0763:0150) for QEMU
   (usbredir protocol, device side; built on fakepad.py).

   python3 fakemidi.py PORT [LOG]   listens on 127.0.0.1:PORT; QEMU connects with
       -chardev socket,id=midi,host=127.0.0.1,port=PORT -device usb-redir,chardev=midi,bus=<controller>.0
   Every bulk OUT packet is logged (raw 4-byte USB-MIDI event packets and the MIDI bytes in them) to LOG
   (default fakemidi.log next to this file); a line "MIDI 90 3C 64" per event.
   FAKEMIDI_SPEED=high   announce a high-speed device (512-byte bulk endpoints: EHCI without companions, xHCI)
   FAKEMIDI_IN=t:hex,..  MIDI IN events sent t seconds after the guest first asks the IN endpoint, e.g.
                         "1:90 40 50,1.5:80 40 00" (each one 4-byte event packet, CIN from the status byte)
   FAKEMIDI_NAK=n        hold every n-th OUT packet back 50 ms (a slow device: the guest sees NAKs)
   Default = the real M-Audio USB Uno 0763:0150 rev 1.25 (usb-devices dump, xed.ch/help/midi.html): bcdUSB 1.00,
   ep0 64 bytes, no serial, self powered 0 mA (Atr C0), bulk IN 81h listed BEFORE bulk OUT 02h, 64 bytes, bInterval 0;
   OUT paced like its 31250-baud UART (the transfer completes only when the bytes would be on the wire: NAKs), IN never
   answers unless FAKEMIDI_IN is set.  FAKEMIDI_OLD=1: the beta.108 shape (bcdUSB 1.01, ep0 8, OUT before IN, no pacing).
   FAKEMIDI_PID=1010     the old MidiSport Uno/1x1 before its firmware is loaded (EZ-USB, vendor class FFh, no MIDI
                         interface): the guest must refuse it with a reason, not hang.
   FAKEMIDI_BAD=zero|long|nojack  broken descriptors: a bLength 0 inside the config, wTotalLength past the end, MS
                         interface without endpoints - the guest must skip the device, never hang.
   FAKEMIDI_PACE=0       no UART pacing (QEMU's OHCI keeps one packet in flight and stops sending a multi-packet OUT
                         when an answer comes late - QEMU only; real OHCIs retry NAKed packets in hardware)
   FAKEMIDI_OUT=nak|stall  every bulk OUT is never answered (NAK for ever) / answered with STALL: the guest must
                         drop the device with a logged reason and keep running."""
import os, socket, struct, sys, time, select

HERE = os.path.dirname(os.path.abspath(__file__))
LOGF = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'fakemidi.log')
LOG = open(LOGF, 'w')
T0 = time.time()
def log(*a):
    LOG.write('%8.3f ' % (time.time() - T0) + ' '.join(str(x) for x in a) + '\n'); LOG.flush()

HIGH = os.environ.get('FAKEMIDI_SPEED') == 'high'
OLD = os.environ.get('FAKEMIDI_OLD') == '1'
PID = int(os.environ.get('FAKEMIDI_PID', '0150'), 16)
BAD = os.environ.get('FAKEMIDI_BAD', '')
BMPS = 512 if HIGH else 64
MPS0 = 64 if HIGH or not OLD else 8
if HIGH: BCD = 0x0200
elif OLD: BCD = 0x0101
else: BCD = 0x0100
DEV = bytes([0x12, 0x01]) + struct.pack('<H', BCD) + bytes([0xFF if PID == 0x1010 else 0, 0, 0, MPS0]) + \
      struct.pack('<HHH', 0x0763, PID, 0x0100 if OLD else 0x0125) + bytes([1, 2, 0, 1])
AC = bytes([9, 4, 0, 0, 0, 1, 1, 0, 0]) + bytes([9, 0x24, 1, 0x00, 0x01, 9, 0, 1, 1])
MS_EXTRA = bytes([6, 0x24, 2, 1, 1, 0]) + bytes([6, 0x24, 2, 2, 2, 0]) + \
           bytes([9, 0x24, 3, 1, 3, 1, 2, 1, 0]) + bytes([9, 0x24, 3, 2, 4, 1, 1, 1, 0])
MS_HDR = bytes([7, 0x24, 1, 0x00, 0x01]) + struct.pack('<H', 7 + len(MS_EXTRA) + 9 + 5 + 9 + 5)
EP_OUT = bytes([9, 5, 0x02, 2]) + struct.pack('<H', BMPS) + bytes([0, 0, 0]) + bytes([5, 0x25, 1, 1, 1])
EP_IN = bytes([9, 5, 0x81, 2]) + struct.pack('<H', BMPS) + bytes([0, 0, 0]) + bytes([5, 0x25, 1, 1, 3])
MS = bytes([9, 4, 1, 0, 0 if BAD == 'nojack' else 2, 1, 3, 0, 0]) + MS_HDR + MS_EXTRA + \
     (b'' if BAD == 'nojack' else (EP_OUT + EP_IN if OLD else EP_IN + EP_OUT))
if PID == 0x1010:                       # MidiSport before firmware: one vendor interface, a few bulk endpoints
    body = bytes([9, 4, 0, 0, 2, 0xFF, 0, 0, 0]) + bytes([7, 5, 0x81, 2, 64, 0, 0]) + bytes([7, 5, 0x02, 2, 64, 0, 0])
else: body = AC + MS
if BAD == 'zero': body = body[:20] + bytes([0, 0x24, 0, 0]) + body[20:]
CFG = bytes([9, 2]) + struct.pack('<H', 9 + len(body) + (200 if BAD == 'long' else 0)) + \
      bytes([2 if PID != 0x1010 else 1, 1, 0, 0x80 if OLD else 0xC0, 50 if OLD else 0]) + body
STR = {0: bytes([4, 3, 0x09, 0x04]), 1: 'M-Audio'.encode('utf-16-le'), 2: 'USB Uno MIDI Interface'.encode('utf-16-le')}
CNT = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1]

def strdesc(i):
    s = STR.get(i, b'')
    return s if i == 0 else bytes([2 + len(s), 3]) + s

def in_events():
    ev = []
    for item in filter(None, os.environ.get('FAKEMIDI_IN', '').split(',')):
        t, hx = item.split(':')
        b = bytes.fromhex(hx.replace(' ', ''))
        cin = b[0] >> 4 if b[0] < 0xF0 else 0xF
        ev.append((float(t), bytes([cin]) + b + bytes(3 - len(b))))
    return ev

def main():
    port = int(sys.argv[1])
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port)); srv.listen(1)
    log('listening', port, 'high speed' if HIGH else 'full speed')
    while True:                         # every connection is a new plug-in (QEMU device_add / device_del)
        c, _ = srv.accept(); log('qemu connected')
        try: session(c)
        except (ConnectionError, OSError) as e: log('connection ended', e)
        c.close()

def session(c):
    wide = [False]
    def send(typ, pid, hdr, data=b''):
        h = struct.pack('<IIQ' if wide[0] else '<III', typ, len(hdr) + len(data), pid)
        c.sendall(h + hdr + data)
    caps = (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)
    send(0, 0, b'fakemidi 1.0'.ljust(64, b'\0') + struct.pack('<I', caps))
    buf = b''
    while True:
        buf += c.recv(65536)
        if len(buf) >= 12:
            typ, ln, pid = struct.unpack('<III', buf[:12])
            if len(buf) >= 12 + ln: break
    peer = struct.unpack('<I', buf[12 + 64:12 + 68])[0] if ln >= 68 else 0
    log('hello from qemu', buf[12:12 + 64].rstrip(b'\0'), 'caps %x' % peer)
    buf = buf[12 + ln:]
    wide[0] = bool(peer & (1 << 5))
    def device_info():
        ifs = bytes([0, 1] + [0] * 30) + bytes([1, 1] + [0] * 30) + bytes([1, 3] + [0] * 30) + bytes([0, 0] + [0] * 30)
        if PID == 0x1010: ifs = bytes([0, 0xFF] + [0] * 30) + bytes([0] * 32) + bytes([0] * 32) + bytes([0] * 32)
        send(4, 0, struct.pack('<I', 1 if PID == 0x1010 else 2) + ifs)
        typ = [255] * 32; itv = [0] * 32; itf = [0] * 32; mps = [0] * 32
        def ep(addr, t, i, intf, m):
            k = ((addr & 0x80) >> 3) | (addr & 0x0F); typ[k] = t; itv[k] = i; itf[k] = intf; mps[k] = m
        ep(0x00, 0, 0, 0, MPS0); ep(0x80, 0, 0, 0, MPS0)
        ep(0x02, 2, 0, 1, BMPS); ep(0x81, 2, 0, 1, BMPS)
        send(5, 0, bytes(typ) + bytes(itv) + bytes(itf) + struct.pack('<32H', *mps) + bytes(32 * 4))
    device_info()
    send(1, 0, struct.pack('<BBBBHHH', 2 if HIGH else 1, DEV[4], 0, 0, 0x0763, PID, 0x0100 if OLD else 0x0125))
    log('device connect sent')
    cfg = 0; pend_in = []; first_in = None; events = in_events(); held = []
    nak = int(os.environ.get('FAKEMIDI_NAK', '0')); nout = 0; wire = [0.0]
    hl = 16 if wide[0] else 12
    bh = '<BBHIH'                       # bulk packet header with 32-bit lengths (cap 6)
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
            if typ == 3: log('reset')
            elif typ == 6:
                cfg = p[0]; log('set_configuration', cfg)
                send(8, pid, bytes([0, cfg])); device_info()
            elif typ == 7: send(8, pid, bytes([0, cfg]))
            elif typ in (9, 10): send(11, pid, bytes([0, p[0], 0]))
            elif typ == 100:
                ep, req, rt, st, val, idx, length = struct.unpack('<BBBBHHH', p[:10])
                out = b''; status = 0
                if rt == 0x80 and req == 6:
                    dt, di = val >> 8, val & 0xFF
                    out = DEV if dt == 1 else CFG if dt == 2 else strdesc(di) if dt == 3 else b''
                    if not out: status = 4
                    out = out[:length]
                elif rt == 0x80 and req == 0: out = bytes(2)[:length]
                elif rt == 0x02 and req == 1: log('clear_feature halt ep %02x' % idx)
                log('control rt=%02x req=%02x val=%04x idx=%04x len=%d -> %d bytes st %d' % (rt, req, val, idx, length, len(out), status))
                send(100, pid, struct.pack('<BBBBHHH', ep, req, rt, status, val, idx, len(out)), out)
            elif typ == 101:
                ep, st, length, sid, lhi = struct.unpack(bh, p[:10])
                length |= lhi << 16
                if ep & 0x80:
                    if first_in is None: first_in = time.time(); log('first bulk IN request, %d bytes' % length)
                    pend_in.append((pid, ep, length))
                else:
                    data = p[10:]; nout += 1
                    log('OUT %d bytes: %s' % (len(data), data.hex()))
                    for i in range(0, len(data) - 3, 4):
                        n = CNT[data[i] & 15]
                        if n: log('MIDI ' + ' '.join('%02X' % x for x in data[i + 1:i + 1 + n]))
                    om = os.environ.get('FAKEMIDI_OUT', '')
                    if om == 'nak': continue        # never answered: the guest sees NAKs for ever
                    if om == 'stall': send(101, pid, struct.pack(bh, ep, 4, 0, 0, 0)); continue
                    if not OLD and not HIGH and os.environ.get('FAKEMIDI_PACE') != '0':   # the Uno's UART: 10 bits a byte at 31250 baud
                        nb = sum(CNT[data[i] & 15] for i in range(0, len(data) - 3, 4))
                        wire[0] = max(wire[0], time.time()) + nb * 0.00032
                        held.append((wire[0], pid, ep, len(data)))
                    elif nak and nout % nak == 0: held.append((time.time() + 0.05, pid, ep, len(data)))
                    else: send(101, pid, struct.pack(bh, ep, 0, len(data) & 0xFFFF, 0, len(data) >> 16))
            elif typ == 21:
                pend_in = [x for x in pend_in if x[0] != pid]       # the packet's own id is the one cancelled
                log('cancel', pid)
            else: log('ignored type', typ, p.hex())
        now = time.time()
        for h in [x for x in held if x[0] <= now]:
            held.remove(h); send(101, h[1], struct.pack(bh, h[2], 0, h[3] & 0xFFFF, 0, h[3] >> 16))
        if first_in is not None and events and pend_in and now - first_in >= events[0][0]:
            pk = events.pop(0)[1]; pid, ep, length = pend_in.pop(0)
            log('IN sent', pk.hex())
            send(101, pid, struct.pack(bh, ep, 0, len(pk), 0, 0), pk)

if __name__ == '__main__':
    main()

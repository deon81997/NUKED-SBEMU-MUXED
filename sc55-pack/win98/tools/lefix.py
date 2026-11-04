#!/usr/bin/env python3
"""Post-link fixes for SC55W.VXD.
  - mark every LE object 0x2045: Windows 98's VxD loader refuses the
    writable+preload objects (0x2043) that wlink emits for data
  - optional: rename the device (DDB name) for development builds, so a new
    build loads even while an older one is still resident
usage: lefix.py file.vxd [DDBNAME]"""
import struct, sys
f = sys.argv[1]
d = bytearray(open(f, 'rb').read())
le = struct.unpack_from('<I', d, 0x3c)[0]
otab = struct.unpack_from('<I', d, le + 0x40)[0]
n = struct.unpack_from('<I', d, le + 0x44)[0]
for i in range(n):
    struct.pack_into('<I', d, le + otab + i * 24 + 8, 0x2045)
if len(sys.argv) > 2:
    i = d.find(b'SC55W   ')
    assert i > 0, 'DDB name not found'
    d[i:i + 8] = sys.argv[2].ljust(8)[:8].encode()
    # module name in the resident-name table too (VXDLDR matches on it)
    rn = struct.unpack_from('<I', d, le + 0x58)[0]
    l = d[le + rn]
    name = sys.argv[2][:l].ljust(l).encode()
    d[le + rn + 1:le + rn + 1 + l] = name
open(f, 'wb').write(d)
print(f'{f}: {n} objects set to 0x2045' + (f', device name {sys.argv[2]}' if len(sys.argv) > 2 else ''))

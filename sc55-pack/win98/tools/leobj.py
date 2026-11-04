import struct,sys
for f in sys.argv[1:]:
    d=open(f,'rb').read(); le=struct.unpack_from('<I',d,0x3c)[0]
    if d[le:le+2]!=b'LE': print(f,'not LE'); continue
    otab=struct.unpack_from('<I',d,le+0x40)[0]; n=struct.unpack_from('<I',d,le+0x44)[0]
    print(f, 'objects',n)
    for i in range(n):
        vs,base,fl,pi,np,_=struct.unpack_from('<6I',d,le+otab+i*24)
        print('   obj%d vsize=%x base=%x flags=%04x pages=%d'%(i+1,vs,base,fl,np))

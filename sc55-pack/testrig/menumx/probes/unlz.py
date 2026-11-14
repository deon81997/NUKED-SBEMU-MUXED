import sys,struct
b=open(sys.argv[1],'rb').read()
hdr=struct.unpack('<H',b[8:10])[0]*16
d=b[hdr:]; pos=0
def rw():
    global pos; v=d[pos]|d[pos+1]<<8; pos+=2; return v
def rb():
    global pos; v=d[pos]; pos+=1; return v
buf=rw(); cnt=16
def bit():
    global buf,cnt
    x=buf&1; cnt-=1
    if cnt==0: buf=rw(); cnt=16
    else: buf>>=1
    return x
out=bytearray()
while True:
    if bit(): out.append(rb()); continue
    if not bit():
        ln=(bit()<<1|bit())+2; span=rb()-256
    else:
        lo=rb(); hi=rb()
        span=(lo|((hi&0xF8)<<5)|0xE000)-0x10000
        ln=(hi&7)+2
        if ln==2:
            ln=rb()
            if ln==0: break
            if ln==1: continue
            ln+=1
    for i in range(ln): out.append(out[len(out)+span])
open(sys.argv[2],'wb').write(out); print(len(out), 'consumed', pos)

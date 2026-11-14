import zipfile, sys, os
R='/home/claude/nuked-sbemu-muxed'; O=R+'/sc55-pack/output/'; P=R+'/sc55-pack/pack/'
src=R+'/release/NUKED-SB-EMU-1.0.0-beta.110-usb.zip'; dst=sys.argv[1]
swap={}
for e in 'gametest joytest kmtest midplay mputest padconv sbdiag sbemu sbset sc55log sc55menu sc55wav timetest usbdrv usbtest'.split():
    swap['NUKEDSB/FILES/SC55/%s.EXE'%e.upper()]=O+e+'.exe'
swap['NUKEDSB/FILES/CDSET/CDGUI.EXE']=O+'cdgui.exe'
swap['NUKEDSB/FILES/SC55/BUGREP.BAT']=P+'FILES/SC55/BUGREP.BAT'
swap['NUKEDSB/README.TXT']=P+'README.TXT'
for a in sys.argv[2:]:
    k,v=a.split('=',1); swap[k]=v
zi=zipfile.ZipFile(src); zo=zipfile.ZipFile(dst,'w',zipfile.ZIP_DEFLATED,compresslevel=9)
done=set()
for i in zi.infolist():
    if i.filename in swap:
        d=open(swap[i.filename],'rb').read(); done.add(i.filename)
        ni=zipfile.ZipInfo(i.filename, date_time=__import__("time").localtime()[:6]); ni.external_attr=i.external_attr; ni.compress_type=zipfile.ZIP_DEFLATED
        zo.writestr(ni,d)
    else: zo.writestr(i, zi.read(i.filename))
print('swapped',len(done),'missing',set(swap)-done)

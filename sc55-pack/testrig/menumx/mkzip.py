# candidate zip: newest release zip with the changed files swapped in
import zipfile, sys, os, glob
R='/home/claude/nuked-sbemu-muxed'; O=R+'/sc55-pack/output/'; P=R+'/sc55-pack/pack/'
src=sorted(glob.glob(R+'/release/*.zip'))[-1]; dst=sys.argv[1]
swap={'NUKEDSB/FILES/SC55/SC55MENU.EXE':O+'sc55menu.exe','NUKEDSB/FILES/SC55/SBEMU.EXE':O+'sbemu.exe',
 'NUKEDSB/FILES/SC55/SBSET.EXE':O+'sbset.exe','NUKEDSB/FILES/SC55/SC55RUN.BAT':P+'FILES/SC55/SC55RUN.BAT',
 'NUKEDSB/FILES/CDSET/HYBRID.BAT':P+'FILES/CDSET/HYBRID.BAT','NUKEDSB/FILES/CDSET/CDON.BAT':P+'FILES/CDSET/CDON.BAT',
 'NUKEDSB/SETUP.BAT':P+'SETUP.BAT','NUKEDSB/CDINST.BAT':P+'CDINST.BAT','NUKEDSB/README.TXT':P+'README.TXT'}
for a in sys.argv[2:]: k,v=a.split('='); swap[k]=v
zi=zipfile.ZipFile(src); zo=zipfile.ZipFile(dst,'w',zipfile.ZIP_DEFLATED)
names=set(zi.namelist())
for i in zi.infolist():
    if i.filename in swap:
        b=open(swap[i.filename],'rb').read()
        if i.filename.upper().endswith('.BAT'): b=b''.join(l for l in b.splitlines(True) if b'SPDX-License-Identifier' not in l)
        zo.writestr(i, b)
    else: zo.writestr(i, zi.read(i.filename))
for k,v in swap.items():
    if k not in names: print('not in zip:',k)
zo.close(); print('wrote',dst,'from',os.path.basename(src))

import zipfile,os,sys
src,dst=sys.argv[1],sys.argv[2]; O='sc55-pack/output/'
z=zipfile.ZipFile(src); w=zipfile.ZipFile(dst,'w',zipfile.ZIP_DEFLATED,compresslevel=9); sw=0
for i in z.infolist():
    n=i.filename; b=os.path.basename(n); f=None
    if n=='NUKEDSB/README.TXT': f='sc55-pack/pack/README.TXT'
    elif n=='NUKEDSB/FILES/SC55/BUGREP.BAT': f='sc55-pack/pack/FILES/SC55/BUGREP.BAT'
    elif (n.startswith('NUKEDSB/FILES/SC55/') or n=='NUKEDSB/FILES/CDSET/CDGUI.EXE') and b and os.path.isfile(O+b.lower()): f=O+b.lower()
    if f: w.writestr(i,open(f,'rb').read()); sw+=1
    else: w.writestr(i,z.read(n))
w.close(); print('swapped',sw)

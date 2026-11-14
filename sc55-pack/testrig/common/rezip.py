import zipfile,os,sys
# rezip.py OLD NEW (repo root, after make): swaps rebuilt EXEs, README.TXT and every NUKEDSB/FILES/SC55 file that
# exists in sc55-pack/pack/FILES/SC55 (BATs), and adds pack/FILES/SC55 files the old zip lacks (beta.119: P5TEST.BAT).
src,dst=sys.argv[1],sys.argv[2]; O='sc55-pack/output/'; P='sc55-pack/pack/FILES/SC55/'
z=zipfile.ZipFile(src); w=zipfile.ZipFile(dst,'w',zipfile.ZIP_DEFLATED,compresslevel=9); sw=0; seen=set()
for i in z.infolist():
    n=i.filename; b=os.path.basename(n); f=None
    if n.startswith('NUKEDSB/FILES/SC55/'): seen.add(b.upper())
    if n=='NUKEDSB/README.TXT': f='sc55-pack/pack/README.TXT'
    elif n.startswith('NUKEDSB/FILES/SC55/') and b and os.path.isfile(P+b): f=P+b
    elif (n.startswith('NUKEDSB/FILES/SC55/') or n=='NUKEDSB/FILES/CDSET/CDGUI.EXE') and b and os.path.isfile(O+b.lower()): f=O+b.lower()
    if f: w.writestr(i,open(f,'rb').read()); sw+=1
    else: w.writestr(i,z.read(n))
add=[b for b in sorted(os.listdir(P)) if b.upper() not in seen and os.path.isfile(P+b)]
for b in add: w.write(P+b,'NUKEDSB/FILES/SC55/'+b); sw+=1
NEW=['wcset.exe']  # new tools the old zip lacks (beta.120: WCSET.EXE)
for b in NEW:
    if b.upper() not in seen: w.write(O+b,'NUKEDSB/FILES/SC55/'+b.upper()); sw+=1; add.append(b.upper())
w.close(); print('swapped/added',sw,'new:',add)

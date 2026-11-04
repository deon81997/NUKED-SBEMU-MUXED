import re,sys
# enumerate services in DDK .inc service tables (sequential numbering)
for fn in sys.argv[1:]:
    lines=open(fn,errors='replace').read().split('\n')
    dev=None; n=0; depth_skip=0
    for l in lines:
        s=l.split(';')[0].strip()
        m=re.match(r'Begin_Service_Table\s+(\w+)',s)
        if m: dev=m.group(1); n=0; continue
        if re.match(r'End_Service_Table',s): dev=None; continue
        if dev:
            m=re.match(r'(\w+_Service|\w+_StdCall_Service|\w+_FastCall_Service)\s+(\w+)',s)
            if m: print(f'{dev} {n:#06x} {m.group(2)}'); n+=1
            elif s and not s.startswith(('IF','ELSE','ENDIF','if','else','endif','.','Begin','End')): print('??',dev,s)

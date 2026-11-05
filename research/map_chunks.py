#!/usr/bin/env python3
"""Walk EMAP chunk streams. chunk = tag(4) u32 ver u32 total_len(incl. 12-byte header)."""
import struct, sys, glob, collections
def chunks(d):
    off=0; out=[]
    while off<len(d):
        tag=d[off:off+4]; ver,ln=struct.unpack_from('<II',d,off+4)
        if ln<12 or off+ln>len(d): raise ValueError(f'bad chunk {tag} ln={ln} at {off}')
        out.append((tag.decode('latin1'),ver,off,ln,d[off+12:off+ln])); off+=ln
    return out
if __name__=='__main__':
    ok=0; seq=collections.Counter(); vers=collections.Counter()
    for f in sorted(glob.glob('assets/Maps/*.map')):
        d=open(f,'rb').read()
        try: cs=chunks(d); ok+=1
        except Exception as e: print('FAIL',f,e); continue
        for c in cs: vers[(c[0],c[1])]+=1
        seq[' '.join(c[0] for c in cs[:6])]+=1
    print('ok',ok); print(vers); 
    for k,v in seq.most_common(8): print(v,k)

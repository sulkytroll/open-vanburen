#!/usr/bin/env python3
"""Granny2 (.gr2-style) container reader for the Van Buren .skel/.anim files: header, sections, relocations (fixups)."""
import struct,sys,glob,collections
MAGIC=bytes.fromhex('b867b0caf86db10f84728c7e5e19001e')
class Gr2:
    def __init__(s,path):
        d=s.d=open(path,'rb').read()
        assert d[:16]==MAGIC,'magic'
        s.header_size,s.header_fmt=struct.unpack_from('<II',d,16)
        (s.version,s.total_size,s.crc,s.sections_off,s.nsec,s.root_type_sec,s.root_type_off,s.root_sec,s.root_off,s.tag)=struct.unpack_from('<IIIIIIIIII',d,32)
        s.extra=struct.unpack_from('<4I',d,72)
        s.secs=[]
        for i in range(s.nsec):
            o=32+s.sections_off+i*44
            f=struct.unpack_from('<11I',d,o)
            s.secs.append(dict(comp=f[0],off=f[1],size=f[2],exp=f[3],align=f[4],f16=f[5],f8=f[6],reloff=f[7],nrel=f[8],mmoff=f[9],nmm=f[10]))
if __name__=='__main__':
    c=collections.Counter(); v=collections.Counter()
    for p in sorted(glob.glob('../assets/**/*.skel',recursive=True)+glob.glob('../assets/**/*.anim',recursive=True)):
        try:
            g=Gr2(p); v[(g.version,g.header_fmt,g.header_size)]+=1
            for s in g.secs: c[s['comp']]+=1
        except Exception as e: print('FAIL',p,e)
    print('versions',v); print('section compression types',c)

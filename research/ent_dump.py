#!/usr/bin/env python3
"""Dump the chunk structure of entity template files (.critter/.door/.item/.weapon/.armor/.ammo/.use/.con). len INCLUDES the 12-byte header."""
import struct,sys,re
def lp(d,p):
    n=struct.unpack_from('<H',d,p)[0]; return d[p+2:p+2+n].decode('latin1'),p+2+n
def chunks(b,p=0):
    out=[]
    while p<len(b):
        tag=b[p:p+4].decode('latin1'); ver,ln=struct.unpack_from('<II',b,p+4); assert ln>=12 and p+ln<=len(b),(tag,ln,p,len(b)); out.append((tag,ver,b[p+12:p+ln])); p+=ln
    return out
if __name__=='__main__':
    for f in sys.argv[1:]:
        d=open(f,'rb').read(); cs=chunks(d)
        print('==',f.split('/')[-1],len(d))
        for t,v,pl in cs:
            print('  ',t,'v',v,len(pl),pl[:120].hex(' '), [x.decode() for x in re.findall(rb'[\x20-\x7e]{4,}',pl)])

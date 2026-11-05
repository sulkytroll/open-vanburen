#!/usr/bin/env python3
"""Van Buren .gui (GUI V1.0..V1.3) parser. Grammar from F3.exe: loader FUN_00495b50, Window::Load FUN_004b10f0, Button 0x4a6b60, Label 0x4aa640, Picture 0x4ab190, fragment 0x4b20a0.
File: "GUI " "V1.x" | string name | u8 | u32 topCount | topCount x (u32 type, Window). Window types: 0 window, 1 button, 2 picture, 3 label, 4 (label subclass)."""
import struct,sys,glob,os
class R:
    def __init__(s,d): s.d=d; s.p=0
    def u8(s): v=s.d[s.p]; s.p+=1; return v
    def u32(s): v=struct.unpack_from('<I',s.d,s.p)[0]; s.p+=4; return v
    def i32(s): v=struct.unpack_from('<i',s.d,s.p)[0]; s.p+=4; return v
    def f32(s): v=struct.unpack_from('<f',s.d,s.p)[0]; s.p+=4; return v
    def rect(s): return tuple(s.i32() for _ in range(4))
    def string(s):
        n=s.u32(); assert n<4096,(n,s.p); v=s.d[s.p:s.p+n].decode('latin1'); s.p+=n; return v
def fragment(r,v):
    f={}
    f['flags']=r.u32(); f['w']=r.u32(); f['h']=r.u32(); f['u3']=r.u32(); f['u4']=r.u32(); f['u5']=r.u32()
    f['tex']=r.string(); f['scale']=r.f32(); f['color']=r.u32(); f['rect']=r.rect()
    return f
def window(r,v,typ):
    w={'type':typ}
    if typ==1:   # button preamble
        w['btnId']=r.u32()
        if v>=4: w['checked']=r.u8()
        w['snd']=[r.string(),r.string(),r.string()]
    w['rect0']=r.rect(); w['name']=r.string(); w['a']=r.u8(); w['b']=r.u8(); w['c']=r.u8(); w['ini']=r.string(); w['color']=r.u32(); w['rect']=r.rect()
    if v>=2: w['d']=r.u8(); w['e']=r.u32()
    if v>=3: w['s1']=r.string(); w['n1']=r.i32(); w['s2']=r.string(); w['n2']=r.i32(); w['s3']=r.string()
    w['frags']=[fragment(r,v) for _ in range(9)]
    w['strings']={}
    while True:
        i=r.u32()
        if i==0xffffffff: break
        w['strings'][i]=r.string()
    n=r.u32(); w['children']=[]
    for _ in range(n):
        t=r.u32(); w['children'].append(window(r,v,t))
    if typ in (3,4):   # label: its own fields follow the base window (children included)
        w['font']=r.u32(); w['text']=r.string(); w['l']=[r.u32() for _ in range(4)]; w['lb']=[r.u8(),r.u8()]; w['l2']=r.u32(); w['l3']=[r.u32() for _ in range(4)]
        w['l4']=r.u32()
    return w
def parse(path):
    d=open(path,'rb').read(); r=R(d); assert d[:4]==b'GUI '; ver={b'V1.0':1,b'V1.1':2,b'V1.2':3,b'V1.3':4}[d[4:8]]; r.p=8
    name=r.string(); r.u8(); n=r.u32(); wins=[]
    for _ in range(n):
        t=r.u32(); wins.append(window(r,ver,t))
    return {'name':name,'ver':ver,'windows':wins,'end':r.p,'size':len(d)}
def dump(w,ind=0):
    print('  '*ind+'%s %s rect=%s ini=%s col=%08x kids=%d'%({0:'win',1:'btn',2:'pic',3:'lbl',4:'lbl4'}[w['type']],w['name'],w['rect'],w['ini'],w['color'],len(w['children']))+(' text=%r font=%d'%(w['text'],w['font']) if 'text' in w else ''))
    for c in w['children']: dump(c,ind+1)
if __name__=='__main__':
    ok=0
    for f in sorted(glob.glob('assets/Interface/*.gui')):
        try:
            g=parse(f); st='OK ' if g['end']==g['size'] else 'LEFT %d'%(g['size']-g['end'])
        except Exception as e: st='FAIL %s'%e; g=None
        print(os.path.basename(f),st)
        if g and g['end']==g['size']: ok+=1
    print(ok,'ok')
    if len(sys.argv)>1:
        g=parse(sys.argv[1]); [dump(w) for w in g['windows']]

#!/usr/bin/env python3
"""Strict decoders for simple EMAP chunk types; each must consume its payload exactly."""
import struct, glob, collections, sys
sys.path.insert(0,'.')
import map_chunks as mc
class R:
    def __init__(s,b): s.b=b; s.o=0
    def u8(s): v=s.b[s.o]; s.o+=1; return v
    def u32(s): v,=struct.unpack_from('<I',s.b,s.o); s.o+=4; return v
    def f(s,n=1):
        v=struct.unpack_from(f'<{n}f',s.b,s.o); s.o+=4*n; return v if n>1 else v[0]
    def s(s):
        n,=struct.unpack_from('<H',s.b,s.o); v=s.b[s.o+2:s.o+2+n].decode('latin1'); s.o+=2+n; return v
    def done(s): return s.o==len(s.b)
def EMEF(r):
    name=r.s(); pos=r.f(3); rot=r.f(3); n=r.u8()
    return dict(name=name,pos=pos,rot=rot,effects=[r.s() for _ in range(n)],flag=r.u8())
def EMSD(r): return dict(name=r.s(),pos=r.f(3),sound=r.s(),a=r.u8(),b=r.u8())
def ESTR(r): return dict(script=r.s(),z=r.u32())
def ETTR(r): return dict(map=r.s(),a=r.u8(),b=r.u8())
def EMNO(r): return dict(pos=r.f(3),icon=r.s(),a=r.u8(),b=r.u8(),c=r.u8(),d=r.u8())
def EPTH(r):
    name=r.s(); n=r.u32(); return dict(name=name,pts=[(r.f(3),r.f(3)) for _ in range(n)])
def EMTR(r):
    t=r.u32(); n=r.u32(); return dict(type=t,verts=[r.f(3) for _ in range(n)])
def EBTR(r): return dict(id=r.u32(),rgb=(r.u8(),r.u8(),r.u8()))
def ECAM(r): return dict(v=r.f(4))
def EMNP(r):
    n=r.u32() if len(r.b)>=4 else 0
    out=[]
    for _ in range(n):
        out.append(dict(flag=r.u8(),pos=r.f(3),z=r.f(3),links=[r.u8() for _ in range(5)]))
    return dict(nodes=out)
def EMEP(r):
    # u8 id, 6 x vec3 positions, 6 x f32 (angles). A legacy 17-byte variant (id + 4 f32) exists in Test_Mesa_2.
    i=r.u8()
    if len(r.b)==17: return dict(id=i,legacy=r.f(4))
    return dict(id=i,pos=[r.f(3) for _ in range(6)],ang=r.f(6))
def EMAP(r):
    d=dict(id=r.u32(),s1=r.s(),s2=r.s(),ambient_map=r.s(),s4=r.s(),ambient_rgb=(r.u8(),r.u8(),r.u8()),
           f1=r.u8(),light_rgba=(r.u8(),r.u8(),r.u8(),r.u8()),f2=r.u8(),fog=r.u8(),fog_rgb=(r.u8(),r.u8(),r.u8()),
           fog_params=r.f(3))
    return d
def EEOV(r,ver):
    d=dict(instance=r.s(),flags=(r.u8(),r.u8(),r.u8()),a=r.u32(),b=r.u32(),display=r.s(),script=r.s())
    d['vars']=[(r.s(),r.u32()) for _ in range(r.u32())]
    d['flag']=r.u8(); d['scale']=r.f(); d['skin']=r.s()
    d['items']=[r.s() for _ in range(r.u8())]
    if ver>=1: d['f1']=r.u8(); d['f2']=r.u8()
    if ver>=2: d['items2']=[r.s() for _ in range(r.u32())]
    return d
def W2TM(r):
    # '2MWT' water: lpstr name, 140 bytes of parameters (i32 -1, floats/flags), u32 n, n * (vec3 pos, lpstr texture, vec2 uv)
    name=r.s(); params=r.b[r.o:r.o+140]; r.o+=140
    n=r.u32(); tiles=[(r.f(3),r.s(),r.f(2)) for _ in range(n)]
    return dict(name=name,params=params,tiles=tiles)
def EME2(r):
    d=dict(entity=r.s(),pos=r.f(3),rot=r.f(3),flag=r.u8())
    t=r.b[r.o:r.o+4].decode('latin1'); ver,ln=struct.unpack_from('<II',r.b,r.o+4)
    sub=R(r.b[r.o+12:r.o+ln]); d['eeov']=EEOV(sub,ver); d['eeov_exact']=sub.done()
    r.o+=ln
    return d
DEC={'2MWT':W2TM,**dict(EMAP=EMAP,EME2=EME2,EMEF=EMEF,EMSD=EMSD,ESTR=ESTR,ETTR=ETTR,EMNO=EMNO,EPTH=EPTH,EMTR=EMTR,EBTR=EBTR,ECAM=ECAM,EMNP=EMNP,EMEP=EMEP)}
if __name__=='__main__':
    res=collections.defaultdict(lambda:[0,0,None])
    for f in sorted(glob.glob('assets/Maps/*.map')):
        for t,v,off,ln,p in mc.chunks(open(f,'rb').read()):
            if t in DEC:
                r=R(p)
                try: DEC[t](r); ok=r.done()
                except Exception as e: ok=False
                res[t][0]+=ok; res[t][1]+=1
                if not ok and res[t][2] is None: res[t][2]=(f.split('/')[-1],off,len(p),r.o)
    for t,(a,b,bad) in res.items(): print(t,a,'/',b,bad or '')

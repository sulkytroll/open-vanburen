#!/usr/bin/env python3
"""Generic Granny2 file reader: decompress sections (native oodle0 via /tmp/gr2_test is NOT used here; this uses the Unicorn oracle or cached
section dumps), apply relocations, and decode the root object with the file's own type descriptors."""
import struct,sys,subprocess,os
sys.path.insert(0,'.')
from gr2_parse import Gr2
def load_sections(path,emu=None):
    g=Gr2(path); secs=[]
    tag='/tmp/g2o/t'; os.makedirs('/tmp/g2o',exist_ok=True)
    subprocess.run(['/tmp/gr2_test',path,tag],capture_output=True)
    for i,s in enumerate(g.secs):
        if s['comp']==1 and s['size']: data=open('%s.%d'%(tag,i),'rb').read()
        elif s['comp']==0: data=g.d[s['off']:s['off']+s['size']]
        else: data=b''
        rel={}
        for k in range(s['nrel']):
            off,tsec,toff=struct.unpack_from('<III',g.d,s['reloff']+12*k); rel[off]=(tsec,toff)
        secs.append((data,rel))
    return g,secs
class Reader:
    def __init__(s,path):
        s.g,s.secs=load_sections(path)
    def ptr(s,sec,off):
        r=s.secs[sec][1].get(off)
        return r   # (tsec,toff) or None
    def u32(s,sec,off): return struct.unpack_from('<I',s.secs[sec][0],off)[0]
MT={0:'End',1:'Inline',2:'Reference',3:'RefToArray',4:'ArrayOfRefs',5:'VariantRef',6:'Unsupported',7:'RefToVariantArray',8:'String',9:'Transform',10:'Real32',11:'Int8',12:'UInt8',13:'BinormalInt8',14:'NormalUInt8',15:'Int16',16:'UInt16',17:'BinormalInt16',18:'NormalUInt16',19:'Int32',20:'UInt32',21:'Real16',22:'EmptyRef'}
def cstr(R,sec,off):
    d=R.secs[sec][0]; return d[off:d.index(b'\0',off)].decode('latin1')
def typedef(R,tsec,toff):
    """-> list of (type, name, ref(tsec,toff)|None, arraysize, extra)"""
    out=[]; d=R.secs[tsec][0]; o=toff
    while True:
        t=struct.unpack_from('<I',d,o)[0]
        if t==0: break
        name=R.ptr(tsec,o+4); ref=R.ptr(tsec,o+8); arr=struct.unpack_from('<I',d,o+12)[0]; extra=struct.unpack_from('<3I',d,o+16)
        out.append((t,cstr(R,*name) if name else '',ref,arr,extra)); o+=32
    return out
if __name__=='__main__':
    R=Reader(sys.argv[1]); g=R.g
    print('root type',g.root_type_sec,g.root_type_off,'root',g.root_sec,g.root_off)
    for t in typedef(R,g.root_type_sec,g.root_type_off): print(MT.get(t[0],t[0]),t[1:])

SIZES={10:4,11:1,12:1,13:1,14:1,15:2,16:2,17:2,18:2,19:4,20:4,21:2,22:4,8:4,2:4,3:8,4:8,5:8,7:12,9:68}
FMT={10:'f',11:'b',12:'B',13:'b',14:'B',15:'h',16:'H',17:'h',18:'H',19:'i',20:'I'}
_td={}
def td(R,ref):
    if ref not in _td: _td[ref]=typedef(R,*ref)
    return _td[ref]
def tsize(R,ref):
    return sum(msize(R,m) for m in td(R,ref))
def msize(R,m):
    t,name,ref,arr,extra=m
    n=max(arr,1)
    if t==1: return tsize(R,ref)*n
    return SIZES[t]*n
def decode(R,sec,off,ref,depth=0):
    d=R.secs[sec][0]; out={}; o=off
    for m in td(R,ref):
        t,name,mref,arr,extra=m; n=max(arr,1)
        def one(o):
            if t==1: return decode(R,sec,o,mref,depth+1)
            if t==2:
                p=R.ptr(sec,o); return decode(R,p[0],p[1],mref,depth+1) if p else None
            if t in (3,4):
                cnt=struct.unpack_from('<I',d,o)[0]; p=R.ptr(sec,o+4)
                if not p or cnt==0: return []
                items=[]
                for k in range(cnt):
                    if t==3: items.append(decode(R,p[0],p[1]+k*tsize(R,mref),mref,depth+1))
                    else:
                        q=R.ptr(p[0],p[1]+4*k); items.append(decode(R,q[0],q[1],mref,depth+1) if q else None)
                return items
            if t==5:
                tp=R.ptr(sec,o); p=R.ptr(sec,o+4)
                return decode(R,p[0],p[1],tp) if tp and p else None
            if t==7:
                tp=R.ptr(sec,o); cnt=struct.unpack_from('<I',d,o+4)[0]; p=R.ptr(sec,o+8)
                if not (tp and p) : return []
                sz=tsize(R,tp); return [decode(R,p[0],p[1]+k*sz,tp) for k in range(cnt)]
            if t==8:
                p=R.ptr(sec,o); return cstr(R,*p) if p else None
            if t==9:
                v=struct.unpack_from('<I3f4f9f',d,o); return dict(flags=v[0],pos=v[1:4],orient=v[4:8],ss=v[8:17])
            if t in FMT: return struct.unpack_from('<'+FMT[t],d,o)[0]
            if t==21: return struct.unpack_from('<H',d,o)[0]
            return None
        vals=[]
        for k in range(n):
            vals.append(one(o)); o+=msize(R,(t,name,mref,0,extra))
        out[name]=vals if arr else vals[0]
    return out
def root(R):
    g=R.g; return decode(R,g.root_sec,g.root_off,(g.root_type_sec,g.root_type_off))

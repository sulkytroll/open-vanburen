#!/usr/bin/env python3
"""Verify the flag_converted==0 ("raw") mesh encoding (Tiles) against real bytes.
Mesh body: u8,u8,u8(flag=0), u32 nverts, nverts*vertex, u32 nmat, nmat*group, footer 0b 0b 01
vertex: f32 pos[3], f32 norm[3], f32 color[4], u32 nUV, nUV*(2 f32), u32 nW, nW*W
group (same as Format A): u32 unk, lpstr key, u32 unk, lpstr tex, u32 unk_a, u8 marker(0xC9..), u32 kk,
        kk*u32 idx, kk*u8 flags
"""
import struct, sys
sys.path.insert(0,'.')
import verify_readb3d as v

def verify(path, verbose=False, wsize=None):
    d=open(path,'rb').read()
    off=v.parse_outer_tags(d,v.find_tag_start(d))
    b1,b2,b3=d[off:off+3]; off+=3
    assert b3==0, f"flag_converted={b3}"
    nverts,=struct.unpack_from('<I',d,off); off+=4
    verts=[]; nuvs=set(); nws=set()
    for i in range(nverts):
        pos=struct.unpack_from('<3f',d,off); nor=struct.unpack_from('<3f',d,off+12)
        col=struct.unpack_from('<4f',d,off+24); off+=40
        nuv,=struct.unpack_from('<I',d,off); off+=4
        nuvs.add(nuv)
        uvs=struct.unpack_from(f'<{nuv*2}f',d,off); off+=nuv*8
        nw,=struct.unpack_from('<I',d,off); off+=4
        nws.add(nw)
        if nw: off+=nw*(wsize or 0)
        verts.append((pos,nor,col,uvs))
    nmat,=struct.unpack_from('<I',d,off); off+=4
    groups=[]
    for g in range(nmat):
        unk1,=struct.unpack_from('<I',d,off); off+=4
        key,off=v.read_lpstr(d,off)
        ntex,=struct.unpack_from('<I',d,off); off+=4
        texs=[]
        for _ in range(ntex):
            t,off=v.read_lpstr(d,off); texs.append(t)
        tex=texs[0] if texs else ''
        unk_a,=struct.unpack_from('<I',d,off); off+=4
        marker=d[off]; off+=1
        assert marker==0xc9, hex(marker)
        kk,=struct.unpack_from('<I',d,off); off+=4
        idx=struct.unpack_from(f'<{kk}I',d,off); off+=4*kk
        flags=d[off:off+kk]; off+=kk
        groups.append((key,tex,kk,idx))
    rem=len(d)-off
    if verbose: print(nverts,nuvs,nws,nmat,[(g[0],g[1],g[2]) for g in groups],'remaining',rem,d[off:off+8].hex())
    return dict(nverts=nverts,nuvs=nuvs,nws=nws,nmat=nmat,rem=rem,groups=groups,verts=verts)

if __name__=='__main__':
    verify(sys.argv[1],True)

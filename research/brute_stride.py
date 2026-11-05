#!/usr/bin/env python3
"""Brute-force nverts+stride+index-layout for GD_Generic_Drop.g3d 0x0F mesh tag.
Constraint: group data must fill EXACTLY to N."""
import struct
d=open("assets/Items/GD_Generic_Drop.g3d","rb").read(); N=len(d)
def u16(o): return struct.unpack_from("<H",d,o)[0]
def u32(o): return struct.unpack_from("<I",d,o)[0]
def f32(o): return struct.unpack_from("<f",d,o)[0]

mesh=0x14b
vstart=mesh+7
print(f"vstart=0x{vstart:x} N=0x{N:x} ({N})")
found=[]
near=[]
for nv in range(40, 90):
  for stride in (56,60,64,68,72):
    vend=vstart+nv*stride
    if vend+4>N: continue
    nmat=u32(vend)
    if not (1<=nmat<=8): continue
    vp=vend+4; ok=True; groups=[]
    for gi in range(nmat):
        if vp+12>N: ok=False; break
        unk=u32(vp); nm=d[vp+4:vp+6]; mref=u32(vp+6); vp+=12
        sln=u16(vp); vp+=2
        if sln>N-vp: ok=False; break
        tex=d[vp:vp+sln].decode('latin1'); vp+=sln
        c1=u32(vp); c2=u32(vp); vp+=8
        groups.append((gi,nm.decode('latin1'),tex,c1,c2,vp))
    if not ok: continue
    hdr_end=vp
    for idxw in (2,4):
        tot_idx=sum(g[3] for g in groups)
        tot_flg=sum(g[4] for g in groups)
        end=hdr_end+tot_idx*idxw+tot_flg
        if end==N:
            found.append(("idx-first",nv,stride,nmat,idxw,tot_idx,tot_flg))
        e2=hdr_end; ok2=True
        for gi,nm,tex,c1,c2,off in groups:
            if c1==0 or c2==0: ok2=False; break
            e2+=c1*idxw+c2
        if e2==N and ok2:
            found.append(("interleave",nv,stride,nmat,idxw,tot_idx,tot_flg))
        e3=hdr_end+sum(g[3] for g in groups)+sum(g[4] for g in groups)
        if e3==N:
            found.append(("c1=idxbytes",nv,stride,nmat,idxw,tot_idx,tot_flg))
        if 0 < abs(end-N) <= 8:
            near.append(("idx-first",nv,stride,nmat,idxw,tot_idx,tot_flg,end-N,groups))

print(f"\nEXACT matches: {len(found)}")
for f in found: print("  ", f)
print(f"\nNEAR (within 8): {len(near)}")
for f in near[:20]:
    print(f"   {f[0]} nv={f[1]} stride={f[2]} nmat={f[3]} idxw={f[4]} nidx={f[5]} nfl={f[6]} diff={f[7]}")
    print(f"      groups: {[(g[1],g[2],g[3],g[4]) for g in f[8]]}")

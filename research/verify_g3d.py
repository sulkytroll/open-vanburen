#!/usr/bin/env python3
"""Verify the full G3D layout end-to-end with a script:
- tag start, all tags, mesh at stride 64, u32 indices, per-group flags.
- The decisive check: for the right stride, the 0x0F tag must consume
  EXACTLY the bytes from the tag to EOF."""
import struct, sys, os

def rd_u16(d,o): return struct.unpack_from("<H",d,o)[0]
def rd_u32(d,o): return struct.unpack_from("<I",d,o)[0]
def rd_f32(d,o): return struct.unpack_from("<f",d,o)[0]
def rd_str(d,o):
    ln = rd_u16(d,o); return d[o+2:o+2+ln], o+2+ln

def parse(path):
    d = open(path,"rb").read(); N = len(d)
    if d[:8] != b"B3D 1.1 ": return None, "bad magic"
    # tag start heuristic
    if d[8]==0x03 and d[9]==0x00: ts=37
    elif d[8]==0x6c and d[9]==0x65: ts=34
    elif d[8]==0x35 and d[9]==0x01: ts=33
    else: ts=39
    p = ts; tags=[]; meshes=[]
    while p < N:
        tag = d[p]
        if tag < 0x07 or tag > 0x0F:
            return None, f"invalid tag 0x{tag:02x} @0x{p:x}"
        if tag == 0x07:
            q=p+1; n1,q=rd_str(d,q); n2,q=rd_str(d,q); q+=72; b,q=rd_str(d,q); t,q=rd_str(d,q); q+=8
            tags.append(f"07 '{n1.decode()}/{n2.decode()}'")
            p=q
        elif tag == 0x08:
            q=p+1; q+=1; nm,q=rd_str(d,q); fn,q=rd_str(d,q); w=rd_u32(d,q); h=rd_u32(d,q); q+=8
            tags.append(f"08 '{nm.decode()}' {w}x{h}"); p=q
        elif tag == 0x0A:
            q=p+1; q+=1; nm,q=rd_str(d,q); tags.append(f"0A '{nm.decode()}'"); p=q
        elif tag == 0x0E:
            q=p+1; nm,q=rd_str(d,q); par=rd_u16(d,q); q+=2; fl=d[q]; q+=1
            q += {3:16, 2:4}.get(fl,0)
            tags.append(f"0E bone '{nm.decode()}'"); p=q
        elif tag == 0x0F:
            q=p+1; q+=2; nv=rd_u32(d,q); q+=4
            # stride search: find s such that q + nv*s + groups == N
            found=None
            for s in (56,64,72):
                ve = q + nv*s
                if ve+4>N: continue
                nmat=rd_u32(d,ve)
                if not (0<nmat<=32): continue
                vp=ve+4; ok=True; tot=0
                for mi in range(nmat):
                    if vp+22>N: ok=False;break
                    vp+=6  # u32 unk + u16 name
                    vp+=4  # u32 matref
                    sln=rd_u16(d,vp); vp+=2
                    if vp+sln>N: ok=False;break
                    vp+=sln
                    nidx=rd_u32(d,vp); nfl=rd_u32(d,vp); vp+=8
                    if nidx==0 or nfl==0 or nidx>N or nfl>N: ok=False;break
                    vp += nidx*4 + nfl
                    tot+=nidx
                if ok and vp==N:
                    found=(s,nmat,nv,tot); break
            if not found:
                return None, f"0x0F stride fail nv={nv} @0x{p:x}"
            s,nmat,nv,tot=found
            tags.append(f"0F mesh nv={nv} stride={s} nmat={nmat} idx={tot}")
            meshes.append(found)
            p=N
        else:
            return None, f"unknown tag 0x{tag:02x}"
    if not meshes:
        return None, "no mesh"
    return tags, None

if __name__=="__main__":
    if len(sys.argv)>1:
        path=sys.argv[1]
        tags,err=parse(path)
        print(path)
        if err: print("  FAIL:",err)
        else:
            for t in tags: print("  ",t)
            print("  OK")
        sys.exit(1 if err else 0)
    # batch mode: walk all g3d
    root="assets"
    files=[]
    for dp,_,fs in os.walk(root):
        for f in fs:
            if f.lower().endswith(".g3d"): files.append(os.path.join(dp,f))
    files.sort()
    print(f"total .g3d files: {len(files)}")
    ok=0; fails=[]
    for path in files:
        tags,err=parse(path)
        if err: fails.append((path,err))
        else: ok+=1
    print(f"OK: {ok}/{len(files)}")
    print(f"FAIL: {len(fails)}")
    for p,e in fails[:40]:
        print(f"  {p}  ->  {e}")

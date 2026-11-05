#!/usr/bin/env python3
"""Full correct tag walker: 07 material, 08 texture, 0A node, 0E bone, 0F mesh.
Goal: land EXACTLY at EOF, with mesh parsed."""
import struct, sys

def rd_u16(d,o): return struct.unpack_from("<H",d,o)[0]
def rd_u32(d,o): return struct.unpack_from("<I",d,o)[0]
def rd_f32(d,o): return struct.unpack_from("<f",d,o)[0]
def rd_str(d,o):
    ln = rd_u16(d,o); return d[o+2:o+2+ln].decode('latin1'), o+2+ln

def walk(path):
    d = open(path,"rb").read(); N = len(d)
    print(f"\n===== {path} (size {N}) =====")
    if d[8]==0x03 and d[9]==0x00: ts=0x25
    elif d[8]==0x6c and d[9]==0x65: ts=22
    elif d[8]==0x35 and d[9]==0x01: ts=21
    else: ts=0x27
    print(f"tag start=0x{ts:x} byte=0x{d[ts]:02x}")
    p = ts
    nmat_tags = 0; mesh_ok = False
    while p < N:
        tag = d[p]
        if tag < 0x07 or tag > 0x0F:
            print(f"  @0x{p:04x}: 0x{tag:02x} not a tag -> loop end, remaining={N-p}")
            break
        if tag == 0x07:
            q = p+1
            n1,q = rd_str(d,q); n2,q = rd_str(d,q)
            q += 18*4
            b,q = rd_str(d,q); t,q = rd_str(d,q)
            u1,u2 = rd_u32(d,q), rd_u32(d,q); q += 8
            print(f"  @0x{p:04x}: 07 MATERIAL '{n1}'/'{n2}' blend='{b}' type='{t}' u=({u1},{u2})")
            nmat_tags += 1
            p = q
        elif tag == 0x08:
            q = p+1; u8 = d[q]; q += 1
            nm,q = rd_str(d,q); fn,q = rd_str(d,q)
            w,h = rd_u32(d,q), rd_u32(d,q); q += 8
            print(f"  @0x{p:04x}: 08 TEXTURE u8={u8} '{nm}' '{fn}' {w}x{h}")
            p = q
        elif tag == 0x0A:
            q = p+1; u8 = d[q]; q += 1
            nm,q = rd_str(d,q)
            print(f"  @0x{p:04x}: 0A NODE type={u8} '{nm}'")
            p = q
        elif tag == 0x0E:
            q = p+1
            nm,q = rd_str(d,q)
            par = rd_u16(d,q); q += 2
            fl = d[q]; q += 1
            extra = 0
            if fl == 3: extra = 16
            elif fl == 2: extra = 4
            q += extra
            print(f"  @0x{p:04x}: 0E BONE '{nm}' parent={par} flag={fl} (+{extra})")
            p = q
        elif tag == 0x0F:
            q = p+1
            u8a = d[q]; q += 1
            u8b = d[q]; q += 1
            nv = rd_u32(d,q); q += 4
            print(f"  @0x{p:04x}: 0F MESH u8=({u8a},{u8b}) nverts={nv}")
            # try strides
            for s in (56,64,72):
                ve = q + nv*s
                if ve + 4 > N: continue
                nmat = rd_u32(d, ve)
                if not (0 < nmat <= 20): continue
                # walk material groups
                vp = ve + 4; ok = True; total_tris = 0
                for mi in range(nmat):
                    if vp + 20 > N: ok=False; break
                    unk = rd_u32(d,vp); vp += 4
                    nmraw = d[vp:vp+2].decode('latin1'); vp += 2
                    a = rd_u32(d,vp); vp += 4
                    pad = d[vp:vp+3]; vp += 3
                    cnt = rd_u32(d,vp); vp += 4
                    kk = rd_u32(d,vp); vp += 4
                    if kk == 0 or kk % 3 != 0:
                        ok = False
                        print(f"    grp{mi}: name2='{nmraw}' a={a} cnt={cnt} kk={kk} -> INVALID")
                        break
                    tri = kk//3
                    vp += tri      # per-vertex flags
                    vp += tri*6    # indices
                    total_tris += tri
                    print(f"    grp{mi}: name2='{nmraw}' a={a} cnt={cnt} kk={kk} tris={tri}")
                if ok:
                    print(f"    VERIFIED stride={s} nmat={nmat} total_tris={total_tris} end=0x{vp:x} (EOF=0x{N:x})")
                    p = vp
                    mesh_ok = True
                    break
            if not mesh_ok:
                print("    NO STRIDE VERIFIED")
                break
        else:
            print(f"  @0x{p:04x}: unknown 0x{tag:02x}, bytes: {' '.join(f'{d[j]:02x}' for j in range(p,p+16))}")
            break
    print(f"  FINAL: p=0x{p:x} ({p}) / {N}  exact_EOF={p==N}  materials={nmat_tags} mesh={mesh_ok}")
    return p==N

if __name__ == "__main__":
    ok = walk("assets/Items/GD_Generic_Drop.g3d")
    sys.exit(0 if ok else 1)

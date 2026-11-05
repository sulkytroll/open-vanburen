#!/usr/bin/env python3
"""Inspect the 0x0F mesh tag: nverts, stride, per-material group structure."""
import struct

d = open("assets/Items/GD_Generic_Drop.g3d","rb").read()
N = len(d)
mesh = 0x14b
nv = struct.unpack_from("<I", d, mesh+3)[0]
print(f"mesh tag @0x{mesh:x}, nverts={nv}")
vstart = mesh + 7
print(f"vertex data starts @0x{vstart:x}")

for s in (56, 64, 72):
    ve = vstart + nv*s
    nmat = struct.unpack_from("<I", d, ve)[0]
    print(f"\nstride {s}: verts_end=0x{ve:x} nmat={nmat}")
    if not (0 < nmat <= 20): continue
    vp = ve + 4
    for mi in range(min(nmat, 4)):
        print(f"  grp{mi} @0x{vp:x}:")
        for off in range(0, 24, 4):
            b = d[vp+off:vp+off+4]
            u32 = struct.unpack_from("<I", d, vp+off)[0]
            f32 = struct.unpack_from("<f", d, vp+off)[0]
            print(f"    +{off:2d}: {b.hex():<10} u32={u32} f32={f32:.5f}")
        vp += 20

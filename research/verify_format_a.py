#!/usr/bin/env python3
"""Independently verify the Format A spec from G3D_FORMAT_FINDINGS.md against
GD_Generic_Drop.g3d raw bytes. Spec under test:

  0x0F mesh (Format A, byte8==0x03):
    u8 tag, u8, u8, u32 nverts
    nverts * 56 bytes vertex (14 floats)
    u32 nmat
    nmat * {
      u32 unk, lpstr matkey, u32 unk, lpstr texname,
      u32 unk_a, u8 marker(0xC9), u8 kk, u8 pad[3],
      kk * u32 index (0-based), kk * u8 flag
    }
    footer: 0x0B 0x0B 0x01
"""
import struct
d = open("assets/Items/GD_Generic_Drop.g3d","rb").read()
N = len(d)
def u16(o): return struct.unpack_from("<H",d,o)[0]
def u32(o): return struct.unpack_from("<I",d,o)[0]
def f32(o): return struct.unpack_from("<f",d,o)[0]
def lpstr(o):
    ln = u16(o); return d[o+2:o+2+ln].decode("latin1"), o+2+ln

mesh = 0x14b
assert d[mesh] == 0x0F
nv = u32(mesh+3)
print(f"nverts={nv}")
vstart = mesh + 7
vend = vstart + nv*56
print(f"vstart=0x{vstart:x} vend=0x{vend:x} nmat={u32(vend)}")
nmat = u32(vend)
vp = vend + 4
total_tris = 0
for gi in range(nmat):
    unk = u32(vp); vp += 4
    key, vp = lpstr(vp)
    unk2 = u32(vp); vp += 4
    tex, vp = lpstr(vp)
    unka = u32(vp); vp += 4
    marker = d[vp]; vp += 1
    kk = d[vp]; vp += 1
    vp += 3  # pad
    idxs = struct.unpack_from(f"<{kk}I", d, vp); vp += kk*4
    flags = d[vp:vp+kk]; vp += kk
    tris = kk//3
    total_tris += tris
    ok_idx = all(0 <= i < nv for i in idxs)
    print(f"grp{gi}: unk={unk} key='{key}' unk2={unk2} tex='{tex}' unka={unka} "
          f"marker=0x{marker:02x} kk={kk} tris={tris}")
    print(f"       idx range=[{min(idxs)},{max(idxs)}] valid_0based={ok_idx} "
          f"flags distinct={sorted(set(flags))}")
    print(f"       first 6 idx: {list(idxs[:6])}")
print(f"\nvp after groups = 0x{vp:x}; footer bytes: {d[vp:vp+3].hex()}; "
      f"exact EOF = {vp+3 == N} (leftover={N-(vp+3)})")
print(f"total triangles = {total_tris}")

# Also sanity-check a few vertices (stride 56, 14 floats)
print("\nvertex 0 floats:", [round(f32(vstart+4*i),3) for i in range(14)])
print("vertex 47 floats:", [round(f32(vstart+(nv-1)*56+4*i),3) for i in range(14)])

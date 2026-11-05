#!/usr/bin/env python3
"""Verify the ReadB3D-derived Format B mesh-tag hypothesis against real file bytes.

Hypothesis (from decompiling Gfx_G3D_Mesh::ReadB3D / G3D_Mesh::ReadB3D in F3.exe):

  u8  flag_converted      (==1 for shipped files)
  u32 nverts
  u32 vstride
  byte vertex_blob[nverts*vstride]
  float bbox_min[3]
  float bbox_max[3]
  u8  flag_skinned
  if flag_skinned:
      u32 must_be_4
      4 * byte blob[nverts*8]
  u32 ngroups
  ngroups * {
      lpstr name1
      lpstr name2
      float ambient[4]
      float diffuse[4]
      float f3[4]
      float f4[4]
      float shininess
      lpstr blend
      lpstr type
      u8 flagA; u8 flagB; u8 flagC; float valA
      u8 flagD; float valB
      u8 flagE; float valC; float valD
      u8 flagF; float valE; float valF
      u8 flagG; float valG; float valH; float valI
      u8 flagH; float valJ; float valK; float valL; float valM
      u32 count100
      (count100>=1: lpstr)
      (count100>=2: lpstr)
      u32 boneWeightCount
      (boneWeightCount * 28-byte record)
  }
  ngroups * { lpstr texname }
  u32 indexcount[ngroups]
  u32 trianglecount[ngroups]
  ngroups * { u16 indices[indexcount[g]] }
  (3-byte footer 0b 0b 01)
"""
import struct, sys

def read_lpstr(data, off):
    n = struct.unpack_from('<H', data, off)[0]
    s = data[off+2:off+2+n].decode('latin1')
    return s, off + 2 + n

def find_tag_start(data):
    if data[8] == 0x6c and data[9] == 0x65:
        return 22
    if data[8] == 0x03 and data[9] == 0x00:
        return 0x25
    if data[8] == 0x35 and data[9] == 0x01:
        return 0x28   # [35 01] variant (CR_Sheep/Badger/Bear): one extra header byte (0x0f)
    return 0x27

def parse_outer_tags(data, off, want_mesh_only=True):
    """Walk material(0x07)/texture(0x08)/node(0x0A)/bone(0x0E) tags until mesh(0x0F)."""
    end = len(data)
    while off < end:
        tag = data[off]
        off += 1
        if tag == 0x07:
            name1, off = read_lpstr(data, off)
            name2, off = read_lpstr(data, off)
            off += 18*4
            blend, off = read_lpstr(data, off)
            mtype, off = read_lpstr(data, off)
            # u32 mask; then one or more doubles per set bit (values/bit = 1,1,2,2,3,4); then u32
            trailer_a, = struct.unpack_from('<I', data, off)
            off += 4
            for bit, cnt in enumerate((1, 1, 2, 2, 3, 4)):
                if trailer_a & (1 << bit): off += 8 * cnt
            off += 4
        elif tag == 0x08:
            off += 1
            name, off = read_lpstr(data, off)
            fname, off = read_lpstr(data, off)
            off += 8
        elif tag == 0x0A:
            off += 1
            name, off = read_lpstr(data, off)
        elif tag == 0x0E:
            name, off = read_lpstr(data, off)
            off += 2
            flags = data[off]; off += 1
            # bitmask: bit0 = position (3 floats), bit1 = axis+angle (4 floats)
            if flags & 1: off += 12
            if flags & 2: off += 16
        elif tag == 0x0F:
            return off  # position right after the mesh tag byte
        else:
            raise ValueError(f"unknown tag 0x{tag:02x} at {off-1}")
    raise ValueError("no mesh tag found")

def verify(path, verbose=True):
    data = open(path, 'rb').read()
    assert data[:8] == b'B3D 1.1 ', data[:8]
    tag_start = find_tag_start(data)
    off = parse_outer_tags(data, tag_start)
    if verbose: print(f"file={path} size={len(data)} mesh_tag_body_start={off}")

    # Format B mesh header: u8,u8,u8, u32 nverts, u32 stride  (12 bytes incl tag, 11 after tag)
    b1, b2, b3 = data[off], data[off+1], data[off+2]
    off += 3
    nverts, = struct.unpack_from('<I', data, off); off += 4
    stride, = struct.unpack_from('<I', data, off); off += 4
    if verbose: print(f"  b1={b1} b2={b2} b3={b3} nverts={nverts} stride={stride}")

    vstart = off
    off += nverts * stride
    if verbose: print(f"  vertex blob [{vstart}:{off}] ({nverts*stride} bytes)")

    bbox_min = struct.unpack_from('<3f', data, off); off += 12
    bbox_max = struct.unpack_from('<3f', data, off); off += 12
    if verbose: print(f"  bbox_min={bbox_min} bbox_max={bbox_max}")

    flag_skinned = data[off]; off += 1
    if verbose: print(f"  flag_skinned={flag_skinned} @ {off-1}")
    if flag_skinned:
        must4, = struct.unpack_from('<I', data, off); off += 4
        if verbose: print(f"  must4={must4}")
        for i in range(4):
            off += nverts * 8

    ngroups, = struct.unpack_from('<I', data, off); off += 4
    if verbose: print(f"  ngroups={ngroups} @ {off-4}")

    groups = []
    for g in range(ngroups):
        name1, off = read_lpstr(data, off)
        name2, off = read_lpstr(data, off)
        ambient = struct.unpack_from('<4f', data, off); off += 16
        diffuse = struct.unpack_from('<4f', data, off); off += 16
        f3 = struct.unpack_from('<4f', data, off); off += 16
        f4 = struct.unpack_from('<4f', data, off); off += 16
        shininess, = struct.unpack_from('<f', data, off); off += 4
        blend, off = read_lpstr(data, off)
        mtype, off = read_lpstr(data, off)
        flagA, flagB, flagC = data[off], data[off+1], data[off+2]; off += 3
        valA, = struct.unpack_from('<f', data, off); off += 4
        flagD = data[off]; off += 1
        valB, = struct.unpack_from('<f', data, off); off += 4
        flagE = data[off]; off += 1
        valC, valD = struct.unpack_from('<2f', data, off); off += 8
        flagF = data[off]; off += 1
        valE, valF = struct.unpack_from('<2f', data, off); off += 8
        flagG = data[off]; off += 1
        valG, valH, valI = struct.unpack_from('<3f', data, off); off += 12
        flagH = data[off]; off += 1
        valJ, valK, valL, valM = struct.unpack_from('<4f', data, off); off += 16
        count100, = struct.unpack_from('<I', data, off); off += 4
        extra_strs = []
        if count100 >= 1:
            s, off = read_lpstr(data, off); extra_strs.append(s)
        if count100 >= 2:
            s, off = read_lpstr(data, off); extra_strs.append(s)
        boneWeightCount, = struct.unpack_from('<I', data, off); off += 4
        for _ in range(boneWeightCount):   # std::string array (0x1c = sizeof(std::string)): lpstrs
            _s, off = read_lpstr(data, off)
        groups.append(dict(name1=name1, name2=name2, ambient=ambient, diffuse=diffuse,
                            f3=f3, f4=f4, shininess=shininess, blend=blend, mtype=mtype,
                            flags=(flagA,flagB,flagC,flagD,flagE,flagF,flagG,flagH),
                            vals=(valA,valB,valC,valD,valE,valF,valG,valH,valI,valJ,valK,valL,valM),
                            count100=count100, extra_strs=extra_strs, boneWeightCount=boneWeightCount)
                       )
        if verbose:
            print(f"  group[{g}]: name1={name1!r} name2={name2!r} shininess={shininess}"
                  f" count100={count100} extra_strs={extra_strs} boneWeightCount={boneWeightCount}"
                  f" blend={blend!r} type={mtype!r}")

    texnames = []
    for g in range(ngroups):
        s, off = read_lpstr(data, off)
        texnames.append(s)
    if verbose: print(f"  texnames={texnames}")

    indexcount = struct.unpack_from(f'<{ngroups}I', data, off); off += 4*ngroups
    trianglecount = struct.unpack_from(f'<{ngroups}I', data, off); off += 4*ngroups
    if verbose: print(f"  indexcount={indexcount} trianglecount={trianglecount}")

    all_indices = []
    for g in range(ngroups):
        n = indexcount[g]
        idx = struct.unpack_from(f'<{n}H', data, off); off += 2*n
        all_indices.append(idx)
        if verbose: print(f"  group[{g}] indices={idx}")

    # Optional trailing "attached bone names" block for skinned meshes:
    # u32 unk, u8 unk, then lpstr* read until exactly 3 bytes (the footer) remain.
    bone_names = []
    if len(data) - off > 3:
        off2 = off + 5
        try:
            while len(data) - off2 > 3:
                s, off2 = read_lpstr(data, off2)
                bone_names.append(s)
            if len(data) - off2 == 3:
                off = off2
        except Exception:
            pass

    remaining = len(data) - off
    if verbose:
        print(f"  bone_names={bone_names}")
        print(f"  consumed up to {off}, file size {len(data)}, remaining={remaining} bytes: {data[off:off+16].hex()}")

    return dict(off=off, size=len(data), remaining=remaining, nverts=nverts, stride=stride,
                ngroups=ngroups, indexcount=indexcount, trianglecount=trianglecount,
                all_indices=all_indices, bbox_min=bbox_min, bbox_max=bbox_max)

if __name__ == '__main__':
    verify(sys.argv[1])

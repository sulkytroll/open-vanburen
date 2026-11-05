#!/usr/bin/env python3
import struct, sys

def analyze_g3d(path):
    with open(path, "rb") as f:
        data = f.read()
    
    print(f"File: {path}")
    print(f"Size: {len(data)} bytes")
    
    # Check magic
    magic = data[0:8]
    print(f"Magic: {magic}")
    
    # Find tag start
    if data[8] == 0x03 and data[9] == 0x00:
        tag_start = 37
    elif len(data) > 20:
        tag_start = 39
    else:
        tag_start = 39
    
    print(f"Tag start: offset {tag_start} (0x{tag_start:x})")
    
    # Parse tag loop from tag_start
    p = tag_start
    tag_idx = 0
    while p < len(data) and p < 500:
        tag = data[p]
        if tag < 0x07 or tag > 0x0F:
            print(f"  Tag {tag_idx}: 0x{tag:02x} = {tag} -> not a valid tag, stopping")
            break
        
        tag_idx += 1
        print(f"\n=== Tag #{tag_idx}: 0x{tag:02x} at offset {p} ===")
        
        # Print raw bytes for this tag
        print(f"  Raw bytes: {' '.join(f'{data[p+i]:02x}' for i in range(min(40, len(data)-p)))}")
        
        if tag == 0x07:  # Material
            # s16 id, s16 name_len, name, 18 floats, s16 blend, s16 type, u32, u32
            if p + 2 < len(data):
                id_val = struct.unpack_from("<h", data, p + 1)[0]
                name_len = struct.unpack_from("<H", data, p + 3)[0]
                print(f"  id={id_val}, name_len={name_len}")
                if p + 4 + name_len <= len(data):
                    name = data[p+4:p+4+name_len]
                    print(f"  name: {name}")
                # 18 floats
                mat_end = p + 4 + name_len + 18*4
                if mat_end + 8 <= len(data):
                    blend = struct.unpack_from("<h", data, mat_end)[0]
                    type_val = struct.unpack_from("<h", data, mat_end+2)[0]
                    print(f"  blend={blend}, type={type_val}")
                    p = mat_end + 8
                    continue
                p = mat_end
                continue
            p += 1
            continue
        elif tag == 0x08:  # Texture
            # u8, s16 name_len, name, s16 fname_len, fname, u32 w, u32 h
            if p + 1 < len(data):
                name_len = struct.unpack_from("<H", data, p + 2)[0]
                if p + 3 + name_len < len(data):
                    name = data[p+3:p+3+name_len]
                    fname_len = struct.unpack_from("<H", data, p + 4 + name_len)[0]
                    fname_start = p + 4 + name_len + 2
                    if fname_start + fname_len <= len(data):
                        fname = data[fname_start:fname_start+fname_len]
                        print(f"  name: {name}, filename: {fname}")
                    p = fname_start + fname_len + 8
                    continue
            p += 1
            continue
        elif tag == 0x0A:  # Node
            # u8, s16 name_len, name
            if p + 1 < len(data):
                name_len = struct.unpack_from("<H", data, p + 2)[0]
                if p + 3 + name_len <= len(data):
                    name = data[p+3:p+3+name_len]
                    print(f"  name: {name}")
                    p = p + 3 + name_len
                    continue
            p += 1
            continue
        elif tag == 0x0E:  # Bone
            # s16 name_len, name, u16, u8 [+ optional floats]
            if p + 2 < len(data):
                name_len = struct.unpack_from("<H", data, p + 1)[0]
                if p + 2 + name_len < len(data):
                    name = data[p+2:p+2+name_len]
                    flags_offset = p + 2 + name_len + 2
                    if flags_offset < len(data):
                        flags = data[flags_offset]
                        print(f"  name: {name}, parent=u16, flags={flags}")
                        extra = 0
                        if flags == 3: extra = 16
                        elif flags == 2: extra = 4
                        p = flags_offset + 1 + extra
                        continue
            p += 1
            continue
        elif tag == 0x0F:  # Vertex/Mesh
            # u8, u8, u32 nverts, then verts[], u32 nmat, faces[]
            if p + 1 + 1 + 4 < len(data):
                nverts = struct.unpack_from("<I", data, p + 2)[0]
                print(f"  nverts={nverts}")
                # Try to find vertex stride
                vertex_start = p + 7
                found_stride = False
                for test_stride in [56, 64, 72]:
                    verts_end = vertex_start + nverts * test_stride
                    if verts_end + 4 > len(data):
                        continue
                    nmat = struct.unpack_from("<I", data, verts_end)[0]
                    if nmat == 0 or nmat > 20:
                        continue
                    print(f"  nmat={nmat} with stride={test_stride}")
                    
                    # Verify by checking material group structure
                    vp = verts_end + 4
                    valid = True
                    for mi in range(nmat):
                        if vp + 20 > len(data):
                            valid = False
                            break
                        vp += 4  # unk
                        vp += 2  # name
                        vp += 4  # a
                        vp += 2  # pad
                        vp += 1  # pad2
                        vp += 4  # count
                        vp += 4  # kk
                        kk = struct.unpack_from("<I", data, vp - 4)[0]
                        if kk == 0 or kk % 3 != 0:
                            valid = False
                            break
                        tri_count = kk // 3
                        vp += tri_count  # per-vertex flags
                        vp += tri_count * 6  # triangle indices
                    
                    if valid:
                        print(f"  STRIDE VERIFIED: {test_stride}")
                        # Parse vertices
                        print(f"  Vertices:")
                        for vi in range(min(3, nverts)):
                            v = vertex_start + vi * test_stride
                            pos = struct.unpack_from("<3f", data, v)
                            norm = struct.unpack_from("<3f", data, v + 12)
                            color = struct.unpack_from("<4f", data, v + 36)
                            nUV = struct.unpack_from("<I", data, v + 40)[0]
                            uv_count = 10 + nUV * 2  # offset in floats
                            print(f"    V{vi}: pos={pos[0]:.2f},{pos[1]:.2f},{pos[2]:.2f} nUV={nUV}")
                        
                        # Parse faces
                        vp2 = vertex_start + nverts * test_stride
                        nmat = struct.unpack_from("<I", data, vp2)[0]
                        vp2 += 4
                        for mi in range(nmat):
                            if vp2 + 20 > len(data):
                                break
                            vp2 += 4  # unk
                            vp2 += 2  # name
                            vp2 += 4  # a
                            vp2 += 2  # pad
                            vp2 += 1  # pad2
                            vp2 += 4  # count
                            vp2 += 4  # kk
                            kk = struct.unpack_from("<I", data, vp2 - 4)[0]
                            tri_count = kk // 3
                            vp2 += tri_count  # per-vertex flags
                            print(f"  Material {mi}: kk={kk}, tri_count={tri_count}")
                            # First 3 triangles
                            for ti in range(min(3, tri_count)):
                                a = struct.unpack_from("<H", data, vp2)[0]
                                b = struct.unpack_from("<H", data, vp2 + 2)[0]
                                c = struct.unpack_from("<H", data, vp2 + 4)[0]
                                print(f"    T{ti}: 1-based=({a},{b},{c}) 0-based=({b-1},{a-1},{c-1})")
                                vp2 += 6
                            break  # only first material for brevity
                        
                        p = vertex_start + nverts * test_stride + 4 + nmat * 20  # approximate
                        found_stride = True
                        break
                
                if not found_stride:
                    print(f"  Could not determine valid stride")
                    p += 1
                    continue
                else:
                    print(f"  Vertex data parsed successfully")
                    # Continue from after the mesh data
                    p = vertex_start + nverts * test_stride
                    break  # We found the vertex data
            else:
                p += 1
                continue
        else:
            print(f"  Unknown tag type")
            p += 1
            continue
    
    # Also print file size for context
    print(f"\nTotal file size: {len(data)} bytes")
    if len(data) > tag_start + 50:
        print(f"Remaining after tag loop: {len(data) - tag_start} bytes")

analyze_g3d("assets/Items/GD_Generic_Drop.g3d")

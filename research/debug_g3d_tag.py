#!/usr/bin/env python3
"""Debug G3D parsing for GD_Generic_Drop.g3d."""
import struct, sys

def parse_g3d_debug(path):
    with open(path, "rb") as f:
        data = f.read()
    
    print(f"File: {path}")
    print(f"Size: {len(data)} bytes")
    
    # Find tag start
    tag_start = 37  # For GD_Generic_Drop: [03 00] at offset 8-9
    print(f"Tag loop starts at offset {tag_start} (0x{tag_start:x})")
    
    p = tag_start
    tag_idx = 0
    
    while p < len(data) and p < 800:  # First 800 bytes of tag loop
        tag = data[p]
        if tag < 0x07 or tag > 0x0F:
            print(f"\n[{p}] 0x{tag:02x} ({tag}) — not a valid tag, stopping")
            break
        
        tag_idx += 1
        print(f"\n{'='*60}")
        print(f"Tag #{tag_idx}: 0x{tag:02x} at file offset {p}")
        
        if tag == 0x07:  # Material
            # s16 id, s16 name, 18×f32, s16 blend, s16 type, u32, u32
            id_val = struct.unpack_from("<h", data, p + 1)[0]
            name_val = struct.unpack_from("<h", data, p + 3)[0]
            tag_size = 1 + 2 + 2 + 18*4 + 2 + 2 + 4 + 4  # 89 bytes
            print(f"  id={id_val}, name_val={name_val}")
            
            # Show first few floats
            floats_start = p + 7
            for i in range(min(6, 18)):
                f = struct.unpack_from("<f", data, floats_start + i*4)[0]
                print(f"    float[{i}] = {f:.6f}")
            
            blend = struct.unpack_from("<h", data, floats_start + 18*4)[0]
            type_val = struct.unpack_from("<h", data, floats_start + 18*4 + 2)[0]
            print(f"  blend={blend}, type={type_val}")
            print(f"  Tag size: {tag_size} bytes (ends at {p + tag_size})")
            p += tag_size
            
        elif tag == 0x08:  # Texture
            u8_val = data[p + 1]
            name_len = struct.unpack_from("<H", data, p + 2)[0]
            print(f"  u8={u8_val}, name_len={name_len}")
            if p + 4 + name_len <= len(data):
                name = data[p+4:p+4+name_len]
                print(f"  name: {name}")
                fname_len = struct.unpack_from("<H", data, p + 4 + name_len + 2)[0]
                print(f"  fname_len={fname_len}")
                if p + 6 + name_len + fname_len <= len(data):
                    fname = data[p+6+name_len:p+6+name_len+fname_len]
                    print(f"  filename: {fname}")
            
            tag_size = 1 + 1 + 2 + name_len + 2 + fname_len + 8 if name_len < 100 and fname_len < 100 else 20
            print(f"  Tag size: ~{tag_size} bytes")
            p += tag_size if tag_size < 200 else 20
            
        elif tag == 0x0A:  # Node
            type_val = data[p + 1]
            name_len = struct.unpack_from("<H", data, p + 2)[0]
            if p + 3 + name_len <= len(data):
                name = data[p+3:p+3+name_len]
                print(f"  type={type_val}, name: {name}")
            tag_size = 1 + 1 + 2 + name_len if name_len < 50 else 10
            print(f"  Tag size: ~{tag_size} bytes")
            p += tag_size if tag_size < 60 else 10
            
        elif tag == 0x0E:  # Bone
            name_len = struct.unpack_from("<H", data, p + 1)[0]
            print(f"  name_len={name_len}")
            if p + 2 + name_len <= len(data):
                name = data[p+2:p+2+name_len]
                parent = struct.unpack_from("<H", data, p + 2 + name_len)[0]
                flags = data[p + 4 + name_len]
                print(f"  name: {name}, parent={parent}, flags={flags}")
                extra = 0
                if flags == 3: extra = 16
                elif flags == 2: extra = 4
            tag_size = 1 + 2 + name_len + 2 + 1 + extra if name_len < 20 else 10
            print(f"  Tag size: ~{tag_size} bytes")
            p += tag_size if tag_size < 60 else 10
            
        elif tag == 0x0F:  # Vertex / Mesh node
            print(f"  Found 0x0F tag at offset {p}!")
            # Parse header: u8, u8, u32 nverts
            nverts = struct.unpack_from("<I", data, p + 2)[0]
            print(f"  nverts={nverts}")
            
            # Try strides
            for test_stride in [56, 64, 72]:
                verts_end = p + 7 + nverts * test_stride
                if verts_end + 4 > len(data):
                    continue
                nmat = struct.unpack_from("<I", data, verts_end)[0]
                if nmat == 0 or nmat > 20:
                    continue
                
                print(f"  Potential match: stride={test_stride}, nverts={nverts}, nmat={nmat}")
                
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
                    print(f"  ✓ VERIFIED: stride={test_stride}, nverts={nverts}, nmat={nmat}")
                    
                    # Parse first vertex
                    v_start = p + 7
                    fp = struct.unpack_from("<14f", data, v_start)
                    print(f"  First vertex: pos=({fp[0]:.2f}, {fp[2]:.2f}, {fp[1]:.2f})")
                    print(f"  nUV={int(fp[10])}, nLM={int(fp[10 + int(fp[10]) * 2])}")
                    
                    # Parse materials
                    vp2 = verts_end + 4
                    for mi in range(min(2, nmat)):
                        if vp2 + 20 > len(data):
                            break
                        vp2 += 4; vp2 += 2; a = struct.unpack_from("<I", data, vp2)[0]; vp2 += 4
                        vp2 += 2; vp2 += 1; vp2 += 4; vp2 += 4
                        kk = struct.unpack_from("<I", data, vp2 - 4)[0]
                        tri_count = kk // 3
                        print(f"  Material {mi}: kk={kk}, tri_count={tri_count}")
                        
                        # Show first 3 triangles
                        vp3 = vp2 + tri_count  # skip per-vertex flags
                        for ti in range(min(3, tri_count)):
                            a = struct.unpack_from("<H", data, vp3)[0]
                            b = struct.unpack_from("<H", data, vp3 + 2)[0]
                            c = struct.unpack_from("<H", data, vp3 + 4)[0]
                            print(f"    T{ti}: 1-based=({a},{b},{c}) 0-based=({b-1},{a-1},{c-1})")
                            vp3 += 6
                        break  # just first material
                    
                    p = verts_end
                    break  # stop trying strides
                else:
                    print(f"  ✗ Invalid stride {test_stride} (material check failed)")
            
            if not any(data[p + 7 + nverts * s] == data[p + 7 + nverts * s] for s in [56, 64, 72]):
                print(f"  ✗ Could not determine valid stride")
            break  # done with 0x0F
        
        else:
            print(f"  Unknown tag type 0x{tag:02x}")
            p += 1
    
    print(f"\n{'='*60}")
    print(f"Total bytes processed: {p - tag_start} (file size: {len(data)}, remaining: {len(data) - p})")
    
    # Also dump the first 100 bytes of the file for reference
    print(f"\nFirst 80 bytes of file:")
    for i in range(0, min(80, len(data)), 16):
        hex_part = ' '.join(f'{data[j]:02x}' for j in range(i, min(i+16, len(data))))
        ascii_part = ''.join(chr(data[j]) if 32 <= data[j] < 127 else '.' for j in range(i, min(i+16, len(data))))
        print(f"  {i:04x}: {hex_part:<48} {ascii_part}")

parse_g3d_debug("assets/Items/GD_Generic_Drop.g3d")

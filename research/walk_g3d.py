#!/usr/bin/env python3
"""Full tag walker for GD_Generic_Drop.g3d with CORRECTED material layout
(length-prefixed strings). Goal: walk every tag, reach 0x0F, parse vertices
with known-good 56-byte stride, and confirm we land exactly at EOF."""
import struct, sys

def rd_u16(d, o): return struct.unpack_from("<H", d, o)[0]
def rd_i16(d, o): return struct.unpack_from("<h", d, o)[0]
def rd_u32(d, o): return struct.unpack_from("<I", d, o)[0]
def rd_f32(d, o): return struct.unpack_from("<f", d, o)[0]
def rd_str(d, o):
    ln = rd_u16(d, o)
    return d[o+2:o+2+ln].decode('latin1'), o+2+ln

def walk(path):
    d = open(path, "rb").read()
    print(f"\n===== {path}  (size {len(d)}) =====")
    # find tag start (use README heuristic; GD_* -> 0x25)
    if d[8] == 0x03 and d[9] == 0x00:
        ts = 0x25
    elif d[8] == 0x6c and d[9] == 0x65:
        ts = 22
    elif d[8] == 0x35 and d[9] == 0x01:
        ts = 21
    else:
        ts = 0x27
    print(f"tag start = 0x{ts:x} ({ts}), byte=0x{d[ts]:02x}")
    p = ts
    while p < len(d):
        tag = d[p]
        if tag < 0x07 or tag > 0x0F:
            print(f"  @0x{p:04x}: 0x{tag:02x} NOT a tag (loop end). remaining={len(d)-p}")
            break
        print(f"  @0x{p:04x}: TAG 0x{tag:02x}")
        if tag == 0x07:
            q = p+1
            n1, q = rd_str(d, q)
            n2, q = rd_str(d, q)
            # 18 floats
            floats = [rd_f32(d, q+i*4) for i in range(18)]
            q += 18*4
            b, q = rd_str(d, q)
            t, q = rd_str(d, q)
            u1, u2 = rd_u32(d, q), rd_u32(d, q)
            q += 8
            print(f"     name1='{n1}' name2='{n2}' blend='{b}' type='{t}' u=({u1},{u2})")
            print(f"     amb={floats[0:4]} diff={floats[4:8]} emis={floats[8:12]} spec={floats[12:16]} shin={floats[16]} alpha={floats[17]}")
            p = q
        elif tag == 0x08:
            q = p+1
            # try: u8, then strings? or u16 lens? Print bytes to decide
            print("     bytes:", ' '.join(f'{d[j]:02x}' for j in range(p, min(p+40, len(d)))))
            p += 1
        else:
            print(f"     (tag 0x{tag:02x} unhandled) bytes:", ' '.join(f'{d[j]:02x}' for j in range(p, min(p+32, len(d)))))
            p += 1
        if p - ts > 5000: 
            print("     (stopping, too far)")
            break
    print(f"  FINAL p=0x{p:04x} ({p}) of {len(d)}; consumed={p-ts}")

if __name__ == "__main__":
    walk("assets/Items/GD_Generic_Drop.g3d")

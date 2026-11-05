#!/usr/bin/env python3
"""Locate 0x0F mesh tag and dump the 0x08 texture tag region precisely."""
import struct

d = open("assets/Items/GD_Generic_Drop.g3d", "rb").read()
N = len(d)
print(f"size {N}")

# find 0x0F candidates in first 300 bytes
print("\n0x0F candidates in first 300:")
for i in range(0x25, 300):
    if d[i] == 0x0F:
        ctx = ' '.join(f'{d[j]:02x}' for j in range(i, min(i+12, N)))
        # test nverts at i+3 (u8,u8,u32)
        nv = struct.unpack_from("<I", d, i+3)[0]
        print(f"  0x{i:04x} ({i}): nverts@+3={nv}  ctx: {ctx}")
# also i+1 variant
print("\n0x0F with nverts@+2 (u8,u32):")
for i in range(0x25, 300):
    if d[i] == 0x0F:
        nv = struct.unpack_from("<I", d, i+2)[0]
        if 1 < nv < 5000:
            print(f"  0x{i:04x} ({i}): nverts={nv}")

# texture tag: dump 0x103..0x130
print("\n--- texture tag region 0x103..0x134 ---")
for i in range(0x103, 0x134, 16):
    hexp = ' '.join(f'{d[j]:02x}' for j in range(i, min(i+16, N)))
    asc = ''.join(chr(d[j]) if 32 <= d[j] < 127 else '.' for j in range(i, min(i+16, N)))
    print(f"  {i:04x}: {hexp:<48}  {asc}")

# Interpretation A: 08 u8 u16len name u16len fname u32 u32
p = 0x103
u8 = d[p+1]; p += 2
ln = struct.unpack_from("<H", d, p)[0]; name = d[p+2:p+2+ln]; p += 2 + ln
ln2 = struct.unpack_from("<H", d, p)[0]; fname = d[p+2:p+2+ln2]; p += 2 + ln2
print(f"\nInterp A: u8={u8} name='{name.decode()}' fname='{fname.decode()}' next@0x{p:x}")
print("  next bytes:", ' '.join(f'{d[j]:02x}' for j in range(p, p+16)))
print(f"  u32@0x{p:x}={struct.unpack_from('<I',d,p)[0]}  u32@0x{p+4:x}={struct.unpack_from('<I',d,p+4)[0]}")
p2 = p + 8
print(f"  after 2 u32: @0x{p2:x} byte=0x{d[p2]:02x}")

# Interpretation B: ...fname u8 u32 u32
print("\nInterp B (u8 u32 u32 after fname):")
p = 0x103
u8 = d[p+1]; p += 2
ln = struct.unpack_from("<H", d, p)[0]; p += 2 + ln
ln2 = struct.unpack_from("<H", d, p)[0]; p += 2 + ln2
u8b = d[p]; p += 1
print(f"  u8b={u8b} u32@0x{p:x}={struct.unpack_from('<I',d,p)[0]} u32@0x{p+4:x}={struct.unpack_from('<I',d,p+4)[0]}")
p2 = p + 8
print(f"  end@0x{p2:x} byte=0x{d[p2]:02x} ({chr(d[p2]) if 32<=d[p2]<127 else '-'})")

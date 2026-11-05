#!/usr/bin/env python3
"""Probe G3D header/tag region bytes for GD_Generic_Drop.g3d."""
import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else "assets/Items/GD_Generic_Drop.g3d"
data = open(path, "rb").read()
print(f"File: {path}")
print(f"Size: {len(data)}")

def hexdump(data, off, n):
    for i in range(off, min(off + n, len(data)), 16):
        hexpart = ' '.join(f'{data[j]:02x}' for j in range(i, min(i + 16, len(data))))
        asc = ''.join(chr(data[j]) if 32 <= data[j] < 127 else '.' for j in range(i, min(i + 16, len(data))))
        print(f"  {i:04x}: {hexpart:<48}  {asc}")

print("\n=== First 128 bytes ===")
hexdump(data, 0, 128)

print("\n=== Region around tag start (37) ===")
hexdump(data, 37, 160)

# Try: is there a u16/u32 length somewhere before tag 0x07 at 37?
print("\n=== Float interpretations at offset 37+7 (18 float theory) ===")
for i in range(18):
    f = struct.unpack_from("<f", data, 44 + i * 4)[0]
    print(f"  float[{i}] = {f!r}")

print("\n=== Bytes 44..116 as 18 floats (hex+val) ===")
for i in range(18):
    b = data[44 + i*4:48 + i*4]
    f = struct.unpack_from("<f", data, 44 + i * 4)[0]
    print(f"  [{i:2d}] {b.hex()}  {f:.8g}")

print("\n=== What's at offsets 116..140? (end of 18-float block + blend/type/u32/u32) ===")
hexdump(data, 116, 40)
print(f"  byte at 116: 0x{data[116]:02x}")
print(f"  i16 at 116: {struct.unpack_from('<h', data, 116)[0]}")
print(f"  i16 at 118: {struct.unpack_from('<h', data, 118)[0]}")
print(f"  u32 at 120: {struct.unpack_from('<I', data, 120)[0]}")
print(f"  u32 at 124: {struct.unpack_from('<I', data, 124)[0]}")
print(f"  byte at 126: 0x{data[126]:02x}  ({chr(data[126]) if 32<=data[126]<127 else '-'})")

# Alternative: maybe material is 1+2+2+ N floats + ... and next tag is 0x08 at some offset
print("\n=== Scan for 0x08 / 0x0F bytes in 37..256 ===")
for i in range(37, 256):
    if data[i] in (0x08, 0x0F, 0x0A, 0x0E):
        # context
        ctx = ' '.join(f'{data[j]:02x}' for j in range(max(0,i-4), i+8))
        print(f"  0x{data[i]:02x} at {i:4d} (0x{i:x})   ctx: {ctx}")

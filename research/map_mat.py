#!/usr/bin/env python3
"""Map the exact byte layout of the material (0x07) tag in GD_Generic_Drop.g3d.
   First material tag spans 0x25 .. 0x8d (next tag 0x07 at 0x8e).
   Walk it byte-by-byte and try to decompose the numeric region into floats."""
import struct

data = open("assets/Items/GD_Generic_Drop.g3d", "rb").read()

def hexdump(off, n, label=""):
    if label: print(f"\n--- {label} ---")
    for i in range(off, off + n, 16):
        hexp = ' '.join(f'{data[j]:02x}' for j in range(i, min(i+16, off+n)))
        asc = ''.join(chr(data[j]) if 32 <= data[j] < 127 else '.' for j in range(i, min(i+16, off+n)))
        print(f"  {i:04x}: {hexp:<48}  {asc}")

start = 0x25
print(f"Material tag starts at 0x{start:x} ({start}), byte=0x{data[start]:02x}")

# Header: tag, u16 len, name, u16 len, str2
p = start
tag = data[p]; p += 1
ln1 = struct.unpack_from("<H", data, p)[0]; p += 2
name1 = data[p:p+ln1].decode('latin1'); p += ln1
ln2 = struct.unpack_from("<H", data, p)[0]; p += 2
name2 = data[p:p+ln2].decode('latin1'); p += ln2
print(f"  name1='{name1}' (len {ln1}), name2='{name2}' (len {ln2})")
print(f"  header ends at 0x{p:x} ({p})")

# Numeric region: from p until we hit the 'OPAQUE' string.
# Find OPAQUE
op = data.find(b"OPAQUE")
sl = data.find(b"SILENT")
print(f"  OPAQUE at 0x{op:x}, SILENT at 0x{sl:x}")
# OPAQUE is length-prefixed: u16 len at op-2
oplen = struct.unpack_from("<H", data, op-2)[0]
print(f"  len before OPAQUE = {oplen}")
# SILENT length prefix at sl-2
sllen = struct.unpack_from("<H", data, sl-2)[0]
print(f"  len before SILENT = {sllen}")
after_sl = sl + 6  # end of SILENT
print(f"  SILENT ends at 0x{after_sl:x}")
u32a = struct.unpack_from("<I", data, after_sl)[0]
u32b = struct.unpack_from("<I", data, after_sl+4)[0]
print(f"  u32 after SILENT = {u32a} (0x{u32a:08x}), next u32 = {u32b} (0x{u32b:08x})")
tag_end = after_sl + 8
print(f"  tag would end at 0x{tag_end:x} ({tag_end}); byte there = 0x{data[tag_end]:02x} (next tag)")

# So numeric region = [p, op-2). How many floats?
num_start = p
num_end = op - 2  # up to the length prefix of OPAQUE
span = num_end - num_start
print(f"\n  NUMERIC REGION: 0x{num_start:x} .. 0x{num_end:x} = {span} bytes")
print(f"  {span} bytes = {span/4} floats")

# Dump numeric region as floats (aligned at num_start)
print(f"\n  Floats (aligned at 0x{num_start:x}):")
for i in range(span // 4):
    f = struct.unpack_from("<f", data, num_start + i*4)[0]
    b = data[num_start+i*4: num_start+i*4+4]
    print(f"    [{i:2d}] {b.hex():<10} = {f!r}")

print("\n\n=== Full tag dump 0x25..0x90 ===")
hexdump(0x25, 0x90-0x25, "first material tag")

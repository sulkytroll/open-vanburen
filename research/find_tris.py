#!/usr/bin/env python3
"""Locate the triangle-index region by scanning for runs of u16s in [1,48],
then reverse-engineer the group header from the gaps."""
import struct

d = open("assets/Items/GD_Generic_Drop.g3d","rb").read()
N = len(d)
nv = 48
ve = 0xbd2  # verts_end (stride 56)
print(f"file size {N}, verts_end 0x{ve:x}, nmat=2")

# Scan for aligned 6-byte triplets with all u16 in [1,48]
hits = []
for o in range(ve+8, N-6, 2):
    a,b,c = struct.unpack_from("<3H", d, o)
    if 1 <= a <= nv and 1 <= b <= nv and 1 <= c <= nv:
        hits.append(o)
print(f"candidate index offsets (aligned, all 3 u16 in [1,{nv}]): {len(hits)}")
# Find the longest consecutive run (step 6)
runs = []
cur = [hits[0]] if hits else []
for i in range(1, len(hits)):
    if hits[i] == hits[i-1] + 6:
        cur.append(hits[i])
    else:
        if cur: runs.append(cur)
        cur = [hits[i]]
if cur: runs.append(cur)
runs.sort(key=len, reverse=True)
print(f"longest runs (start, ntri): " + ", ".join(f"(0x{r[0]:x},{len(r)})" for r in runs[:8]))

# Show context around the first long run
if runs:
    r = runs[0]
    start = r[0]
    print(f"\nbest index run: 0x{start:x} .. 0x{start+6*len(r)-1:x} ({len(r)} tris)")
    print("bytes before run (64 bytes):")
    for i in range(max(ve, start-64), start, 16):
        hexp=' '.join(f'{d[j]:02x}' for j in range(i, min(i+16,start)))
        asc=''.join(chr(d[j]) if 32<=d[j]<127 else '.' for j in range(i, min(i+16,start)))
        print(f"  {i:04x}: {hexp:<48}  {asc}")
    print("first 3 tris:", [struct.unpack_from("<3H", d, start+6*i) for i in range(3)])
    # after the run
    end = start + 6*len(r)
    print(f"after run @0x{end:x}: {' '.join(f'{d[j]:02x}' for j in range(end, end+32))}")
    # Now what's between ve+4 (0xbd6) and start?
    gap = start - (ve+4)
    print(f"\ngap between group-data start (0x{ve+4:x}) and indices (0x{start:x}) = {gap} bytes")

#!/usr/bin/env python3
"""Dump the whole region 0xbd0..EOF and scan the ENTIRE file for index runs."""
import struct
d = open("assets/Items/GD_Generic_Drop.g3d","rb").read()
N = len(d)
nv = 48
print(f"size {N} (0x{N:x})")
print("\n--- 0xbd0 .. EOF ---")
for i in range(0xbd0, N, 16):
    hexp=' '.join(f'{d[j]:02x}' for j in range(i, min(i+16,N)))
    asc=''.join(chr(d[j]) if 32<=d[j]<127 else '.' for j in range(i, min(i+16,N)))
    print(f"  {i:04x}: {hexp:<48}  {asc}")

# whole-file scan for index runs (both alignments)
for align in (0,1):
    hits=[]
    o = 0xbd0 + align
    while o + 6 <= N:
        a,b,c = struct.unpack_from("<3H", d, o)
        if 1 <= a <= nv and 1 <= b <= nv and 1 <= c <= nv:
            hits.append(o)
        o += 2
    runs=[]; cur=[]
    for i in range(1,len(hits)):
        if hits[i]==hits[i-1]+6: cur.append(hits[i])
        else:
            if cur: runs.append(cur)
            cur=[hits[i]]
    if cur: runs.append(cur)
    runs.sort(key=len, reverse=True)
    print(f"\nalign {align}: {len(hits)} candidates; top runs: " +
          ", ".join(f"(0x{r[0]:x}, n={len(r)})" for r in runs[:6]))

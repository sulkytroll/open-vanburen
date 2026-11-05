#!/usr/bin/env python3
"""Probe the failing categories (Critters, Tiles, Effects, some Props/Items)
to understand WHY they fail: byte-8 pattern, candidate tag starts, and what
tag bytes actually appear near the expected tag-loop start."""
import struct, os, collections

ASSETS = "assets"
VALID = {0x07,0x08,0x0A,0x0E,0x0F}

def probe(path):
    d = open(path,"rb").read()
    N = len(d)
    b8, b9 = d[8], d[9]
    # current find_tag_start logic
    if N>20 and d[8]==0x6c and d[9]==0x65 and d[10]==0x20: ts=22
    elif N>20 and d[8]==0x03 and d[9]==0x00: ts=0x25
    else: ts=0x27
    byte_at_ts = d[ts] if ts<N else None
    # scan a window [8, 60) for the first byte that's a valid tag AND is
    # followed by a plausible record; report all valid-tag bytes in window
    valid_positions = [i for i in range(8, min(60,N)) if d[i] in VALID]
    return dict(path=path, N=N, b8=b8, b9=b9, ts=ts, byte_at_ts=byte_at_ts,
                valid_positions=valid_positions)

# Sample failing files from each category
samples = {
 "Critters": [f"assets/Critters/CR_Ant.g3d",
              f"assets/Critters/CR_Badger.g3d"],
 "Tiles":    [f"assets/Tiles/Ext5x5_City1_build01{{32}}onestorywall (0).g3d"],
 "Effects":  [f"assets/Effects/Cone_Blood.g3d"],
}

for cat, files in samples.items():
    print(f"\n===== {cat} =====")
    for f in files:
        if not os.path.exists(f):
            # glob fallback
            import glob
            cand = glob.glob(os.path.join(ASSETS, cat, "*.g3d"))[:1]
            if not cand:
                print(f"  (no files) {f}"); continue
            f = cand[0]
        p = probe(f)
        print(f"  {os.path.basename(f)}")
        print(f"    size={p['N']} byte8=0x{p['b8']:02x} byte9=0x{p['b9']:02x} tag_start={p['ts']} byte@ts=0x{p['byte_at_ts']:02x} ({'VALID' if p['byte_at_ts'] in VALID else 'INVALID'})")
        print(f"    valid-tag bytes in [8,60): {[f'{i}(0x{b:02x})' for i,b in [(i, open(f,'rb').read()[i]) for i in p['valid_positions'][:12]]]}")
        # hexdump first 64 bytes
        d = open(f,"rb").read()
        for i in range(0, min(64,len(d)),16):
            hexp=' '.join(f'{d[j]:02x}' for j in range(i,min(i+16,len(d))))
            asc=''.join(chr(d[j]) if 32<=d[j]<127 else '.' for j in range(i,min(i+16,len(d))))
            print(f"      {i:04x}: {hexp:<48}  {asc}")

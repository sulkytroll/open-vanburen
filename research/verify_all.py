#!/usr/bin/env python3
"""Strict byte-exact verification of every .g3d in the corpus. A file passes only if parsing
consumes every byte up to the fixed 3-byte footer 0b 0b 01.
 - converted path (mesh-tag flag byte==1): Format B  (verify_readb3d.verify)
 - raw path (flag==0): variable-length vertices + Format-A-style groups, header has 3 or 2 pre-count bytes
Usage: python3 verify_all.py [-v]"""
import sys, glob, struct, collections
sys.path.insert(0, '.')
import verify_readb3d as v

def raw(d, pre):
    off = v.parse_outer_tags(d, v.find_tag_start(d)) + pre
    n, = struct.unpack_from('<I', d, off); off += 4
    if n == 0 or n > 200000: raise ValueError('nverts')
    for _ in range(n):
        off += 40
        nuv, = struct.unpack_from('<I', d, off); off += 4 + nuv * 8
        nw, = struct.unpack_from('<I', d, off); off += 4 + nw * 8   # (u32 bone, f32 weight)
    nm, = struct.unpack_from('<I', d, off); off += 4
    for _ in range(nm):
        off += 4
        _, off = v.read_lpstr(d, off)
        nt, = struct.unpack_from('<I', d, off); off += 4
        if nt > 8: raise ValueError('ntex')
        for _ in range(nt): _, off = v.read_lpstr(d, off)
        off += 4
        if d[off] != 0xc9: raise ValueError('marker')
        off += 1
        kk, = struct.unpack_from('<I', d, off); off += 4 + 5 * kk
        if off > len(d): raise ValueError('kk')
    return len(d) - off

def classify(path):
    d = open(path, 'rb').read()
    try:
        if v.verify(path, verbose=False)['remaining'] == 3: return 'B'
    except Exception: pass
    for pre in (3, 2):
        try:
            if raw(d, pre) == 3: return f'raw{pre}'
        except Exception: pass
    return None

if __name__ == '__main__':
    cat = collections.defaultdict(collections.Counter); fails = []
    for f in sorted(glob.glob('assets/**/*.g3d', recursive=True)):
        c = f.split('/assets/')[1].split('/')[0]; k = classify(f)
        cat[c][k or 'FAIL'] += 1
        if not k: fails.append(f)
    tot = ok = 0
    for c, ct in cat.items():
        n = sum(ct.values()); a = n - ct['FAIL']; tot += n; ok += a
        print(f'{c:9s} {a}/{n}  {dict(ct)}')
    print(f'TOTAL {ok}/{tot}')
    for f in fails: print('FAIL', f.split('/assets/')[1])

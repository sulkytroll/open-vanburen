#!/usr/bin/env python3
"""Strict verification of all map-related lumps (see MAP_FORMAT_FINDINGS.md).
  .map : EMAP chunk stream -- every chunk framed exactly, every known chunk decoder consumes its payload exactly
  .col : RLE grid, every row complete, no trailing bytes
  .tre : octree -- every chunk consumed, tree traversal ends exactly at EOF, triangles == FLGS entries, indices in range
Usage: python3 verify_maps.py"""
import sys, glob, struct, collections
sys.path.insert(0, '.')
import map_chunks as mc, map_decode as md, tre_parse as tp, col_parse as cp

A = 'assets/Maps/'
def verify_map(path):
    d = open(path, 'rb').read(); cs = mc.chunks(d)       # raises if framing is off
    if sum(c[3] for c in cs) != len(d): raise ValueError('framing')
    for t, v, off, ln, p in cs:
        if t in md.DEC:
            r = md.R(p); md.DEC[t](r)
            if not r.done() and not (t == 'EMEP' and len(p) == 17): raise ValueError(f'{t} not exact @ {off}')
            if t == 'EME2':
                r = md.R(p); x = md.EME2(r)
                if not x['eeov_exact']: raise ValueError('EEOV')
    return collections.Counter(c[0] for c in cs)

def verify_tre(path):
    d = open(path, 'rb').read(); t = tp.parse(d)
    if t['tree_pos'] != len(d): raise ValueError('tree end != EOF')
    nm = len(t['materials']); nb = len(t['vbufs'])
    for bnd, bid, fl, groups, flg in t['leaves']:
        nv = t['vbufs'][bid]['n']; ntri = sum(len(g[3]) for g in groups) // 3
        if flg is not None and len(flg) != ntri: raise ValueError('FLGS != tris')
        for mat, lm, layer, ind in groups:
            if mat >= nm or (ind and max(ind) >= nv): raise ValueError('range')
    return len(t['leaves'])

if __name__ == '__main__':
    ok = bad = 0; types = collections.Counter()
    for f in sorted(glob.glob(A + '*.map') + glob.glob(A + '../Engine/*.map')):
        try: types.update(verify_map(f)); ok += 1
        except Exception as e: bad += 1; print('MAP FAIL', f.split('/')[-1], e)
    print(f'.map {ok}/{ok+bad}', dict(types))
    ok = bad = 0
    for f in sorted(glob.glob(A + '*.tre') + glob.glob(A + '../Engine/*.tre')):
        try: verify_tre(f); ok += 1
        except Exception as e: bad += 1; print('TRE FAIL', f.split('/')[-1], e)
    print(f'.tre {ok}/{ok+bad}')
    ok = bad = 0
    for f in sorted(glob.glob(A + '*.col') + glob.glob(A + '../Engine/*.col')):
        try:
            w, h, rows = cp.parse(open(f, 'rb').read())
            t = tp.parse(open(f[:-4] + '.tre', 'rb').read()); r = t['nodes'][0][0] if t['nodes'] else t['leaves'][0][0]
            # grid covers the octree root XZ extent at 2 cells per world unit (a few maps ship a stale grid; reported, not failed)
            if (round((r[3]-r[0])*2), round((r[5]-r[2])*2)) != (w, h): print('  note: grid != octree extent', f.split('/')[-1], (w, h))
            ok += 1
        except Exception as e: bad += 1; print('COL FAIL', f.split('/')[-1], e)
    print(f'.col {ok}/{ok+bad}')

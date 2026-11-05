#!/usr/bin/env python3
"""Decode + strictly verify octree static lights (LGTD) and the light-visibility grid (LVLD) in .tre files.

LGTR (64 B): u32 type (0 point, 1 spot), u32 aux (engine adds 1 on load), f32 rgb[3], f32 pos[3], f32 dir[3] (NaN = unused),
             f32 intensity, f32 range, f32 falloff, f32 theta, f32 phi (the last 3 are 1,0,0 for points; spot cone angles in radians).
LVLD: INFO (u32 w, u32 h, f32 sx, f32 sy), LVL0 = ceil(w*h/32) pairs (u32 offset, u32 mask) then a u16 stream of light-id lists.
  Cell(x,z): col = int((x-minX)/sx + .5), row = int((maxZ-z)/sy + .5), cell = row*w + col  (engine 0x5afce0).
  pair = cell>>5; bit (31-(cell&31)) of mask set -> cell has a list; lists of a block are stored in cell order starting at
  byte offset `offset` (from the start of LVL0); a list is a run of u16 with equal bit15; consecutive lists flip bit15; id = v & 0x7fff.
"""
import struct, sys, glob, math, collections

def chunks(d, p, end):
    while p < end:
        tag = d[p:p+4]; ln, = struct.unpack_from('<I', d, p+4); yield tag, p+8, ln; p += 8 + ln

def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'8TRE'
    head = struct.unpack_from('<4I', d, 16)
    out = dict(head=head, lights=[], grid=None)
    p = 8 + 8 + 16
    tag = d[p:p+4]
    if tag == b'LGTD':
        ln, = struct.unpack_from('<I', d, p+4); q = p + 8; end = q + ln
        assert d[q:q+4] == b'INFO'; n, = struct.unpack_from('<I', d, q+8); q += 8 + 4
        for _ in range(n):
            assert d[q:q+4] == b'LGTR' and struct.unpack_from('<I', d, q+4)[0] == 64
            out['lights'].append(struct.unpack_from('<2I13f', d, q+8)); q += 72
        assert q == end
        p = end; tag = d[p:p+4]
    if tag == b'LVLD':
        ln, = struct.unpack_from('<I', d, p+4); q = p + 8; end = q + ln
        assert d[q:q+4] == b'INFO' and struct.unpack_from('<I', d, q+4)[0] == 16
        w, h, sx, sy = struct.unpack_from('<IIff', d, q+8); q += 24
        assert d[q:q+4] == b'LVL0'; l0, = struct.unpack_from('<I', d, q+4); assert q + 8 + l0 == end
        out['grid'] = dict(w=w, h=h, sx=sx, sy=sy, data=d[q+8:end])
    # root node bounds
    i = d.find(b'TREE'); 
    assert d[i+8:i+12] == b'NODE' or d[i+8:i+12] == b'LEAF'
    out['bounds'] = struct.unpack_from('<6f', d, i + 8 + 8 + 8)
    return out

def decode_grid(g):
    """-> dict cell -> tuple(light ids); raises on any structural inconsistency."""
    data = g['data']; ncell = g['w'] * g['h']; npair = (ncell + 31) // 32
    assert len(data) >= npair * 8
    pairs = struct.unpack_from('<%dI' % (npair * 2), data, 0)
    stream_end = len(data) // 2
    cells = {}
    expect = npair * 4          # first list starts right after the pair table (in u16 units)
    for k in range(npair):
        off, mask = pairs[2*k], pairs[2*k+1]
        if mask == 0: continue
        assert off % 2 == 0
        pos = off // 2
        assert pos == expect, (k, pos, expect)
        par = None
        for b in range(32):
            if not mask & (0x80000000 >> b): continue
            cell = k * 32 + b
            assert cell < ncell
            ids = []
            v = struct.unpack_from('<H', data, pos*2)[0]; cur = v & 0x8000
            if par is not None: assert cur != par, 'parity did not flip'
            par = cur
            while pos < stream_end:
                v = struct.unpack_from('<H', data, pos*2)[0]
                if (v & 0x8000) != cur: break
                ids.append(v & 0x7fff); pos += 1
            cells[cell] = tuple(ids)
        expect = pos
    assert expect == stream_end, ('stream not fully consumed', expect, stream_end)
    return cells

if __name__ == '__main__':
    files = sorted(glob.glob('../assets/**/*.tre', recursive=True)) if len(sys.argv) < 2 else sys.argv[1:]
    ok = bad = 0
    for f in files:
        t = load(f); g = t['grid']; name = f.split('/')[-1]
        if g is None:
            print('%-45s head=%s lights=%d no grid' % (name, t['head'][1], len(t['lights']))); continue
        try:
            cells = decode_grid(g)
            nl = len(t['lights'])
            mx = max((max(v) for v in cells.values() if v), default=-1)
            assert mx < max(nl, 1) or nl == 0 and mx < 0, ('light id out of range', mx, nl)
            ok += 1
            print('%-45s flags=%d lights=%d grid=%dx%d s=(%g,%g) cells_with_lists=%d maxid=%d' % (name, t['head'][1], nl, g['w'], g['h'], g['sx'], g['sy'], len(cells), mx))
        except AssertionError as e:
            bad += 1; print('%-45s FAIL %s' % (name, e))
    print('ok', ok, 'bad', bad)

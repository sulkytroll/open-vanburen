#!/usr/bin/env python3
"""Parser/verifier for Van Buren octree files (".tre", lump type 400, magic "8TRE").

File = '8TRE' u32 size, then chunks (tag(4) u32 len, payload; len excludes the 8-byte header):
  HEAD  16B: u32 version(2), u32 flags (4=static lights, 8=static lighting grid), u32 depth, u32 ?
  [LGTD { INFO u32 n; n * LGTR(64B) }]            only if flags&4
  [LVLD { INFO 16B (u32 w, u32 h, f32 sx, f32 sy); LVL0 (lighting grid, (u32,u32) pairs) }]   if flags&8
  MATD  { INFO u32 n; n * MATR(196B) }
  TXTD  { u32 first_lightmap_index, u32 total; (total-1) * TXTR(zstring) }
  VTXD  { u32 nbuf; nbuf * VTXB { u32 nverts; XYZ f32x3, NRML f32x3, DIFU u32, TXUV f32x2, LMUV f32x2 } }
  TREE  { preorder NODE/LEAF chunks }
     NODE { INFO 64B: f32 min[3], f32 max[3], u32 buffer_id, u32 flags, u32 child_present[8] }  (children follow as siblings, slot order)
     LEAF { INFO 32B: f32 min[3], f32 max[3], u32 buffer_id, u32 flags;
            IDXD { IDXS u16[] ; FLGS u8[ntri] } }
  IDXS = u16 stream:  [0x8000|material, c1, { [0x4000|lightmap, c2, { [layer, n, n*index, pad if n odd] }* ] }* ]
  every LEAF is followed by 4 padding bytes (copies of the last FLGS byte); the TREE chunk length excludes these pads, and the file ends right after the last one.
"""
import struct, sys, glob, collections

def chunk_at(d, p):
    tag = d[p:p+4]; ln, = struct.unpack_from('<I', d, p+4)
    return tag, ln, p+8

def parse_idxs(idx):
    """-> list of (material, lightmap, layer, [indices])"""
    n = len(idx); p = 0; out = []
    while p < n:
        m, c1 = idx[p], idx[p+1]
        if not m & 0x8000: raise ValueError('lvl1 marker')
        q = p + 2; e = q + c1
        while q < e:
            m2, c2 = idx[q], idx[q+1]
            if not m2 & 0x4000: raise ValueError('lvl2 marker')
            r = q + 2; e2 = r + c2
            while r < e2:
                x, c3 = idx[r], idx[r+1]
                out.append((m & 0x7fff, m2 & 0x3fff, x, list(idx[r+2:r+2+c3])))
                r += 2 + c3 + (c3 & 1)
            if r != e2: raise ValueError('lvl3 size')
            q = e2
        if q != e: raise ValueError('lvl2 size')
        p = e
    return out

def parse(d):
    if d[:4] != b'8TRE': raise ValueError('magic')
    size, = struct.unpack_from('<I', d, 4)
    t = {}
    p = 8
    tag, ln, q = chunk_at(d, p); assert tag == b'HEAD' and ln == 16
    t['head'] = struct.unpack_from('<4I', d, q); p = q + ln
    flags = t['head'][1]
    tag, ln, q = chunk_at(d, p)
    if tag == b'LGTD':
        t['lgtd_len'] = ln; p = q + ln; tag, ln, q = chunk_at(d, p)
    if tag == b'LVLD':
        t['lvld'] = d[q:q+ln]; p = q + ln; tag, ln, q = chunk_at(d, p)
    assert tag == b'MATD', tag
    mq = q; mend = q + ln
    t_tag, t_ln, qq = chunk_at(d, mq); assert t_tag == b'INFO'
    nmat, = struct.unpack_from('<I', d, qq); mq = qq + t_ln
    mats = []
    for _ in range(nmat):
        tg, l, qq = chunk_at(d, mq); assert tg == b'MATR' and l == 196
        mats.append(d[qq:qq+l]); mq = qq + l
    assert mq == mend, 'MATD size'
    t['materials'] = mats; p = mend
    tag, ln, q = chunk_at(d, p); assert tag == b'TXTD'
    first_lm, total = struct.unpack_from('<II', d, q); tq = q + 8; tend = q + ln
    texs = []
    for _ in range(total - 1):
        tg, l, qq = chunk_at(d, tq); assert tg == b'TXTR'
        texs.append(d[qq:qq+l].split(b'\0')[0].decode('latin1')); tq = qq + l
    assert tq == tend, 'TXTD size'
    t['first_lightmap'] = first_lm; t['textures'] = texs; p = tend
    tag, ln, q = chunk_at(d, p); assert tag == b'VTXD'
    nbuf, = struct.unpack_from('<I', d, q); vq = q + 4; vend = q + ln
    bufs = []
    for _ in range(nbuf):
        tg, l, qq = chunk_at(d, vq); assert tg == b'VTXB'
        nv, = struct.unpack_from('<I', d, qq); sq = qq + 4; send = qq + l
        b = {'n': nv}
        while sq < send:
            tg2, l2, q2 = chunk_at(d, sq)
            b[tg2.decode('latin1').strip()] = d[q2:q2+l2]; sq = q2 + l2
        assert sq == send, 'VTXB size'
        bufs.append(b); vq = qq + l
    assert vq == vend, 'VTXD size'
    t['vbufs'] = bufs; p = vend
    tag, ln, q = chunk_at(d, p); assert tag == b'TREE'
    tend = q + ln; p = q
    nodes = []; leaves = []
    def read_node():
        nonlocal p
        tag, ln, q = chunk_at(d, p)
        if tag == b'NODE':
            it, il, iq = chunk_at(d, q); assert it == b'INFO' and il == 64
            f = struct.unpack_from('<6f2I8I', d, iq)
            p = q + ln
            nodes.append((f[:6], f[6], f[7], f[8:]))
            for c in f[8:]:
                if c: read_node()
        elif tag == b'LEAF':
            end = q + ln
            it, il, iq = chunk_at(d, q); assert it == b'INFO' and il == 32
            f = struct.unpack_from('<6f2I', d, iq)
            r = iq + il
            idxs = flgs = None
            while r < end:
                tg, l, qq = chunk_at(d, r)
                if tg == b'IDXD':
                    sr = qq
                    while sr < qq + l:
                        t2, l2, q2 = chunk_at(d, sr)
                        if t2 == b'IDXS': idxs = struct.unpack_from(f'<{l2//2}H', d, q2)
                        elif t2 == b'FLGS': flgs = d[q2:q2+l2]
                        sr = q2 + l2
                r = qq + l
            leaves.append((f[:6], f[6], f[7], parse_idxs(idxs) if idxs else [], flgs))
            p = end
            p += 4      # 4 padding bytes (repeat of the last FLGS byte) follow every LEAF
        else:
            raise ValueError(f'unexpected tree chunk {tag} at {p}')
    read_node()
    t['nodes'] = nodes; t['leaves'] = leaves; t['tree_end'] = tend; t['tree_pos'] = p
    t['end'] = tend; t['size'] = size
    return t

if __name__ == '__main__':
    ok = 0; tot = 0
    for f in sorted(glob.glob('assets/Maps/*.tre')):
        d = open(f, 'rb').read(); tot += 1
        try:
            t = parse(d)
            ntri = sum(sum(len(g[3]) for g in lf[3]) // 3 for lf in t['leaves'])
            nfl = sum(len(lf[4] or b'') for lf in t['leaves'])
            consumed = t['tree_pos'] == len(d)
            tail = len(d) - t['tree_pos']
            print(f"{f.split('/')[-1]:34s} nodes={len(t['nodes']):4d} leaves={len(t['leaves']):4d} tris={ntri:7d} flgs={nfl:7d} "
                  f"verts={sum(b['n'] for b in t['vbufs'])} mats={len(t['materials'])} tree_consumed={consumed} tail={tail}")
            ok += consumed and ntri == nfl
        except Exception as e:
            print(f.split('/')[-1], 'FAIL', repr(e)[:100])
    print('ok', ok, '/', tot)

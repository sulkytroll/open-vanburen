#!/usr/bin/env python3
"""COL (lump 1700) collision grid: u32 width, u32 height, then per row (u32 run, u8 value) tokens until the runs sum to width."""
import struct, glob, collections
def parse(d):
    w, h = struct.unpack_from('<II', d, 0); o = 8; rows = []
    for _ in range(h):
        row = []; x = 0
        while x < w:
            run, = struct.unpack_from('<I', d, o); v = d[o+4]; o += 5
            if run == 0 or x + run > w: raise ValueError('run overflows row')
            row.append((run, v)); x += run
        rows.append(row)
    if o != len(d): raise ValueError(f'{len(d)-o} trailing bytes')
    return w, h, rows
def expand(w, h, rows):
    return [bytes(v for run, v in row for _ in range(run)) for row in rows]
if __name__ == '__main__':
    A = 'assets/'
    ok = 0; fs = sorted(glob.glob(A+'Maps/*.col') + glob.glob(A+'Engine/*.col')); vals = collections.Counter()
    for f in fs:
        try:
            w, h, rows = parse(open(f, 'rb').read()); ok += 1
            for r in rows:
                for run, v in r: vals[v] += run
        except Exception as e: print('FAIL', f.split('/')[-1], e)
    print('ok', ok, '/', len(fs)); print(sorted(vals.items(), key=lambda kv: -kv[1])[:20])

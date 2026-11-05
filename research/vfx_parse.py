#!/usr/bin/env python3
"""Parser for Effects/*.vfx.  File: "VEG V1.1", u32 effectCount, 12 bytes (zero in every shipped file), then per effect:
     "VFX V1.0", u64 0, u32 n, n x property      (effectCount times)
   property = u16 nameLen, name, u16 typeLen, type string ("float", "bool", "enum X", "VFX_Vector", ...), i32 -1, u32 valueSize, value.
   After the last effect: u32 m (= 3), m x property: the group's Loop, MinStartTime, MaxStartTime.  Each effect's own properties are its definition (particle system, model, light, rain ...).
   usage: vfx_parse.py FILE... [-v]  (-v prints every property)"""
import struct, sys, glob, collections

def read_prop(d, p):
    if p + 2 > len(d): return None
    (nl,) = struct.unpack_from('<H', d, p); q = p + 2
    if nl == 0 or nl > 80 or q + nl > len(d): return None
    name = d[q:q+nl]; q += nl
    if not all(32 <= c < 127 for c in name) or q + 2 > len(d): return None
    (tl,) = struct.unpack_from('<H', d, q); q += 2
    if tl == 0 or tl > 80 or q + tl > len(d): return None
    typ = d[q:q+tl]; q += tl
    if not all(32 <= c < 127 for c in typ) or q + 8 > len(d): return None
    m, size = struct.unpack_from('<iI', d, q); q += 8
    if m != -1 or q + size > len(d): return None
    return name.decode(), typ.decode(), d[q:q+size], q + size

def sval(v):
    if len(v) >= 2:
        (l,) = struct.unpack_from('<H', v, 0)
        if 2 + l == len(v) and all(32 <= c < 127 for c in v[2:]): return v[2:].decode()
    return None

def decode(typ, v):
    if typ == 'float' and len(v) == 4: return round(struct.unpack('<f', v)[0], 5)
    if typ == 'bool' and len(v) == 1: return bool(v[0])
    if typ in ('int', 'long', 'unsigned int', 'DWORD') and len(v) == 4: return struct.unpack('<i', v)[0]
    if typ == 'VFX_Color' and len(v) == 3: return '#%02x%02x%02x' % (v[2], v[1], v[0]) if False else v.hex()
    if typ == 'VFX_Byte' and len(v) == 1: return v[0]
    if typ == 'VFX_Resource' and len(v) >= 4:        # two strings: kind ("tga" texture, "b3d" model, "wav" sound ...) and the file name (may be empty)
        (l1,) = struct.unpack_from('<H', v, 0); k = v[2:2+l1].decode('latin1')
        if 4 + l1 <= len(v):
            (l2,) = struct.unpack_from('<H', v, 2 + l1)
            if 4 + l1 + l2 == len(v): return k + ':' + v[4+l1:].decode('latin1')
    s = sval(v)
    if s is not None and (typ == 'std::string' or typ.startswith('enum ') or typ.startswith('VFX_')): return s
    if len(v) % 4 == 0 and 0 < len(v) <= 36: return [round(x, 4) for x in struct.unpack('<%df' % (len(v)//4), v)]
    return v.hex()

def parse(path):
    """returns (effects, groupTiming): effects = list of property lists; groupTiming = {Loop, MinStartTime, MaxStartTime}"""
    d = open(path, 'rb').read()
    if d[:8] != b'VEG V1.1': raise ValueError('not a VEG V1.1 file')
    (cnt,) = struct.unpack_from('<I', d, 8); p = 0x18; effects = []
    for _ in range(cnt):
        if d[p:p+8] != b'VFX V1.0': raise ValueError('expected VFX at 0x%x' % p)
        (n,) = struct.unpack_from('<I', d, p + 16); p += 20; props = []
        for _ in range(n):
            r = read_prop(d, p)
            if not r: raise ValueError('bad property at 0x%x' % p)
            props.append((r[0], r[1], decode(r[1], r[2]), r[2])); p = r[3]
        effects.append(props)
    (m,) = struct.unpack_from('<I', d, p); p += 4; trail = []
    for _ in range(m):
        r = read_prop(d, p)
        if not r: raise ValueError('bad trailer property at 0x%x' % p)
        trail.append((r[0], r[1], decode(r[1], r[2]), r[2])); p = r[3]
    if p != len(d): raise ValueError('%d bytes left over' % (len(d) - p))
    timing = {n: v for n, t, v, raw in trail}          # group level: Loop, MinStartTime, MaxStartTime (always 3 properties)
    return effects, timing

def catalog_line(path):
    effs, timing = parse(path); parts = []
    for props in effs:
        dd = {n: v for n, t, v, raw in props}; et = dd['EffectType'].replace('VFXEFFECTTYPE_', '').lower()
        tgt = dd['Target'].replace('VFXTARGET_', '').lower(); snap = dd.get('SnapBone') or ''
        extra = ''
        for k in ('Texture', 'Model', 'WAVFile', 'SnowTexture'):
            v = dd.get(k)
            if isinstance(v, str) and ':' in v and not v.endswith(':'): extra = v.split(':', 1)[1]; break
        parts.append('%s "%s" t=%g+%g %s%s%s' % (et, dd['Name'], dd['Delay'], dd['Duration'], tgt, ('/' + snap) if snap else '', (' [' + extra + ']') if extra else ''))
    return '%s%s: %s' % (path.split('/')[-1], ' (loops)' if timing.get('Loop') else '', ' | '.join(parts))

if __name__ == '__main__':
    if '-catalog' in sys.argv:
        for f in sorted(a for a in sys.argv[1:] if not a.startswith('-')): print(catalog_line(f))
        sys.exit(0)
    files = [a for a in sys.argv[1:] if not a.startswith('-')]; verbose = '-v' in sys.argv
    ok = bad = 0; types = collections.Counter(); names = collections.Counter()
    for f in files:
        try: eff, timing = parse(f); ok += 1
        except Exception as e: bad += 1; print('FAIL', f, e); continue
        for props in eff:
            dd = {n: v for n, t, v, raw in props}; types[dd.get('EffectType')] += 1
            for n, t, v, raw in props: names[(n, t)] += 1
            if verbose:
                print('==', f, len(props), 'props'); [print('  %-28s %-26s %s' % (n, t, v)) for n, t, v, raw in props]; pass
    print('files ok=%d bad=%d' % (ok, bad)); print(types)
    if '-names' in sys.argv:
        for (n, t), c in sorted(names.items()): print('%5d %-32s %s' % (c, n, t))

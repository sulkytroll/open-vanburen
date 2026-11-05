#include "gr2.h"
#include "binreader.h"
#include "oodle0.h"
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

namespace vb {

const GVal& GVal::at(const char* name) const {
    static const GVal null;
    for (auto& m : members) if (m.first == name) return m.second;
    return null;
}

namespace {

struct Section {
    std::vector<uint8_t> data;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> reloc;   // offset -> (target section, target offset)
};

struct Member { uint32_t type; std::string name; bool hasRef; uint32_t refSec, refOff; uint32_t arr; };

enum { End = 0, Inline = 1, Reference = 2, RefToArray = 3, ArrayOfRefs = 4, VariantRef = 5, RefToVariantArray = 7, String = 8, Transform = 9,
       Real32 = 10, Int8 = 11, UInt8 = 12, BinInt8 = 13, NormUInt8 = 14, Int16 = 15, UInt16 = 16, BinInt16 = 17, NormUInt16 = 18, Int32 = 19, UInt32 = 20, Real16 = 21 };

struct Decoder {
    std::vector<Section> secs;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<Member>> typeCache;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> sizeCache;

    template <class T> T rd(uint32_t sec, uint32_t off) const {
        const auto& d = secs.at(sec).data; if ((size_t)off + sizeof(T) > d.size()) throw std::runtime_error("GR2: read out of range");
        T v; memcpy(&v, &d[off], sizeof(T)); return v;
    }
    bool ptr(uint32_t sec, uint32_t off, uint32_t& tsec, uint32_t& toff) const {
        auto& r = secs.at(sec).reloc; auto it = r.find(off);
        if (it == r.end()) return false; tsec = it->second.first; toff = it->second.second; return true;
    }
    std::string cstr(uint32_t sec, uint32_t off) const {
        const auto& d = secs.at(sec).data; if (off >= d.size()) throw std::runtime_error("GR2: bad string");
        size_t e = off; while (e < d.size() && d[e]) e++;
        return std::string((const char*)&d[off], e - off);
    }
    const std::vector<Member>& type(uint32_t sec, uint32_t off) {
        auto key = std::make_pair(sec, off);
        auto it = typeCache.find(key); if (it != typeCache.end()) return it->second;
        std::vector<Member> v;
        for (uint32_t o = off;; o += 32) {
            Member m; m.type = rd<uint32_t>(sec, o); if (m.type == End) break;
            uint32_t ts, to;
            m.name = ptr(sec, o + 4, ts, to) ? cstr(ts, to) : "";
            m.hasRef = ptr(sec, o + 8, m.refSec, m.refOff);
            m.arr = rd<uint32_t>(sec, o + 12);
            v.push_back(std::move(m));
            if (v.size() > 4096) throw std::runtime_error("GR2: runaway type");
        }
        return typeCache[key] = std::move(v);
    }
    uint32_t scalarSize(uint32_t t) const {
        switch (t) { case Real32: case Int32: case UInt32: case Reference: case String: return 4; case Int8: case UInt8: case BinInt8: case NormUInt8: return 1;
            case Int16: case UInt16: case BinInt16: case NormUInt16: case Real16: return 2; case RefToArray: case ArrayOfRefs: case VariantRef: return 8;
            case RefToVariantArray: return 12; case Transform: return 68; default: throw std::runtime_error("GR2: unsupported member type " + std::to_string(t)); }
    }
    uint32_t memberSize(const Member& m) {
        uint32_t n = m.arr ? m.arr : 1;
        if (m.type == Inline) return structSize(m.refSec, m.refOff) * n;
        return scalarSize(m.type) * n;
    }
    uint32_t structSize(uint32_t sec, uint32_t off) {
        auto key = std::make_pair(sec, off); auto it = sizeCache.find(key); if (it != sizeCache.end()) return it->second;
        uint32_t s = 0; for (const Member& m : type(sec, off)) s += memberSize(m);
        return sizeCache[key] = s;
    }
    // Struct made of exactly one Real32 member (the "Real32" variant used for Knots/Controls arrays).
    bool isFloatBox(uint32_t sec, uint32_t off) {
        const auto& t = type(sec, off); return t.size() == 1 && t[0].type == Real32 && t[0].arr == 0;
    }

    GVal scalar(const Member& m, uint32_t sec, uint32_t o, int depth) {
        GVal v; if (depth > 24) throw std::runtime_error("GR2: nesting too deep");
        switch (m.type) {
        case Inline: return object(m.refSec, m.refOff, sec, o, depth + 1);
        case Reference: { uint32_t ts, to; if (ptr(sec, o, ts, to)) return object(m.refSec, m.refOff, ts, to, depth + 1); return v; }
        case RefToArray: case ArrayOfRefs: {
            uint32_t cnt = rd<uint32_t>(sec, o), ts, to; v.kind = GVal::Arr;
            if (cnt == 0 || !ptr(sec, o + 4, ts, to)) return v;
            if (m.type == RefToArray) {
                uint32_t sz = structSize(m.refSec, m.refOff);
                for (uint32_t k = 0; k < cnt; k++) v.items.push_back(object(m.refSec, m.refOff, ts, to + k * sz, depth + 1));
            } else {
                for (uint32_t k = 0; k < cnt; k++) { uint32_t s2, o2; if (ptr(ts, to + 4 * k, s2, o2)) v.items.push_back(object(m.refSec, m.refOff, s2, o2, depth + 1)); else v.items.emplace_back(); }
            }
            return v;
        }
        case VariantRef: {
            uint32_t tsec, toff, ps, po;
            if (ptr(sec, o, tsec, toff) && ptr(sec, o + 4, ps, po)) return object(tsec, toff, ps, po, depth + 1);
            return v;
        }
        case RefToVariantArray: {
            uint32_t tsec, toff, ps, po; uint32_t cnt = rd<uint32_t>(sec, o + 4);
            if (!(ptr(sec, o, tsec, toff) && ptr(sec, o + 8, ps, po)) || !cnt) { v.kind = GVal::Arr; return v; }
            if (isFloatBox(tsec, toff)) { v.kind = GVal::Floats; v.floats.resize(cnt); for (uint32_t k = 0; k < cnt; k++) v.floats[k] = rd<float>(ps, po + 4 * k); return v; }
            v.kind = GVal::Arr; uint32_t sz = structSize(tsec, toff);
            for (uint32_t k = 0; k < cnt; k++) v.items.push_back(object(tsec, toff, ps, po + k * sz, depth + 1));
            return v;
        }
        case String: { uint32_t ts, to; if (ptr(sec, o, ts, to)) { v.kind = GVal::Str; v.str = cstr(ts, to); } return v; }
        case Transform: {
            v.kind = GVal::Obj;
            auto add = [&](const char* n, GVal g) { v.members.emplace_back(n, std::move(g)); };
            GVal fl; fl.kind = GVal::Num; fl.num = rd<uint32_t>(sec, o); add("flags", fl);
            GVal f; f.kind = GVal::Floats; f.floats.resize(16); for (int k = 0; k < 16; k++) f.floats[k] = rd<float>(sec, o + 4 + 4 * k);
            GVal p, q, s; p.kind = q.kind = s.kind = GVal::Floats;
            p.floats.assign(f.floats.begin(), f.floats.begin() + 3); q.floats.assign(f.floats.begin() + 3, f.floats.begin() + 7);
            s.floats.resize(9); for (int k = 0; k < 9; k++) s.floats[k] = rd<float>(sec, o + 4 + 28 + 4 * k);
            add("pos", p); add("orient", q); add("ss", s);
            return v;
        }
        case Real32: v.kind = GVal::Num; v.num = rd<float>(sec, o); return v;
        case Int8: v.kind = GVal::Num; v.num = rd<int8_t>(sec, o); return v;
        case UInt8: case NormUInt8: v.kind = GVal::Num; v.num = rd<uint8_t>(sec, o); return v;
        case BinInt8: v.kind = GVal::Num; v.num = rd<int8_t>(sec, o); return v;
        case Int16: case BinInt16: v.kind = GVal::Num; v.num = rd<int16_t>(sec, o); return v;
        case UInt16: case NormUInt16: case Real16: v.kind = GVal::Num; v.num = rd<uint16_t>(sec, o); return v;
        case Int32: v.kind = GVal::Num; v.num = rd<int32_t>(sec, o); return v;
        case UInt32: v.kind = GVal::Num; v.num = rd<uint32_t>(sec, o); return v;
        default: throw std::runtime_error("GR2: unsupported member type " + std::to_string(m.type));
        }
    }
    GVal object(uint32_t tsec, uint32_t toff, uint32_t sec, uint32_t off, int depth) {
        GVal obj; obj.kind = GVal::Obj; uint32_t o = off;
        for (const Member& m : type(tsec, toff)) {
            uint32_t one = m.type == Inline ? structSize(m.refSec, m.refOff) : scalarSize(m.type);
            if (m.arr) {
                GVal a; a.kind = GVal::Arr;
                for (uint32_t k = 0; k < m.arr; k++) { a.items.push_back(scalar(m, sec, o, depth)); o += one; }
                obj.members.emplace_back(m.name, std::move(a));
            } else { obj.members.emplace_back(m.name, scalar(m, sec, o, depth)); o += one; }
        }
        return obj;
    }
};

GrTransform toTransform(const GVal& t) {
    GrTransform r; r.flags = (uint32_t)t.at("flags").num;
    const GVal& p = t.at("pos"); const GVal& q = t.at("orient"); const GVal& s = t.at("ss");
    for (int k = 0; k < 3 && k < (int)p.floats.size(); k++) r.pos[k] = p.floats[k];
    for (int k = 0; k < 4 && k < (int)q.floats.size(); k++) r.orient[k] = q.floats[k];
    for (int k = 0; k < 9 && k < (int)s.floats.size(); k++) r.ss[k] = s.floats[k];
    return r;
}
std::vector<float> floatsOf(const GVal& a) {      // array of floats, or array of one-member {Real32} structs
    if (a.kind == GVal::Floats) return a.floats;
    std::vector<float> v;
    for (const GVal& it : a.items) v.push_back(it.kind == GVal::Num ? it.f() : it.at("Real32").f());
    return v;
}
GrCurve toCurve(const GVal& c, int dim) {
    GrCurve r; r.dim = dim; r.degree = c.at("Degree").i();
    r.knots = floatsOf(c.at("Knots")); r.controls = floatsOf(c.at("Controls"));
    return r;
}

} // namespace

void GrCurve::sample(float t, float* out) const {
    // placeholder replaced below
    (void)t; (void)out;
}

Gr2 loadGr2(const std::string& path) {
    std::vector<uint8_t> d = readFileBytes(path);
    static const uint8_t magic[16] = {0xb8, 0x67, 0xb0, 0xca, 0xf8, 0x6d, 0xb1, 0x0f, 0x84, 0x72, 0x8c, 0x7e, 0x5e, 0x19, 0x00, 0x1e};
    if (d.size() < 0x80 || memcmp(d.data(), magic, 16) != 0) throw std::runtime_error("GR2: bad magic");
    auto u32 = [&](size_t o) { if (o + 4 > d.size()) throw std::runtime_error("GR2: truncated"); uint32_t v; memcpy(&v, &d[o], 4); return v; };
    uint32_t version = u32(32), secOff = u32(44), nsec = u32(48), rtSec = u32(52), rtOff = u32(56), rootSec = u32(60), rootOff = u32(64);
    if (version != 6 || nsec == 0 || nsec > 64) throw std::runtime_error("GR2: unsupported header");
    Decoder dec; dec.secs.resize(nsec);
    for (uint32_t i = 0; i < nsec; i++) {
        size_t so = 32 + (size_t)secOff + (size_t)i * 44;
        uint32_t comp = u32(so), off = u32(so + 4), size = u32(so + 8), exp = u32(so + 12), f16 = u32(so + 20), f8 = u32(so + 24), relOff = u32(so + 28), nrel = u32(so + 32);
        Section& s = dec.secs[i];
        if (comp == 0) { if ((size_t)off + size > d.size()) throw std::runtime_error("GR2: section out of range"); s.data.assign(d.begin() + off, d.begin() + off + size); }
        else if (comp == 1) {
            if (size == 0) { /* empty */ }
            else {
                if ((size_t)off + size > d.size()) throw std::runtime_error("GR2: section out of range");
                s.data.assign((size_t)exp + 16, 0);
                if (!oodle0Decompress(&d[off], size, f16, f8, exp, s.data.data())) throw std::runtime_error("GR2: Oodle0 stream corrupt");
                s.data.resize(exp);
            }
        } else throw std::runtime_error("GR2: unsupported section compression " + std::to_string(comp));
        if ((size_t)relOff + (size_t)nrel * 12 > d.size()) throw std::runtime_error("GR2: relocations out of range");
        for (uint32_t k = 0; k < nrel; k++) {
            uint32_t a = u32(relOff + 12 * k), ts = u32(relOff + 12 * k + 4), to = u32(relOff + 12 * k + 8);
            s.reloc[a] = {ts, to};
        }
    }
    Gr2 g;
    g.root = std::make_unique<GVal>(dec.object(rtSec, rtOff, rootSec, rootOff, 0));
    const GVal& r = *g.root;
    for (const GVal& sk : r.at("Skeletons").items) {
        GrSkeleton s; s.name = sk.at("Name").str;
        for (const GVal& b : sk.at("Bones").items) {
            GrBone bone; bone.name = b.at("Name").str; bone.parent = b.at("ParentIndex").i();
            bone.local = toTransform(b.at("Transform"));
            const GVal& iw = b.at("InverseWorldTransform");
            for (int k = 0; k < 16 && k < (int)iw.items.size(); k++) bone.inverseWorld[k] = iw.items[k].f();
            s.bones.push_back(std::move(bone));
        }
        g.skeletons.push_back(std::move(s));
    }
    for (const GVal& a : r.at("Animations").items) {
        GrAnimation an; an.name = a.at("Name").str; an.duration = a.at("Duration").f(); an.timeStep = a.at("TimeStep").f();
        for (const GVal& tg : r.at("TrackGroups").items)
            for (const GVal& t : tg.at("TransformTracks").items) {
                GrTrack tr; tr.name = t.at("Name").str;
                tr.position = toCurve(t.at("PositionCurve"), 3); tr.orientation = toCurve(t.at("OrientationCurve"), 4); tr.scaleShear = toCurve(t.at("ScaleShearCurve"), 9);
                an.tracks.push_back(std::move(tr));
            }
        g.animations.push_back(std::move(an));
    }
    return g;
}

} // namespace vb

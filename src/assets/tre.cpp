#include "tre.h"
#include "binreader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace vb {
namespace {

struct Chunk { std::string tag; uint32_t len; };
Chunk chunk(BinReader& r) { Chunk c; c.tag = r.tag(); c.len = r.u32(); return c; }

void expect(const Chunk& c, const char* tag) {
    if (c.tag != tag) throw std::runtime_error(std::string("TRE: expected ") + tag + ", got '" + c.tag + "'");
}

std::string cstr(const uint8_t* p, size_t max) {
    size_t n = 0; while (n < max && p[n]) n++;
    return std::string((const char*)p, n);
}

std::vector<TreBatch> parseIdxs(const uint8_t* p, size_t bytes) {
    if (bytes % 2) throw std::runtime_error("TRE: odd IDXS size");
    size_t n = bytes / 2;
    std::vector<uint16_t> w(n);
    memcpy(w.data(), p, bytes);
    std::vector<TreBatch> out;
    size_t i = 0;
    auto at = [&](size_t k) -> uint32_t { if (k >= n) throw std::runtime_error("TRE: IDXS overrun"); return w[k]; };
    while (i < n) {
        uint32_t m = at(i), c1 = at(i + 1);
        if (!(m & 0x8000)) throw std::runtime_error("TRE: bad IDXS level-1 marker");
        size_t q = i + 2, e = q + c1;
        while (q < e) {
            uint32_t m2 = at(q), c2 = at(q + 1);
            if (!(m2 & 0x4000)) throw std::runtime_error("TRE: bad IDXS level-2 marker");
            size_t r = q + 2, e2 = r + c2;
            while (r < e2) {
                uint32_t layer = at(r), c3 = at(r + 1);
                if (r + 2 + c3 > n) throw std::runtime_error("TRE: IDXS run overrun");
                TreBatch b;
                b.material = m & 0x7fff; b.lightmap = m2 & 0x3fff; b.layer = layer;
                b.indices.assign(w.begin() + r + 2, w.begin() + r + 2 + c3);
                out.push_back(std::move(b));
                r += 2 + c3 + (c3 & 1);
            }
            if (r != e2) throw std::runtime_error("TRE: IDXS level-3 size mismatch");
            q = e2;
        }
        if (q != e) throw std::runtime_error("TRE: IDXS level-2 size mismatch");
        i = e;
    }
    return out;
}

void readNode(BinReader& r, Tre& t, bool root) {
    Chunk c = chunk(r);
    if (c.tag == "NODE") {
        BinReader s = r.sub(c.len);
        Chunk info = chunk(s);
        expect(info, "INFO");
        if (info.len != 64) throw std::runtime_error("TRE: NODE INFO size");
        float b[6]; s.f32n(b, 6);
        s.u32(); s.u32();
        uint32_t child[8]; for (auto& v : child) v = s.u32();
        if (root) { memcpy(t.min, b, 12); memcpy(t.max, b + 3, 12); }
        t.node_count++;
        for (uint32_t v : child) if (v) readNode(r, t, false);
    } else if (c.tag == "LEAF") {
        // Every leaf is followed by 4 padding bytes that the LEAF length does not count (IDXD's length sometimes does).
        size_t leaf_end = r.pos() + c.len;       // + 4 padding bytes
        Chunk info = chunk(r);
        expect(info, "INFO");
        if (info.len != 32) throw std::runtime_error("TRE: LEAF INFO size");
        TreLeaf lf;
        r.f32n(lf.min, 3); r.f32n(lf.max, 3);
        lf.buffer = r.u32(); lf.flags = r.u32();
        if (root) { memcpy(t.min, lf.min, 12); memcpy(t.max, lf.max, 12); }
        // Mirror of the verified reference parser (tre_parse.py): walk sibling chunks by absolute offset up to the
        // leaf length; inside IDXD walk sub-chunks up to its declared length (which may overshoot the leaf by the
        // 4 padding bytes and then reads garbage that is ignored).
        size_t rr = r.pos();
        while (rr < leaf_end) {
            r.seek(rr);
            Chunk c2 = chunk(r);
            size_t qq = r.pos();
            if (c2.tag == "IDXD") {
                size_t sr = qq;
                while (sr < qq + c2.len && sr + 8 <= r.size()) {
                    r.seek(sr);
                    Chunk c3 = chunk(r);
                    size_t q2 = r.pos();
                    if (c3.tag == "IDXS" || c3.tag == "FLGS") {
                        if (q2 + c3.len > r.size()) throw std::runtime_error("TRE: IDXD sub-chunk overruns file");
                        if (c3.tag == "IDXS") lf.batches = parseIdxs(r.ptr(), c3.len);
                        else lf.tri_flags.assign(r.ptr(), r.ptr() + c3.len);
                    }
                    sr = q2 + c3.len;
                }
            }
            rr = qq + c2.len;
        }
        r.seek(leaf_end);
        r.skip(4);                                  // padding
        if (lf.buffer >= t.buffers.size()) throw std::runtime_error("TRE: leaf buffer id out of range");
        uint32_t nv = t.buffers[lf.buffer].count; size_t tris = 0;
        for (auto& b : lf.batches) {
            if (b.material >= t.materials.size()) throw std::runtime_error("TRE: material index out of range");
            if (b.indices.size() % 3) throw std::runtime_error("TRE: index count not a multiple of 3");
            for (uint16_t i : b.indices) if (i >= nv) throw std::runtime_error("TRE: vertex index out of range");
            tris += b.indices.size() / 3;
        }
        if (!lf.tri_flags.empty() && lf.tri_flags.size() != tris) throw std::runtime_error("TRE: FLGS size != triangle count");
        t.leaves.push_back(std::move(lf));
    } else {
        throw std::runtime_error("TRE: unexpected tree chunk '" + c.tag + "'");
    }
}

} // namespace

std::vector<uint16_t> TreLightGrid::visibleLights(float x, float z) const {
    std::vector<uint16_t> out;
    int col = (int)((x - min_x) / sx + 0.5f), row = (int)((max_z - z) / sy + 0.5f);   // engine 0x5afce0
    if (x < min_x || z > max_z || col < 0 || row < 0 || (uint32_t)col >= w || (uint32_t)row >= h) return out;
    uint32_t cell = (uint32_t)row * w + (uint32_t)col, bit = cell & 31;
    size_t pair = (cell >> 5) * 8;
    auto u32at = [&](size_t o) { uint32_t v; std::memcpy(&v, &data[o], 4); return v; };
    auto u16at = [&](size_t o) -> uint16_t { if (o + 2 > data.size()) return 0; uint16_t v; std::memcpy(&v, &data[o], 2); return v; };
    uint32_t off = u32at(pair), mask = u32at(pair + 4);
    if (!(mask & (0x80000000u >> bit))) return out;
    size_t pos = off;
    uint16_t par = u16at(pos) & 0x8000;
    for (uint32_t b = 0; b < bit; b++) {            // skip the lists of earlier cells in this block
        if (!(mask & (0x80000000u >> b))) continue;
        while (pos < data.size() && (u16at(pos) & 0x8000) == par) pos += 2;
        par ^= 0x8000;
    }
    while (pos < data.size() && (u16at(pos) & 0x8000) == par) { out.push_back(u16at(pos) & 0x7fff); pos += 2; }
    return out;
}

Tre loadTre(const std::string& path) {
    std::vector<uint8_t> data = readFileBytes(path);
    BinReader r(data.data(), data.size());
    if (r.tag() != "8TRE") throw std::runtime_error("TRE: bad magic");
    r.u32();                                    // size field (file size - 40, not needed)

    Tre t;
    {
        Chunk c = chunk(r); expect(c, "HEAD");
        if (c.len != 16) throw std::runtime_error("TRE: HEAD size");
        for (auto& v : t.head) v = r.u32();
    }
    bool have_tree = false;
    while (!have_tree) {
        Chunk c = chunk(r);
        if (c.tag == "MATD") {
            BinReader s = r.sub(c.len);
            Chunk info = chunk(s); expect(info, "INFO");
            uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; i++) {
                Chunk m = chunk(s); expect(m, "MATR");
                if (m.len != 196) throw std::runtime_error("TRE: MATR size");
                BinReader x = s.sub(m.len);
                TreMaterial mat;
                mat.shader = x.u32();
                const uint8_t* b = x.ptr();
                mat.name = cstr(b, 80);
                memcpy(mat.rgba, b + 80, 4);
                mat.blend = cstr(b + 84, 16);
                mat.surface = cstr(b + 100, 80);
                memcpy(mat.extra, b + 180, 12);
                t.materials.push_back(std::move(mat));
            }
            if (!s.done()) throw std::runtime_error("TRE: MATD trailing bytes");
        } else if (c.tag == "TXTD") {
            BinReader s = r.sub(c.len);
            t.first_lightmap = s.u32();
            uint32_t total = s.u32();
            for (uint32_t i = 1; i < total; i++) {
                Chunk x = chunk(s); expect(x, "TXTR");
                BinReader y = s.sub(x.len);
                t.textures.push_back(cstr(y.ptr(), y.size()));
            }
            if (!s.done()) throw std::runtime_error("TRE: TXTD trailing bytes");
        } else if (c.tag == "VTXD") {
            BinReader s = r.sub(c.len);
            uint32_t nbuf = s.u32();
            for (uint32_t i = 0; i < nbuf; i++) {
                Chunk vb = chunk(s); expect(vb, "VTXB");
                BinReader x = s.sub(vb.len);
                TreVertexBuffer b; b.count = x.u32();
                size_t n = b.count;
                while (!x.done()) {
                    Chunk a = chunk(x);
                    BinReader d = x.sub(a.len);
                    auto floats = [&](std::vector<float>& v, size_t per) {
                        if (d.size() != n * per * 4) throw std::runtime_error("TRE: vertex array size");
                        v.resize(n * per); memcpy(v.data(), d.ptr(), d.size());
                    };
                    if (a.tag == "XYZ ") floats(b.xyz, 3);
                    else if (a.tag == "NRML") floats(b.nrm, 3);
                    else if (a.tag == "TXUV") floats(b.uv, 2);
                    else if (a.tag == "LMUV") floats(b.lmuv, 2);
                    else if (a.tag == "DIFU") {
                        if (d.size() != n * 4) throw std::runtime_error("TRE: DIFU size");
                        b.diffuse.resize(n); memcpy(b.diffuse.data(), d.ptr(), d.size());
                    }
                }
                if (b.xyz.empty()) throw std::runtime_error("TRE: vertex buffer without XYZ");
                if (b.nrm.empty()) b.nrm.assign(n * 3, 0.f);
                if (b.uv.empty()) b.uv.assign(n * 2, 0.f);
                if (b.lmuv.empty()) b.lmuv.assign(n * 2, 0.f);
                if (b.diffuse.empty()) b.diffuse.assign(n, 0xffffffffu);
                t.buffers.push_back(std::move(b));
            }
        } else if (c.tag == "TREE") {
            have_tree = true;
        } else if (c.tag == "LGTD") {
            BinReader s = r.sub(c.len);
            Chunk info = chunk(s); expect(info, "INFO");
            BinReader ib = s.sub(info.len);
            uint32_t n = ib.u32();
            for (uint32_t i = 0; i < n; i++) {
                Chunk lc = chunk(s); expect(lc, "LGTR");
                if (lc.len != 64) throw std::runtime_error("TRE: LGTR size");
                BinReader x = s.sub(lc.len);
                TreLight l;
                l.type = x.u32(); l.aux = x.u32();
                for (float& v : l.color) v = x.f32();
                for (float& v : l.pos) v = x.f32();
                for (float& v : l.dir) v = x.f32();
                l.intensity = x.f32(); l.range = x.f32(); l.falloff = x.f32(); l.inner = x.f32(); l.outer = x.f32();
                t.lights.push_back(l);
            }
            if (!s.done()) throw std::runtime_error("TRE: LGTD trailing bytes");
        } else if (c.tag == "LVLD") {
            BinReader s = r.sub(c.len);
            Chunk info = chunk(s); expect(info, "INFO");
            if (info.len != 16) throw std::runtime_error("TRE: LVLD INFO size");
            BinReader ib = s.sub(info.len);
            t.grid.w = ib.u32(); t.grid.h = ib.u32(); t.grid.sx = ib.f32(); t.grid.sy = ib.f32();
            Chunk l0 = chunk(s); expect(l0, "LVL0");
            BinReader d = s.sub(l0.len);
            t.grid.data.assign(d.ptr(), d.ptr() + d.size());
            size_t cells = (size_t)t.grid.w * t.grid.h;
            if (t.grid.data.size() < (cells + 31) / 32 * 8) throw std::runtime_error("TRE: LVL0 too small");
            t.has_grid = true;
            if (!s.done()) throw std::runtime_error("TRE: LVLD trailing bytes");
        } else {
            r.skip(c.len);
        }
    }
    readNode(r, t, true);
    t.grid.min_x = t.min[0]; t.grid.max_z = t.max[2];
    if (!r.done()) throw std::runtime_error("TRE: " + std::to_string(r.remaining()) + " trailing bytes after tree");
    return t;
}

} // namespace vb

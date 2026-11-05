#include "map.h"
#include "binreader.h"

namespace vb {

void readEeov(BinReader& r, uint32_t ver, MapEntity& e) {
    e.instance = r.lpstr();
    r.skip(3);                                  // three flag bytes
    r.u32(); r.u32();
    e.display = r.lpstr(); e.script = r.lpstr();
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; i++) { std::string k = r.lpstr(); uint32_t v = r.u32(); e.vars.emplace_back(k, v); }
    r.u8(); r.f32();
    e.skin = r.lpstr();
    uint8_t m = r.u8();
    for (uint8_t i = 0; i < m; i++) e.items.push_back(r.lpstr());
    if (ver >= 1) r.skip(2);
    if (ver >= 2) { uint32_t k = r.u32(); for (uint32_t i = 0; i < k; i++) e.items.push_back(r.lpstr()); }
    if (!r.done()) throw std::runtime_error("MAP: EEOV not fully consumed");
}

Map loadMap(const std::string& path) {
    std::vector<uint8_t> d = readFileBytes(path);
    BinReader r(d.data(), d.size());
    Map m;
    MapTrigger* pending = nullptr;              // trigger waiting for its ETTR/ESTR/EBTR data chunk
    bool first = true;
    while (!r.done()) {
        size_t start = r.pos();
        std::string tag = r.tag(); uint32_t ver = r.u32(), len = r.u32();
        if (len < 12 || start + len > r.size()) throw std::runtime_error("MAP: bad chunk length for " + tag);
        if (first && tag != "EMAP") throw std::runtime_error("MAP: missing EMAP header");
        first = false;
        BinReader s = r.sub(len - 12);
        if (tag == "EMAP") {
            m.id = s.u32(); m.heightmap = s.lpstr(); m.rle = s.lpstr(); m.ambient_map = s.lpstr(); s.lpstr();
            if (ver >= 1) for (auto& c : m.ambient_rgb) c = s.u8();
            if (ver >= 2) s.u8();
            if (ver >= 3) for (auto& c : m.light_rgba) c = s.u8();
            if (ver >= 4) s.u8();
            if (ver >= 5) { m.fog_enable = s.u8(); for (auto& c : m.fog_rgb) c = s.u8(); s.f32n(m.fog_params, 3); }
        } else if (tag == "ECAM") {
            s.f32n(m.camera, 4);
        } else if (tag == "EME2") {
            MapEntity e; e.file = s.lpstr(); s.f32n(e.pos, 3); s.f32n(e.rot, 3); e.flag = s.u8();
            std::string t2 = s.tag(); uint32_t v2 = s.u32(), l2 = s.u32();
            if (t2 != "EEOV" || l2 < 12) throw std::runtime_error("MAP: EME2 without EEOV");
            BinReader o = s.sub(l2 - 12);
            readEeov(o, v2, e);
            m.entities.push_back(std::move(e));
        } else if (tag == "EMEF") {
            MapEffect f; f.name = s.lpstr(); s.f32n(f.pos, 3); s.f32n(f.rot, 3);
            for (uint8_t n = s.u8(); n; n--) f.effects.push_back(s.lpstr());
            m.effects.push_back(std::move(f));
        } else if (tag == "EMSD") {
            MapSound a; a.name = s.lpstr(); s.f32n(a.pos, 3); a.sound = s.lpstr();
            m.sounds.push_back(std::move(a));
        } else if (tag == "EMTR") {
            MapTrigger t; t.type = s.u32(); uint32_t n = s.u32();
            for (uint32_t i = 0; i < n * 3; i++) t.verts.push_back(s.f32());
            m.triggers.push_back(std::move(t));
            pending = &m.triggers.back();
            continue;                           // keep `pending` for the next chunk
        } else if (tag == "ETTR" || tag == "ESTR" || tag == "EBTR") {
            if (!pending) throw std::runtime_error("MAP: " + tag + " without EMTR");
            if (tag == "ETTR") { pending->target = s.lpstr(); pending->entry = s.u8(); }
            else if (tag == "ESTR") { pending->target = s.lpstr(); pending->id = s.u32(); }
            else pending->id = s.u32();
        } else if (tag == "EMEP") {
            MapEntry e; e.id = s.u8();
            if (s.remaining() >= 96) { for (auto& p : e.pos) s.f32n(p, 3); s.f32n(e.angle, 6); }
            m.entries.push_back(e);
        } else if (tag == "EPTH") {
            MapPath p; p.name = s.lpstr(); uint32_t n = s.u32();
            for (uint32_t i = 0; i < n; i++) { float f[6]; s.f32n(f, 6); p.pts.insert(p.pts.end(), f, f + 3); }
            m.paths.push_back(std::move(p));
        } else if (tag == "EMNP") {
            if (s.remaining() >= 4) {
                uint32_t n = s.u32();
                for (uint32_t i = 0; i < n; i++) {
                    MapNavNode nn; nn.flag = s.u8(); s.f32n(nn.pos, 3); s.skip(12);
                    for (auto& l : nn.links) l = s.u8();
                    m.nav.push_back(nn);
                }
            }
        } else if (tag == "EMNO") {
            MapNote n; s.f32n(n.pos, 3); n.icon = s.lpstr(); m.notes.push_back(std::move(n));
        }
        // 2MWT (water), EMEN/EMFG (legacy) and unknown tags: skipped by length
        pending = nullptr;
    }
    return m;
}

} // namespace vb

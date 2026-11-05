#include "assets/vfs.h"
#include "entity.h"
#include "binreader.h"
#include "g3d.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace vb {
namespace {

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

bool printable(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i] < 0x20 || p[i] > 0x7e) return false;
    return true;
}

// The GCRE / GITM chunk interiors are not decoded field by field; the strings they hold are length-prefixed (u16) so they
// can be found reliably by scanning.
std::vector<std::string> scanStrings(BinReader& r) {
    std::vector<std::string> out;
    size_t base = r.pos(); (void)base;
    const uint8_t* p = r.ptr(); size_t n = r.remaining();
    for (size_t i = 0; i + 2 <= n;) {
        uint16_t l = (uint16_t)(p[i] | (p[i + 1] << 8));
        if (l >= 3 && l <= 120 && i + 2 + l <= n && printable(p + i + 2, l)) { out.emplace_back((const char*)p + i + 2, l); i += 2 + l; }
        else i++;
    }
    return out;
}

const char* kindForExt(const std::string& ext) {
    static const std::map<std::string, const char*> m = {{".critter", "critter"}, {".door", "door"}, {".item", "item"}, {".weapon", "weapon"},
        {".armor", "armor"}, {".ammo", "ammo"}, {".use", "use"}, {".con", "con"}};
    auto it = m.find(ext); return it == m.end() ? "" : it->second;
}

} // namespace

EntityTemplate loadEntityTemplate(const std::string& path) {
    std::vector<uint8_t> d = readFileBytes(path);
    BinReader r(d.data(), d.size());
    EntityTemplate t; t.path = path; t.kind = kindForExt(lower(fs::path(path).extension().string()));
    bool first = true;
    while (!r.done()) {
        size_t start = r.pos();
        std::string tag = r.tag(); uint32_t ver = r.u32(), len = r.u32();      // len includes the 12-byte header
        if (len < 12 || start + len > r.size()) throw std::runtime_error("ENT: bad chunk length for " + tag);
        if (first && tag != "EEN2") throw std::runtime_error("ENT: missing EEN2");
        first = false;
        BinReader s = r.sub(len - 12);
        if (tag == "EEN2") {
            t.model = s.lpstr();
            // icon strings (0..2), then a few flag bytes, then the nested EEOV chunk
            while (s.remaining() >= 2) {
                const uint8_t* p = s.ptr(); uint16_t l = (uint16_t)(p[0] | (p[1] << 8));
                if (l > 0 && l < 200 && s.remaining() >= 2u + l && printable(p + 2, l) && !(l == 4 && memcmp(p + 2, "EEOV", 4) == 0)) t.icons.push_back(s.lpstr());
                else break;
            }
            while (s.remaining() >= 4 && memcmp(s.ptr(), "EEOV", 4) != 0) s.skip(1);
            { const uint8_t* q = s.ptr(); size_t n = s.remaining(); for (size_t i = 0; i + 8 < n; i++) if (q[i + 2] == 'F' && q[i + 3] == 'X' && q[i + 4] == '_') { uint16_t l = (uint16_t)(q[i] | (q[i + 1] << 8)); if (l > 3 && l < 100 && i + 2 + l <= n && printable(q + i + 2, l)) { t.heldEffect.assign((const char*)q + i + 2, l); break; } } }
            if (s.remaining() < 12) throw std::runtime_error("ENT: EEN2 without EEOV");
            s.tag(); uint32_t v2 = s.u32(), l2 = s.u32();
            if (l2 < 12) throw std::runtime_error("ENT: bad EEOV");
            BinReader o = s.sub(l2 - 12);
            readEeov(o, v2, t.defaults);
        } else if (tag == "GENT" && s.remaining() >= 32) {
            uint32_t w[8]; for (auto& x : w) x = s.u32();
            t.nameStr = (int)w[0]; t.descStr = (int)w[1]; t.maxHp = (int)w[6]; t.hp = (int)w[7];       // [6] = maximum, [7] = current hit points (the broken eye bot: 10 / 1; most others have both equal)
        } else if (tag == "GCRE") {
            if (s.remaining() >= 60) {
                BinReader q = s; uint32_t w[15]; for (auto& x : w) x = q.u32();
                for (int i = 0; i < 7; i++) t.special[i] = (int)w[i];
                t.race = (int)w[10]; t.age = (int)w[11]; t.gender = (int)w[12]; t.team = (int)w[13]; t.hasStats = true;
            }
            // length-prefixed strings in file order: portrait .dds, then the worn parts (name, optionally followed by its texture .dds) and the armor file
            for (const std::string& str : scanStrings(s)) {
                std::string l = lower(str);
                bool dds = l.size() > 4 && l.compare(l.size() - 4, 4, ".dds") == 0;
                if (l.size() > 4 && l.compare(l.size() - 4, 4, ".arm") == 0) t.armor = str;
                else if (dds) { if (l.compare(0, 4, "port") == 0 && t.portrait.empty()) t.portrait = str; else if (!t.wear.empty() && t.wear.back().second.empty()) t.wear.back().second = str; }
                else if (l.find('.') == std::string::npos) t.wear.emplace_back(str, std::string());
            }
        } else if (tag == "GIWP" && s.remaining() >= 12) {
            const uint8_t* b = s.ptr(); size_t n = s.remaining(); auto i32 = [&](size_t off) { int32_t v = 0; if (off + 4 <= n) std::memcpy(&v, b + off, 4); return v; };
            t.wClass = i32(0); t.wClip = i32(8);
            for (size_t i = 0; i + 16 <= n; i++) if (std::memcmp(b + i, "GWAM", 4) == 0) {         // attack mode blocks: damage at word 6, range (16.16 fixed) at word 12, caption string id at word 15, then the effect file (u16 length + name)
                size_t body = i + 12; EntityTemplate::AttackMode m;
                m.kind = i32(body); m.dmgType = i32(body + 4); m.shots = i32(body + 8); m.damage = i32(body + 6 * 4); m.range = (float)i32(body + 12 * 4) / 65536.f; m.nameStr = i32(body + 15 * 4);
                if (body + 66 <= n) { uint16_t l = (uint16_t)(b[body + 64] | (b[body + 65] << 8)); if (l > 3 && l < 100 && body + 66 + l <= n) { std::string e((const char*)b + body + 66, l); if (printable((const uint8_t*)e.data(), e.size())) m.effect = e; } }
                if (t.modes.empty()) { t.wDamage = m.damage; t.wRange = m.range; }
                t.modes.push_back(m);
            }
        } else if (tag == "GITM" && t.kind == "weapon") {            // the held model's name is the first string that is not a flag: "WP_9mmMachinePistol"
            for (const std::string& str : scanStrings(s)) if (str.find('.') == std::string::npos && str.rfind("WP_", 0) == 0) { t.heldModel = str; break; }
        } else if (tag == "GIAR" && s.remaining() >= 4) {
            BinReader q = s; t.armorRating = (int)q.u32();
        } else if (tag == "GITM" && t.kind == "armor") {
            for (const std::string& str : scanStrings(s)) if (str.find('.') == std::string::npos) t.parts.push_back(str);
        }
        // GENT, GOBJ, GDOR, GIAR, GWEA, ...: not needed to draw
    }
    return t;
}

EntityLibrary::EntityLibrary(const std::string& assetsRoot) : root_(assetsRoot) {
    for (const std::string& p : vfs::walk(assetsRoot)) {
        fs::path fp(p); std::string ext = lower(fp.extension().string());
        if (ext == ".g3d") g3d_.emplace(lower(fp.stem().string()), p);
        else if (*kindForExt(ext)) templates_.emplace(lower(fp.stem().string()) + ext, p);
    }
}

std::string EntityLibrary::g3dPath(const std::string& name) const {
    auto it = g3d_.find(lower(name)); return it == g3d_.end() ? "" : it->second;
}

const EntityTemplate* EntityLibrary::tmpl(const std::string& entityFile) {
    std::string key = lower(entityFile);
    static const std::map<std::string, std::string> ext = {{".crt", ".critter"}, {".dor", ".door"}, {".itm", ".item"}, {".wea", ".weapon"},
        {".arm", ".armor"}, {".amo", ".ammo"}, {".con", ".con"}, {".use", ".use"}};
    size_t dot = key.rfind('.');
    if (dot != std::string::npos) { auto it = ext.find(key.substr(dot)); if (it != ext.end()) key = key.substr(0, dot) + it->second; }
    auto c = cache_.find(key);
    if (c != cache_.end()) return c->second.get();
    std::unique_ptr<EntityTemplate> t;
    auto it = templates_.find(key);
    if (it != templates_.end()) { try { t = std::make_unique<EntityTemplate>(loadEntityTemplate(it->second)); } catch (...) {} }
    return (cache_[key] = std::move(t)).get();
}

std::vector<ModelPart> EntityLibrary::assemble(const std::string& model, const std::string& skin, const std::vector<std::pair<std::string, std::string>>& wear, const std::string& armorFile) {
    std::vector<ModelPart> out;
    std::string base = g3dPath(model);
    if (base.empty()) return out;
    auto real = [](const std::string& n) { return !n.empty() && n[0] != '<'; };          // "<No Text...>" is the editor's empty placeholder
    out.push_back({base, real(skin) ? skin : std::string(), {}});
    std::vector<std::string> cloth;      // clothing meshes (their codes cover body skin); hair/beard/eyes/pipboy do not
    auto isCloth = [](const std::string& part) { std::string c = part.substr(0, 3); return c == "Bod" || c == "Fee" || c == "Han" || c == "Bac" || c == "Sho"; };
    for (const auto& w : wear) { std::string p = g3dPath(model + "_" + w.first); if (!p.empty()) { out.push_back({p, real(w.second) ? w.second : std::string(), {}}); if (isCloth(w.first)) cloth.push_back(p); } }
    if (!armorFile.empty())
        if (const EntityTemplate* a = tmpl(armorFile))
            for (const std::string& part : a->parts) { std::string p = g3dPath(model + "_" + part); if (!p.empty()) { out.push_back({p, "", {}}); if (isCloth(part)) cloth.push_back(p); } }
    // hide the bare-body groups that the worn parts cover (cached per body + outfit)
    std::string key = base; for (const std::string& c : cloth) key += "|" + c;
    auto hc = hideCache_.find(key);
    if (hc == hideCache_.end()) {
        std::vector<std::string> hide;
        try {
            Mesh bm = parseG3D(base); std::vector<Mesh> ov; std::vector<const Mesh*> ptrs;
            for (const std::string& c : cloth) ov.push_back(parseG3D(c));
            for (const Mesh& m : ov) ptrs.push_back(&m);
            if (!ptrs.empty()) hide = coveredGroups(bm, ptrs);
        } catch (...) {}
        hc = hideCache_.emplace(key, std::move(hide)).first;
    }
    out[0].hideGroups = hc->second;
    return out;
}

std::vector<ModelPart> EntityLibrary::assemblePlayer(const std::string& file, const std::string& bodyArmor, const std::string& headArmor) {
    const EntityTemplate* t = tmpl(file); if (!t || t->model.empty()) return {};
    auto real = [](const std::string& n) { return !n.empty() && n[0] != '<'; };
    std::string skin = real(t->defaults.skin) ? t->defaults.skin : std::string();
    auto pre = [](const std::string& s, const char* p) { return s.compare(0, 3, p) == 0; };
    std::vector<std::pair<std::string, std::string>> wear = t->wear;
    const EntityTemplate* body = bodyArmor.empty() ? nullptr : tmpl(bodyArmor); const EntityTemplate* head = headArmor.empty() ? nullptr : tmpl(headArmor);
    std::string armorFile = t->armor;
    if (body) {                                            // a worn outfit replaces the template's clothes (and the back piece), keeping hair / face / pip-boy
        armorFile.clear();
        wear.erase(std::remove_if(wear.begin(), wear.end(), [&](const auto& w) { return pre(w.first, "Bod") || pre(w.first, "Fee") || pre(w.first, "Han") || pre(w.first, "Bac") || pre(w.first, "Sho"); }), wear.end());
        for (const std::string& part : body->parts) if (!(head && pre(part, "Hea"))) wear.emplace_back(part, std::string());
    }
    bool hat = head != nullptr; if (body) for (const std::string& part : body->parts) if (pre(part, "Hea")) hat = true;
    if (head) for (const std::string& part : head->parts) wear.emplace_back(part, std::string());
    if (hat) wear.erase(std::remove_if(wear.begin(), wear.end(), [&](const auto& w) { return pre(w.first, "Hai"); }), wear.end());     // a helmet covers the hair
    return assemble(t->model, skin, wear, armorFile);
}

std::vector<ModelPart> EntityLibrary::parts(const MapEntity& e) {
    const EntityTemplate* t = tmpl(e.file);
    if (!t || t->model.empty()) return {};
    auto real = [](const std::string& n) { return !n.empty() && n[0] != '<'; };
    std::string skin = real(e.skin) ? e.skin : (real(t->defaults.skin) ? t->defaults.skin : std::string());
    const bool human = t->kind == "critter" && t->model.size() == 7 && t->model.compare(0, 3, "Hum") == 0;
    if (human) return assemble(t->model, skin, t->wear, t->armor);
    std::string base = g3dPath(t->model);
    if (base.empty()) return {};
    return {{base, skin, {}}};
}

} // namespace vb

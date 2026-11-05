#include "assets/vfs.h"
#include "vfx.h"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace vb {
namespace {
const std::string kEmpty;
bool readProp(const std::vector<uint8_t>& d, size_t& p, std::string& name, std::string& type, std::vector<uint8_t>& val) {
    auto u16 = [&](size_t o) { return (size_t)(d[o] | (d[o + 1] << 8)); };
    if (p + 2 > d.size()) return false; size_t nl = u16(p), q = p + 2; if (nl == 0 || nl > 80 || q + nl + 2 > d.size()) return false;
    name.assign((const char*)&d[q], nl); q += nl; size_t tl = u16(q); q += 2; if (tl == 0 || tl > 80 || q + tl + 8 > d.size()) return false;
    type.assign((const char*)&d[q], tl); q += tl; int32_t m; uint32_t sz; std::memcpy(&m, &d[q], 4); std::memcpy(&sz, &d[q + 4], 4); q += 8;
    if (m != -1 || q + sz > d.size()) return false; val.assign(d.begin() + (long)q, d.begin() + (long)(q + sz)); p = q + sz; return true;
}
std::string strOf(const std::vector<uint8_t>& v) { if (v.size() < 2) return std::string(); size_t l = (size_t)(v[0] | (v[1] << 8)); return 2 + l <= v.size() ? std::string((const char*)&v[2], l) : std::string(); }
}

float VfxEffect::f(const std::string& k, float def) const { auto it = props.find(k); if (it == props.end() || it->second.second.size() != 4) return def; float v; std::memcpy(&v, it->second.second.data(), 4); return v; }
int VfxEffect::i(const std::string& k, int def) const {
    auto it = props.find(k); if (it == props.end()) return def; const auto& v = it->second.second;
    if (v.size() == 4) { int32_t x; std::memcpy(&x, v.data(), 4); return x; } if (v.size() == 1) return v[0]; return def;
}
bool VfxEffect::b(const std::string& k, bool def) const { auto it = props.find(k); return it == props.end() || it->second.second.empty() ? def : it->second.second[0] != 0; }
std::string VfxEffect::s(const std::string& k) const { auto it = props.find(k); return it == props.end() ? kEmpty : strOf(it->second.second); }
std::array<float, 3> VfxEffect::v3(const std::string& k) const { std::array<float, 3> o = {0, 0, 0}; auto it = props.find(k); if (it != props.end() && it->second.second.size() >= 12) std::memcpy(o.data(), it->second.second.data(), 12); return o; }
std::array<float, 3> VfxEffect::rgb(const std::string& k) const { std::array<float, 3> o = {1, 1, 1}; auto it = props.find(k); if (it != props.end() && it->second.second.size() >= 3) for (int c = 0; c < 3; c++) o[(size_t)c] = it->second.second[(size_t)c] / 255.f; return o; }
std::array<float, 4> VfxEffect::uv(const std::string& k) const { std::array<float, 4> o = {0, 0, 1, 1}; auto it = props.find(k); if (it != props.end() && it->second.second.size() >= 16) std::memcpy(o.data(), it->second.second.data(), 16); return o; }
std::string VfxEffect::res(const std::string& k) const {
    auto it = props.find(k); if (it == props.end()) return kEmpty; const auto& v = it->second.second; if (v.size() < 4) return kEmpty;
    size_t l1 = (size_t)(v[0] | (v[1] << 8)); if (2 + l1 + 2 > v.size()) return kEmpty; size_t l2 = (size_t)(v[2 + l1] | (v[3 + l1] << 8)); if (4 + l1 + l2 > v.size()) return kEmpty;
    return std::string((const char*)&v[4 + l1], l2);
}

VfxFile loadVfx(const std::string& path) {
    vfs::Stream in(path); if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 0x18 || std::memcmp(d.data(), "VEG V1.1", 8) != 0) throw std::runtime_error("not a VEG V1.1 file: " + path);
    VfxFile f; f.path = path; uint32_t cnt; std::memcpy(&cnt, &d[8], 4); size_t p = 0x18;
    for (uint32_t e = 0; e < cnt; e++) {
        if (p + 20 > d.size() || std::memcmp(&d[p], "VFX V1.0", 8) != 0) throw std::runtime_error("expected an effect block: " + path);
        uint32_t n; std::memcpy(&n, &d[p + 16], 4); p += 20; VfxEffect fx; std::string nm, ty; std::vector<uint8_t> val;
        for (uint32_t k = 0; k < n; k++) { if (!readProp(d, p, nm, ty, val)) throw std::runtime_error("bad property: " + path); fx.props[nm] = {ty, val}; }
        std::string et = fx.s("EffectType"); const std::string pre = "VFXEFFECTTYPE_"; if (et.rfind(pre, 0) == 0) et = et.substr(pre.size());
        for (auto& c : et) c = (char)std::tolower((unsigned char)c); fx.type = et; f.effects.push_back(std::move(fx));
    }
    uint32_t m = 0; if (p + 4 <= d.size()) { std::memcpy(&m, &d[p], 4); p += 4; }
    std::string nm, ty; std::vector<uint8_t> val;
    for (uint32_t k = 0; k < m; k++) {
        if (!readProp(d, p, nm, ty, val)) throw std::runtime_error("bad group property: " + path);
        float v = val.size() == 4 ? *(const float*)val.data() : 0.f;
        if (nm == "Loop") f.loop = !val.empty() && val[0]; else if (nm == "MinStartTime") f.minStart = v; else if (nm == "MaxStartTime") f.maxStart = v;
    }
    return f;
}

} // namespace vb

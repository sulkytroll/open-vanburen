#include "assets/rht.h"
#include <fstream>
#include <stdexcept>
#include <set>
#include <unordered_map>

namespace vb {

namespace {
// Split a null-terminated string pool into (offset, string) pairs.
struct StrOff { int32_t off; std::string text; };
std::vector<StrOff> splitPool(const uint8_t* p, size_t n) {
    std::vector<StrOff> out;
    int32_t start = 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != 0) continue;
        out.push_back({ start, std::string(reinterpret_cast<const char*>(p) + start,
                                            i - start) });
        start = static_cast<int32_t>(i + 1);
    }
    return out;
}
} // namespace

Rht Rht::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (buf.size() < 20) throw std::runtime_error("rht too small: " + path);

    Rht r;
    auto rd32 = [&](size_t i) {
        return (int32_t)(buf[i] | buf[i+1] << 8 | buf[i+2] << 16 |
                         (uint32_t)buf[i+3] << 24);
    };
    r.major_       = rd32(0);
    r.minor_       = rd32(4);
    int32_t num    = rd32(8);
    int32_t offG   = rd32(12);
    int32_t offR   = rd32(16);

    if (num < 0 || (size_t)(20 + num * 20) > buf.size())
        throw std::runtime_error("rht entry table out of range: " + path);

    r.entries_.reserve(num);
    for (int32_t i = 0; i < num; ++i) {
        size_t b = 20 + i * 20;
        RhtEntry e;
        e.version      = rd32(b);
        e.index        = rd32(b + 4);
        e.type         = rd32(b + 8);
        e.nameOff      = rd32(b + 12);
        e.groupNameOff = rd32(b + 16);
        r.entries_.push_back(std::move(e));
    }

    auto gpool = splitPool(buf.data() + offG, (size_t)(offR - offG));
    auto rpool = splitPool(buf.data() + offR, buf.size() - offR);

    std::unordered_map<int32_t, const std::string*> gmap, rmap;             // pool offset -> string (a linear search per entry was quadratic)
    for (auto& s : gpool) gmap.emplace(s.off, &s.text);
    for (auto& s : rpool) rmap.emplace(s.off, &s.text);
    auto lookup = [](const std::unordered_map<int32_t, const std::string*>& pool, int32_t off) -> std::string {
        auto it = pool.find(off); return it == pool.end() ? std::string() : *it->second;
    };
    std::set<std::string> gseen;
    for (auto& e : r.entries_) {
        e.group = lookup(gmap, e.groupNameOff);
        e.name  = lookup(rmap, e.nameOff);
        if (!e.group.empty() && gseen.insert(e.group).second)
            r.groups_.push_back(e.group);
    }
    return r;
}

} // namespace vb

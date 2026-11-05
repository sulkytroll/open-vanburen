#include "stf.h"
#include "binreader.h"
#include <cstring>

namespace vb {

bool StringTable::load(const std::string& path, std::string& err) {
    std::vector<uint8_t> d;
    try { d = readFileBytes(path); } catch (std::exception& e) { err = e.what(); return false; }
    if (d.size() < 12) { err = "STF: too small"; return false; }
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, &d[o], 4); return v; };
    uint32_t ver = u32(0), count = u32(8);
    if (ver != 3 || (size_t)12 + (size_t)count * 16 > d.size()) { err = "STF: unsupported header"; return false; }
    strings_.clear(); strings_.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        size_t r = 12 + 16 * (size_t)i; uint32_t off = u32(r), len = u32(r + 4);
        if ((size_t)off + len > d.size()) { err = "STF: record out of range"; return false; }
        strings_.emplace_back((const char*)&d[off], len);
    }
    return true;
}

} // namespace vb

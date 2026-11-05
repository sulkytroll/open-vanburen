#include "assets/grp.h"
#include <fstream>
#include <stdexcept>

namespace vb {

Grp Grp::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    Grp g;
    g.path_ = path;
    g.data_.assign((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
    if (g.data_.size() < 12) throw std::runtime_error("grp too small: " + path);

    auto rd32 = [&](size_t i) {
        return (int32_t)(g.data_[i] | g.data_[i+1] << 8 | g.data_[i+2] << 16 |
                         (uint32_t)g.data_[i+3] << 24);
    };
    g.major_ = rd32(0);
    g.minor_ = rd32(4);
    int32_t n = rd32(8);
    if (n < 0 || 12 + (size_t)n * 8 > g.data_.size())
        throw std::runtime_error("grp table out of range: " + path);
    g.lumps_.reserve(n);
    for (int32_t i = 0; i < n; ++i) {
        size_t b = 12 + (size_t)i * 8;
        GrpLump L;
        L.offset = rd32(b);
        L.length = rd32(b + 4);
        g.lumps_.push_back(L);
    }
    return g;
}

bool Grp::read(int i, std::vector<uint8_t>& out) const {
    if (i < 0 || i >= (int)lumps_.size()) return false;
    const auto& L = lumps_[i];
    if (L.offset < 0 || L.length < 0 ||
        (size_t)(L.offset + L.length) > data_.size())
        return false;
    out.assign(data_.begin() + L.offset, data_.begin() + L.offset + L.length);
    return true;
}

} // namespace vb

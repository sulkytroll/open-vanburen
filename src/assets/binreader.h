#pragma once
#include "assets/vfs.h"
// Small bounds-checked little-endian reader shared by the map-format parsers.
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace vb {

class BinReader {
public:
    BinReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    size_t pos() const { return o_; }
    size_t size() const { return n_; }
    size_t remaining() const { return n_ - o_; }
    bool done() const { return o_ == n_; }
    void seek(size_t o) { if (o > n_) fail("seek"); o_ = o; }
    void skip(size_t k) { need(k); o_ += k; }
    const uint8_t* ptr() const { return p_ + o_; }

    uint8_t u8() { need(1); return p_[o_++]; }
    uint32_t u32() { need(4); uint32_t v; memcpy(&v, p_ + o_, 4); o_ += 4; return v; }
    float f32() { need(4); float v; memcpy(&v, p_ + o_, 4); o_ += 4; return v; }
    void f32n(float* out, int n) { for (int i = 0; i < n; i++) out[i] = f32(); }
    std::string tag() { need(4); std::string s((const char*)p_ + o_, 4); o_ += 4; return s; }
    // u16 length-prefixed string
    std::string lpstr() {
        need(2); uint16_t l; memcpy(&l, p_ + o_, 2); o_ += 2;
        need(l); std::string s((const char*)p_ + o_, l); o_ += l; return s;
    }
    // sub-reader over the next `len` bytes (and advance past them)
    BinReader sub(size_t len) { need(len); BinReader r(p_ + o_, len); o_ += len; return r; }

    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("binary read error: ") + what + " at " + std::to_string(o_));
    }
private:
    void need(size_t k) const { if (k > n_ - o_) fail("out of data"); }
    const uint8_t* p_; size_t n_; size_t o_ = 0;
};

inline std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::vector<uint8_t> d;
    if (!vfs::readBytes(path, d)) throw std::runtime_error("cannot open " + path);      // (a packaged resource or an ordinary file)
    return d;
}

} // namespace vb

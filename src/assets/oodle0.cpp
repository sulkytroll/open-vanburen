#include "oodle0.h"
#include <cstring>
#include <memory>
#include <vector>

namespace vb {
namespace {

// ---- adaptive frequency model (granny2.dll FUN_1000e0c0 / e130 / e1c0 / e6f0 / e7f0 / ec30 / e690) -------------------------------
// State is kept in the original u16 layout: w[0..15] cumulative bucket sums (w[15] = total), w[0x10] = symbols seen, w[0x12] = 15*bucket,
// w[0x13] = bucket shift, w[0x14] = bucket size, w[0x16] = max symbols, w[0x1c + i] = frequency of index i (index 0 = escape).
struct Model {
    std::vector<uint16_t> w;       // 0x1c header words + (n + 5 & ~3) + slack frequency words
    std::vector<uint16_t> map;     // index -> symbol
    explicit Model(int n) : w(0x1c + ((n + 5) & ~3) + 8, 0), map(((n + 5) & ~3) + 8, 0) {
        w[0x16] = (uint16_t)n;
        setupBuckets(n + 1);
        update(0, 0x30003);
    }
    uint32_t get32(int k) const { return (uint32_t)w[2 * k] | ((uint32_t)w[2 * k + 1] << 16); }
    void add32(int k, uint32_t d) { uint32_t v = get32(k) + d; w[2 * k] = (uint16_t)v; w[2 * k + 1] = (uint16_t)(v >> 16); }

    void setupBuckets(uint32_t count) {                       // FUN_1000e130
        if (count < 6) { w[0x14] = 0; w[0x13] = 0xf; w[0x12] = 0; return; }
        uint32_t best = 0xffffffffu, bestShift = 0;
        for (uint32_t sh = 0; sh < 0x10; sh++) {
            uint32_t b = 1u << sh;
            uint32_t k = (b - 1 + count) / b; if (k > 0x10) k = 0x10;
            uint32_t v = count - (k - 1) * b; if (v < b) v = b;
            if (v < best) { bestShift = sh; best = v; }
            if (!(b <= count)) break;
        }
        uint16_t b = (uint16_t)(1u << bestShift);
        w[0x14] = b; w[0x13] = (uint16_t)bestShift; w[0x12] = (uint16_t)(b * 0xf);
    }
    void update(uint32_t index, uint32_t delta) {            // FUN_1000e1c0
        uint16_t s = (uint16_t)delta;
        if (w[0x12] <= index) { w[0xf] += s; w[0x1c + index] += s; return; }
        uint32_t j = index >> w[0x13];
        if (j & 1) { w[j] += s; j++; }
        for (uint32_t k = j >> 1; k < 8; k++) add32((int)k, delta);
        w[0x1c + index] += s;
    }
    int bucketOf(uint32_t i) const { return i < w[0x12] ? (int)(i >> w[0x13]) : 15; }
    void rebuild(uint32_t* sums) {                           // FUN_1000e690
        if (w[0x10] != w[0x16] && w[0x1c] == 0) { w[0x1c] = 2; sums[w[0x12] != 0 ? 0 : 15] += 2; }
        w[0] = (uint16_t)sums[0];
        for (int i = 1; i < 16; i++) w[i] = (uint16_t)(w[i - 1] + (uint16_t)sums[i]);
    }
    void rescale() {                                         // FUN_1000ec30
        uint32_t sums[16] = {0};
        uint32_t maxf = 0, best = 0;
        setupBuckets((uint32_t)w[0x10] + 1);
        w[0x1c] >>= 1;
        sums[w[0x12] != 0 ? 0 : 15] = w[0x1c];
        if (w[0x10] != 0) {
            uint32_t i = 1;
            do {
                uint16_t f = w[0x1c + i];
                bool leave = false;
                while (f < 2) {
                    if (w[0x10] <= i) { w[0x1c + i] = 0; w[0x10]--; leave = true; break; }
                    w[0x1c + i] = w[0x1c + w[0x10]];
                    w[0x1c + w[0x10]] = 0;
                    map[i] = map[w[0x10]];
                    w[0x10]--;
                    f = w[0x1c + i];
                }
                if (leave) break;
                w[0x1c + i] >>= 1;
                uint32_t v = w[0x1c + i];
                if (maxf < v) { best = i; maxf = v; }
                sums[bucketOf(i)] += v;
                i++;
            } while (i <= w[0x10]);
        }
        if (maxf != 0) {
            uint32_t target = w[0x10] < w[0x12] ? (uint32_t)((w[0x10] >> w[0x13]) << w[0x13]) : w[0x12];
            if (target == 0) target = 1;
            if (best != target) {
                uint16_t fb = w[0x1c + best], ft = w[0x1c + target];
                w[0x1c + target] = fb;
                sums[bucketOf(target)] += (uint32_t)fb - ft;
                sums[bucketOf(best)] += (uint32_t)ft - w[0x1c + target];
                w[0x1c + best] = ft;
                std::swap(map[target], map[best]);
            }
        }
        rebuild(sums);
    }
    // FUN_1000e7f0: find the index whose cumulative interval contains t, bump the cumulative counters, return cumlow.
    uint32_t find(uint32_t t, uint32_t* cumlow) {
        uint16_t tt = (uint16_t)t;
        int b = 15;
        for (int k = 0; k < 15; k++) if (tt < w[k]) { b = k; break; }
        uint32_t base = b == 0 ? 0 : w[b - 1];
        uint32_t idx = b == 15 ? w[0x12] : ((uint32_t)b << w[0x13]);
        for (int k = b; k < 16; k++) w[k] += 1;
        uint32_t cum = base;
        for (;; idx++) {
            uint32_t next = cum + w[0x1c + idx];
            if (t < next) { *cumlow = cum; return idx; }
            cum = next;
        }
    }
};

// ---- arithmetic decoder (FUN_1000ddc0 / e020) -----------------------------------------------------------------------------------
struct RC {
    const uint8_t* p; size_t n; size_t bit = 0;
    uint32_t hi = 0x7fffffff, lo = 0, code = 0;
    uint32_t getbit() { size_t byte = bit >> 3; uint32_t b = byte < n ? (p[byte] >> (bit & 7)) & 1 : 0; bit++; return b; }
    void decodeUpdate(uint32_t low, uint32_t freq, uint32_t total) {
        uint64_t range = (uint64_t)hi - lo + 1;
        uint32_t nhi = lo + (uint32_t)(range * (low + freq) / total) - 1;
        uint32_t nlo = lo + (uint32_t)(range * low / total);
        uint32_t c = code;
        while (((nlo ^ nhi) & 0x40000000) == 0) { nlo <<= 1; nhi = nhi * 2 | 1; c = c * 2 | getbit(); }
        while ((nlo & 0x20000000) && !(nhi & 0x20000000)) {
            nlo = (nlo & 0x1fffffff) << 1; nhi = nhi * 2 | 0x40000001; c = (c ^ 0x20000000) * 2 | getbit();
        }
        lo = nlo & 0x7fffffff; hi = nhi & 0x7fffffff; code = c & 0x7fffffff;
    }
    uint32_t uniform(uint32_t n) {                           // FUN_1000e020
        uint64_t range = (uint64_t)hi - lo + 1;
        uint32_t t = (uint32_t)(((uint64_t)code - lo + 1) * n - 1) ;
        t = (uint32_t)((((uint64_t)(code - lo) + 1) * n - 1) / range);
        decodeUpdate(t, 1, n);
        return t;
    }
    // FUN_1000e6f0: returns the symbol, or sets *escape and returns the map slot to fill with the new symbol.
    uint32_t symbol(Model& m, bool* escape, uint32_t* slot) {
        if (m.w[0xf] > 0x3fff) m.rescale();
        uint64_t range = (uint64_t)hi - lo + 1;
        uint32_t total = m.w[0xf];
        uint32_t t = (uint32_t)((((uint64_t)(code - lo) + 1) * total - 1) / range);
        uint32_t cumlow;
        uint32_t idx = m.find(t, &cumlow);
        decodeUpdate(cumlow, m.w[0x1c + idx], (uint32_t)m.w[0xf] - 1);
        m.w[0x1c + idx] += 1;
        if (idx == 0) {
            m.w[0x10]++;
            m.update(m.w[0x10], 0x20002);
            if (m.w[0x10] == m.w[0x16]) { uint32_t f = m.w[0x1c]; m.update(0, ((0u - f - 1) << 16) | ((0u - f) & 0xffff)); }
            *escape = true; *slot = m.w[0x10];
            return 0;
        }
        *escape = false;
        return m.map[idx];
    }
};

} // namespace

bool oodle0Decompress(const uint8_t* comp, size_t compSize, uint32_t stop0, uint32_t stop1, uint32_t stop2, uint8_t* out) {
    if (compSize < 0x28) return false;
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, comp + o, 4); return v; };
    RC rc; rc.p = comp + 0x24; rc.n = compSize - 0x24;
    {   // initial code: the first 31 stream bits, first bit becomes the most significant
        uint32_t v = u32(0x24) & 0x7fffffff, c = 0;
        for (int i = 0; i < 31; i++) c = (c << 1) | ((v >> i) & 1);
        rc.code = c; rc.bit = 31;
    }
    const uint32_t stops[3] = {stop0, stop1, stop2};
    uint32_t produced = 0; uint8_t* o = out;
    for (int seg = 0; seg < 3; seg++) {
        uint32_t p0 = u32(seg * 12), p1 = u32(seg * 12 + 4), p2 = u32(seg * 12 + 8);
        // FUN_1001ca70: build this segment's model set
        uint32_t litAlpha = p0 & 0x1ff, A = p0 >> 9, B = A > 3 ? 4 : A, C = (A >> 2) + 1;
        Model lit((int)(p1 & 0x1ff));
        std::vector<std::unique_ptr<Model>> lenModels;
        for (int g = 0; g < 4; g++) { int n = (int)((p2 >> (24 - 8 * g)) & 0xff); for (int k = 0; k < 16; k++) lenModels.emplace_back(new Model(n)); }
        lenModels.emplace_back(new Model((int)(p2 & 0xff)));                 // index 64
        Model off1((int)B), off2((int)(p1 >> 9));
        uint32_t last = 0, pos = 0;
        while (produced < stops[seg]) {
            bool esc; uint32_t slot;
            Model& lm = *lenModels[last];
            uint32_t sym = rc.symbol(lm, &esc, &slot);
            if (esc) { sym = rc.uniform(0x41); lm.map[slot] = (uint16_t)sym; }
            last = sym;
            uint32_t n;
            if (sym != 0) {
                static const uint32_t big[4] = {128, 192, 256, 512};
                n = sym < 0x3d ? sym + 1 : big[sym - 0x3d];
                uint32_t lo = rc.symbol(off1, &esc, &slot);
                if (esc) { lo = rc.uniform(B); off1.map[slot] = (uint16_t)lo; }
                uint32_t hi = rc.symbol(off2, &esc, &slot);
                if (esc) { uint32_t lim = A < pos ? A : pos; hi = rc.uniform((lim >> 2) + 1); off2.map[slot] = (uint16_t)hi; }
                uint32_t dist = hi * 4 + lo + 1;
                pos += n;
                if (dist > produced || produced + n > stop2) return false;
                for (uint32_t k = 0; k < n; k++) o[k] = o[(ptrdiff_t)k - (ptrdiff_t)dist];
            } else {
                uint32_t v = rc.symbol(lit, &esc, &slot);
                if (esc) { v = rc.uniform(litAlpha); lit.map[slot] = (uint16_t)v; }
                if (produced + 1 > stop2) return false;
                *o = (uint8_t)v; pos += 1; n = 1;
            }
            o += n; produced += n;
        }
    }
    return true;
}

} // namespace vb

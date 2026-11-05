#include "col.h"
#include "binreader.h"

namespace vb {

Col loadCol(const std::string& path) {
    std::vector<uint8_t> d = readFileBytes(path);
    BinReader r(d.data(), d.size());
    Col c;
    c.width = (int)r.u32(); c.height = (int)r.u32();
    if (c.width <= 0 || c.height <= 0 || (uint64_t)c.width * c.height > (1u << 28))
        throw std::runtime_error("COL: bad dimensions");
    c.cells.resize((size_t)c.width * c.height);
    for (int row = 0; row < c.height; row++) {
        int z = row;                      // file row 0 is the min-Z edge (verified against level geometry)
        int x = 0;
        while (x < c.width) {
            uint32_t run = r.u32(); uint8_t v = r.u8();
            if (run == 0 || (uint64_t)x + run > (uint64_t)c.width) throw std::runtime_error("COL: run overflows row");
            for (uint32_t i = 0; i < run; i++) c.cells[(size_t)z * c.width + x + i] = v;
            x += (int)run;
        }
    }
    if (!r.done()) throw std::runtime_error("COL: trailing bytes");
    return c;
}

} // namespace vb

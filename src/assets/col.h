#pragma once
// Van Buren collision grid (".col", lump type 1700). 2 cells per world unit, origin at the octree root min XZ.
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

struct Col {
    int width = 0, height = 0;
    std::vector<uint8_t> cells;     // cells[z*width + x], z increasing (file rows are stored max-Z first)
    uint8_t at(int x, int z) const { return cells[(size_t)z * width + x]; }
};

Col loadCol(const std::string& path);   // throws std::runtime_error

} // namespace vb

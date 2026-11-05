#pragma once
// Van Buren octree level geometry ("8TRE", lump type 400). Layout: MAP_FORMAT_FINDINGS.md.
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

struct TreMaterial {
    uint32_t shader = 0;        // 0 NO_TEXTURE, 2 BASE_2X, 28 SK_BASE_LM_2X (base x lightmap)
    std::string name;           // shader name
    uint8_t rgba[4] = {127, 127, 127, 255};
    std::string blend;          // OPAQUE / ALPHABLEND / ADD ...
    std::string surface;        // SILENT / SAND / CONCRETE ... (footstep material)
    uint8_t extra[12] = {};     // the last 12 bytes of the record (mesh-group flags, see GAME_DATA_FINDINGS.md)
};

// One draw batch inside a leaf: triangle list over the leaf's vertex buffer.
struct TreBatch {
    uint32_t material = 0;      // index into Tre::materials
    uint32_t lightmap = 0;      // 1-based lightmap number (0 = none); texture slot = first_lightmap + lightmap - 1
    uint32_t layer = 0;         // primary diffuse texture slot (0 = none)
    std::vector<uint16_t> indices;
};

struct TreLeaf {
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};
    uint32_t buffer = 0;        // index into Tre::buffers
    uint32_t flags = 0;
    std::vector<TreBatch> batches;
    std::vector<uint8_t> tri_flags;   // one byte per triangle
};

struct TreVertexBuffer {
    uint32_t count = 0;
    std::vector<float> xyz, nrm, uv, lmuv;   // 3,3,2,2 floats per vertex
    std::vector<uint32_t> diffuse;           // packed vertex colour
};

// Static light from LGTD. type 0 = point, 1 = spot (dir/cone valid). Layout: MAP_FORMAT_FINDINGS.md.
struct TreLight {
    uint32_t type = 0, aux = 0;
    float color[3] = {1, 1, 1}, pos[3] = {0, 0, 0}, dir[3] = {0, 0, 0};   // dir is NaN for point lights
    float intensity = 1, range = 0, falloff = 1, inner = 0, outer = 0;      // cone angles in radians (spot only)
};

// Per-cell list of lights that can see the cell (LVLD), engine OCTREE::GetLightVisibility. Cells are w*h, row 0 at max Z.
struct TreLightGrid {
    uint32_t w = 0, h = 0;
    float sx = 0.5f, sy = 0.5f;
    std::vector<uint8_t> data;               // raw LVL0 payload (pair table + u16 id stream)
    float min_x = 0, max_z = 0;              // set from the octree root bounds
    // Light indices (into Tre::lights) visible from a world position; empty outside the grid or if none.
    std::vector<uint16_t> visibleLights(float x, float z) const;
};

struct Tre {
    uint32_t head[4] = {0, 0, 0, 0};
    std::vector<TreMaterial> materials;
    std::vector<std::string> textures;       // slot i (1-based) -> textures[i-1]; slot 0 = none
    uint32_t first_lightmap = 0;             // slots >= this are baked lightmaps (.ctx -> .tga)
    std::vector<TreVertexBuffer> buffers;
    std::vector<TreLeaf> leaves;
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};   // root bounds
    size_t node_count = 0;
    std::vector<TreLight> lights;            // HEAD flag 4
    bool has_grid = false; TreLightGrid grid;   // HEAD flag 8
};

// Throws std::runtime_error on any structural error.
Tre loadTre(const std::string& path);

} // namespace vb

#pragma once
// G3D (Granny "B3D 1.1") mesh parser — Van Buren implementation.
//
// Parses the proprietary binary format used by Van Buren's Granny2-based
// engine. Extracts meshes with positions, normals, colors, UVs, materials,
// textures, and triangle indices.
//
// Reference:
//   - README.md in project root (format notes)
//   - 2012 fodev.net "Exports from Van Buren" thread (incomplete but useful)
//   - Direct inspection of 3916 .g3d files across the game's .grp archives

#include <cstdint>
#include <string>
#include <vector>

namespace vb {

struct Vertex {
    float pos[3];   // x, z, y (Z/Y swapped in file)
    float norm[3];
    float color[4];
    float uv[2];    // first UV set (s, t)
    float lm[2];    // lightmap UV (may be zeros if nLM==0)
    int16_t bone[4] = {-1, -1, -1, -1};   // skin influences: index into Mesh::bones (-1 = unused)
    float weight[4] = {0, 0, 0, 0};
};

struct Face {
    uint16_t vert[3];  // 0-based indices
    uint16_t mat;      // material group index
};

struct Material {
    int16_t id;
    std::string name;
    std::string shader;       // first material string: "BASE_2X", "BASE_VERTEX_ALPHA_2X" (alpha from the vertex colours, doubled) ...
    float ambient[4];
    float diffuse[4];
    float emissive[4];
    float specular[4];
    float shininess;
    float alpha;
    std::string blend_name;  // "OPAQUE", "ALPHATEST", "ALPHABLEND", "ADD" ... (empty if unknown); texture alpha only means transparency when not OPAQUE
    int16_t blend;     // unused (0)
    int16_t type;      // SILENT etc.
    std::string texture_filename;  // per-group texture (Format B); may be empty
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    std::vector<Material> materials;
    std::vector<std::string> bones;   // the file's bone list (tag 0x0E); Vertex::bone indexes this. Bind to a .skel by bone NAME.
    bool skinned() const { return !bones.empty(); }
    std::string texture_name;
    std::string texture_filename;
    bool has_bbox = false;
    float bbox_min[3] = {0, 0, 0};
    float bbox_max[3] = {0, 0, 0};
    uint32_t nverts() const { return vertices.size(); }
    uint32_t nfaces() const { return faces.size(); }
};

/// Human critter parts (Critters/Hum[MF](Fat|Nor|Str|Wir)_<Part>*.g3d) are UV-mapped into one shared 512x512 character atlas,
/// while each part's .tga holds only its own sub-rectangle (Body 256x256 at (0,.5), Head/Hands/Feet 128x128 in the right-hand
/// column, ...). Returns true and the sub-rectangle (u0,v0,su,sv) when `stem` is such a part; the texture coordinate to sample
/// the part's own .tga is then ((u-u0)/su, (v-v0)/sv). Body is fixed at (0,.5); other parts use the size-aligned cell that
/// contains the mesh's UV minimum (verified on all 16 Bod meshes' islands; small parts -- hair/beard/mustache -- are a heuristic).
struct UVRect { float u0, v0, su, sv; };
bool humanAtlasRect(const std::string& stem, const Mesh& mesh, int texW, int texH, UVRect& out);

/// Names of `base` material groups that are hidden under `overlays` (worn clothing meshes in the same bind pose): a group is covered when at least
/// `fraction` of its vertices lie within `dist` of an overlay vertex. The engine draws the bare body plus clothing parts; without hiding the covered
/// groups the skin pokes through the clothes. (dist/fraction chosen from the leather/vault/prison outfits: covered groups score 98-100%, bare ones 0-44%.)
std::vector<std::string> coveredGroups(const Mesh& base, const std::vector<const Mesh*>& overlays, float dist = 0.15f, float fraction = 0.8f);

/// Parse a G3D file and extract the mesh.
/// @param path File path to parse
/// @throws std::runtime_error on I/O or format errors
/// @return Parsed mesh with vertices, faces, materials, and texture info
Mesh parseG3D(const std::string& path);

} // namespace vb

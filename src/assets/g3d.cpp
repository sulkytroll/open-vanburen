#include "assets/vfs.h"
#include <algorithm>
// G3D (Granny "B3D 1.1") mesh parser -- Van Buren implementation.
//
// Every layout below was ground-truthed against F3.exe's own reader
// (G3D_Mesh::ReadB3D @0x4db810, Gfx_G3D_Mesh::LoadMesh @0x5267e0, decompiled
// with Ghidra) and then verified byte-exact against the shipped corpus with
// verify_all.py (a file passes only if parsing consumes every byte up to the
// fixed 3-byte footer 0b 0b 01). 3912 of 3916 files pass; see
// G3D_FORMAT_FINDINGS.md for the derivation and the 4 known failures.
//
// File layout:
//   "B3D 1.1 ", version (u8 == 0x03 for the "v3" files, else u16), f32 1.0,
//   u8 4, f32 1024.0, u8 5, lpstr "LEFT_HANDED", u8 unk, i32 -1,
//   then a tag stream: material(0x07) texture(0x08) node(0x0A) bone(0x0E)
//   ... mesh(0x0F, terminal), then footer 0b 0b 01.
//
// The mesh tag has two encodings, selected by a flag byte (the engine's
// "converted" flag):
//   * converted (flag==1, header u8,u8,u8): "Format B" -- vertices are one
//     opaque blob (nverts*stride), then bbox, optional skin block, groups
//     with an embedded material, per-group texture names, u16 indices.
//   * raw (flag==0 -- every v3 file, and every Tile): variable-length
//     vertices and groups with u32 indices. v3 files have a 2-byte header
//     (u8,u8=flag), all others a 3-byte one (u8,u8,u8=flag).
//
// A std::string array in the engine's memory (0x1c bytes per element) is
// serialized as `u32 count` followed by `count` length-prefixed strings.

#include "g3d.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <optional>

namespace vb {

// -- Little-endian helpers --------------------------------------------

static uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])       |
         (static_cast<uint32_t>(p[1]) << 8)  |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t read_u16(const uint8_t* p) {
    uint16_t v; memcpy(&v, p, 2); return v;
}

static float read_f32(const uint8_t* p) {
    float f; memcpy(&f, p, 4); return f;
}

// Length-prefixed string: u16 length, then that many bytes.
static bool read_lpstr(const uint8_t* p, const uint8_t* end, std::string& out, const uint8_t*& next) {
    if (p + 2 > end) return false;
    uint16_t n = read_u16(p);
    if (p + 2 + n > end) return false;
    out.assign(reinterpret_cast<const char*>(p + 2), n);
    next = p + 2 + n;
    return true;
}

// -- Internal state -----------------------------------------------------

struct TagState {
    bool v3 = false;                    // file version byte == 0x03 (2-byte raw mesh header)
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    std::vector<Material> outer;        // tag-0x07 material declarations
    std::vector<Material> groups;       // one per mesh group; Face::mat indexes this
    std::string tex_name;
    std::string tex_filename;
    bool has_bbox = false;
    float bbox_min[3] = {0, 0, 0};
    float bbox_max[3] = {0, 0, 0};
    bool parsed_vertex_data = false;
    std::vector<std::string> bones;     // tag-0x0E bone names, in file order
};

// -- Material tag (0x07) -------------------------------------------------
// lpstr name1, lpstr name2 (the key groups refer to), 18 floats (Ambient4
// Diffuse4 Emissive4 Specular4 Shininess Alpha), lpstr blend, lpstr type,
// then an optional-properties trailer: u32 mask, then one or more f64 per set
// bit (values per bit 0..5 = 1,1,2,2,3,4), then a u32 (observed 0 or 2).
static bool parse_material_body(const uint8_t*& p, const uint8_t* end, Material& mat) {
    const uint8_t* next;
    std::string name1, name2;
    if (!read_lpstr(p, end, name1, next)) return false; p = next;
    if (!read_lpstr(p, end, name2, next)) return false; p = next;
    if (p + 18 * 4 > end) return false;
    float vals[18];
    for (int i = 0; i < 18; i++) { vals[i] = read_f32(p); p += 4; }
    memcpy(mat.ambient, vals, sizeof(float) * 4);
    memcpy(mat.diffuse, vals + 4, sizeof(float) * 4);
    memcpy(mat.emissive, vals + 8, sizeof(float) * 4);
    memcpy(mat.specular, vals + 12, sizeof(float) * 4);
    mat.shininess = vals[16];
    mat.alpha = vals[17];
    mat.id = 0;
    mat.name = name2;           // the key mesh groups use to find this material
    mat.shader = name1;
    std::string blend, mtype;
    if (!read_lpstr(p, end, blend, next)) return false; p = next;
    if (!read_lpstr(p, end, mtype, next)) return false; p = next;
    mat.blend = 0; mat.type = 0; mat.blend_name = blend;

    if (p + 4 > end) return false;
    uint32_t mask = read_u32(p); p += 4;
    static const int cnt[6] = {1, 1, 2, 2, 3, 4};
    for (int b = 0; b < 6; b++) {
        if (mask & (1u << b)) {
            if (p + 8 * cnt[b] > end) return false;
            p += 8 * cnt[b];
        }
    }
    if (p + 4 > end) return false;
    p += 4;
    return true;
}

// -- Raw mesh body (flag==0): all v3 files and all Tiles -----------------
// vertex: f32 pos[3], f32 norm[3], f32 color[4], u32 nUV, nUV * (f32 u, f32 v)
//         [channel 0 = texture UV, channel 1 = lightmap UV],
//         u32 nW, nW * (u32 bone, f32 weight)
// u32 ngroups; per group: u32 index, lpstr matkey, u32 ntex, ntex * lpstr,
//         u32 unk, u8 marker (0xC9 == triangle list), u32 kk,
//         kk * u32 index (0-based), kk * u8 flag
static bool try_raw(const uint8_t* p, const uint8_t* end, TagState& ts) {
    const uint8_t* next;
    uint32_t nverts = read_u32(p); p += 4;
    if (nverts == 0 || nverts > 1000000) return false;
    ts.vertices.reserve(nverts);
    for (uint32_t i = 0; i < nverts; i++) {
        if (p + 44 > end) return false;
        Vertex v;
        // Raw meshes are stored at 1/10 of world scale (LoadMesh multiplies by a scale argument of 10: a 10x10 tile is 1.0 wide, the 6-unit-tall
        // human skeleton is 0.6, Vault door 0.62 vs a 6-unit opening). Tiles (non-v3 raw) are Z-up and get Y/Z swapped; v3 props are already Y-up.
        const float kRawScale = 10.f;
        const float fx = read_f32(p + 0), fy = read_f32(p + 4), fz = read_f32(p + 8);
        const float nx = read_f32(p + 12), ny = read_f32(p + 16), nz = read_f32(p + 20);
        v.pos[0] = fx * kRawScale;
        if (ts.v3) { v.pos[1] = fy * kRawScale; v.pos[2] = fz * kRawScale; v.norm[0] = nx; v.norm[1] = ny; v.norm[2] = nz; }
        else       { v.pos[1] = fz * kRawScale; v.pos[2] = fy * kRawScale; v.norm[0] = nx; v.norm[1] = nz; v.norm[2] = ny; }
        for (int j = 0; j < 4; j++) v.color[j] = read_f32(p + 24 + j * 4);
        uint32_t nUV = read_u32(p + 40); p += 44;
        if (nUV > 16 || p + static_cast<size_t>(nUV) * 8 + 4 > end) return false;
        v.uv[0] = v.uv[1] = v.lm[0] = v.lm[1] = 0.0f;
        if (nUV >= 1) { v.uv[0] = read_f32(p); v.uv[1] = read_f32(p + 4); }
        if (nUV >= 2) { v.lm[0] = read_f32(p + 8); v.lm[1] = read_f32(p + 12); }
        p += static_cast<size_t>(nUV) * 8;
        uint32_t nW = read_u32(p); p += 4;
        if (nW > 64 || p + static_cast<size_t>(nW) * 8 > end) return false;
        for (uint32_t k = 0; k < nW && k < 4; k++) { v.bone[k] = (int16_t)read_u32(p + k * 8); v.weight[k] = read_f32(p + k * 8 + 4); }
        p += static_cast<size_t>(nW) * 8;
        ts.vertices.push_back(v);
    }

    if (p + 4 > end) return false;
    uint32_t ngroups = read_u32(p); p += 4;
    if (ngroups == 0 || ngroups > 256) return false;
    for (uint32_t g = 0; g < ngroups; g++) {
        if (p + 4 > end) return false;
        p += 4; // group index
        std::string key;
        if (!read_lpstr(p, end, key, next)) return false; p = next;
        if (p + 4 > end) return false;
        uint32_t ntex = read_u32(p); p += 4;
        if (ntex > 8) return false;
        std::string tex;
        for (uint32_t t = 0; t < ntex; t++) {
            std::string s;
            if (!read_lpstr(p, end, s, next)) return false; p = next;
            if (t == 0) tex = s;
        }
        if (p + 4 + 1 + 4 > end) return false;
        p += 4; // unk (0)
        if (*p++ != 0xC9) return false; // primitive type 0 (triangle list) only
        uint32_t kk = read_u32(p); p += 4;
        if (kk % 3 != 0) return false;
        if (p + static_cast<size_t>(kk) * 5 > end) return false;

        // Resolve the group's material from the outer declarations by key.
        Material mat;
        bool found = false;
        for (const Material& m : ts.outer) {
            if (m.name == key) { mat = m; found = true; break; }
        }
        if (!found) {
            memset(&mat.ambient, 0, sizeof(mat.ambient));
            for (int j = 0; j < 4; j++) mat.diffuse[j] = 1.0f;
            memset(&mat.emissive, 0, sizeof(mat.emissive));
            memset(&mat.specular, 0, sizeof(mat.specular));
            mat.shininess = 0; mat.alpha = 1; mat.id = 0; mat.blend = 0; mat.type = 0;
            mat.name = key;
        }
        mat.texture_filename = tex;
        ts.groups.push_back(mat);
        if (ts.tex_name.empty() && !tex.empty()) { ts.tex_name = tex; ts.tex_filename = tex; }

        for (uint32_t t = 0; t < kk / 3; t++) {
            uint32_t a = read_u32(p), b = read_u32(p + 4), c = read_u32(p + 8);
            p += 12;
            if (a >= nverts || b >= nverts || c >= nverts) continue;
            Face f; f.vert[0] = (uint16_t)a; f.vert[1] = (uint16_t)b; f.vert[2] = (uint16_t)c;
            f.mat = (uint16_t)g;
            ts.faces.push_back(f);
        }
        p += kk; // per-corner flags (always 1), unused
    }
    ts.parsed_vertex_data = true;
    return true;
}

// -- Converted mesh body (flag==1): "Format B" ---------------------------
// u32 nverts, u32 stride, byte vertex_blob[nverts*stride]
//   vertex: pos(3f), norm(3f), color 4 x u8, u32 nUV, nUV*2 f32, ...
// f32 bbox_min[3], f32 bbox_max[3]
// u8 flag_skinned; if set: u32 must_be_4, 4 * byte[nverts*8]
// u32 ngroups; per group:
//   lpstr name1, lpstr name2, f32 ambient[4] diffuse[4] f3[4] f4[4] shininess,
//   lpstr blend, lpstr type, 60 bytes of optional render-state fields,
//   u32 n1, n1 * lpstr (texture-like refs), u32 n2, n2 * lpstr
// ngroups * lpstr texname; u32 indexcount[ngroups]; u32 trianglecount[ngroups];
// ngroups * u16 indices[indexcount[g]]
// then (skinned meshes only) u32, u8, lpstr* up to the 3-byte footer.
static bool try_converted(const uint8_t* p, const uint8_t* end, TagState& ts) {
    const uint8_t* next;
    if (p + 8 > end) return false;
    uint32_t nverts = read_u32(p); p += 4;
    uint32_t stride = read_u32(p); p += 4;
    if (nverts == 0 || nverts > 1000000) return false;
    if (stride < 28 || stride > 256) return false;

    const uint8_t* vstart = p;
    const uint8_t* vend = vstart + static_cast<size_t>(nverts) * stride;
    if (vend + 24 > end) return false;

    for (uint32_t i = 0; i < nverts; i++) {
        const uint8_t* vp = vstart + static_cast<size_t>(i) * stride;
        Vertex v;
        for (int j = 0; j < 3; j++) v.pos[j] = read_f32(vp + j * 4);
        for (int j = 0; j < 3; j++) v.norm[j] = read_f32(vp + 12 + j * 4);
        for (int j = 0; j < 4; j++) v.color[j] = vp[24 + j] / 255.0f;
        // Verified against textures: after the 4-byte colour come two UV sets with no count word
        // (stride 44 in every shipped converted mesh): UV0 at +28, UV1 (lightmap) at +36.
        v.uv[0] = v.uv[1] = v.lm[0] = v.lm[1] = 0.0f;
        if (stride >= 36) { v.uv[0] = read_f32(vp + 28); v.uv[1] = read_f32(vp + 32); }
        if (stride >= 44) { v.lm[0] = read_f32(vp + 36); v.lm[1] = read_f32(vp + 40); }
        ts.vertices.push_back(v);
    }

    ts.has_bbox = true;
    for (int j = 0; j < 3; j++) ts.bbox_min[j] = read_f32(vend + j * 4);
    for (int j = 0; j < 3; j++) ts.bbox_max[j] = read_f32(vend + 12 + j * 4);

    const uint8_t* q = vend + 24;
    if (q >= end) return false;
    uint8_t flag_skinned = *q++;
    if (flag_skinned) {
        if (q + 4 > end) return false;
        q += 4;
        size_t skin_bytes = static_cast<size_t>(nverts) * 8 * 4;
        if (q + skin_bytes > end) return false;
        // 4 parallel influence arrays, each nverts * (u32 bone, f32 weight); bone 0xffffffff = unused
        for (uint32_t a = 0; a < 4; a++)
            for (uint32_t i = 0; i < nverts; i++) {
                const uint8_t* e = q + (static_cast<size_t>(a) * nverts + i) * 8;
                uint32_t b = read_u32(e);
                if (b != 0xffffffffu) { ts.vertices[i].bone[a] = (int16_t)b; ts.vertices[i].weight[a] = read_f32(e + 4); }
            }
        q += skin_bytes;
    }

    if (q + 4 > end) return false;
    uint32_t ngroups = read_u32(q); q += 4;
    if (ngroups == 0 || ngroups > 256) return false;

    std::vector<Material> gm(ngroups);
    for (uint32_t g = 0; g < ngroups; g++) {
        Material& mat = gm[g];
        std::string name1, name2, blend, mtype;
        if (!read_lpstr(q, end, name1, next)) return false; q = next;
        if (!read_lpstr(q, end, name2, next)) return false; q = next;
        if (q + 17 * 4 > end) return false;
        float vals[17];
        for (int i = 0; i < 17; i++) { vals[i] = read_f32(q); q += 4; }
        memcpy(mat.ambient, vals, sizeof(float) * 4);
        memcpy(mat.diffuse, vals + 4, sizeof(float) * 4);
        memcpy(mat.specular, vals + 8, sizeof(float) * 4);
        memcpy(mat.emissive, vals + 12, sizeof(float) * 4);
        mat.shininess = vals[16];
        mat.alpha = mat.diffuse[3];
        mat.id = 0; mat.blend = 0; mat.type = 0;
        mat.name = name1;
        if (!read_lpstr(q, end, blend, next)) return false; q = next;
        if (!read_lpstr(q, end, mtype, next)) return false; q = next;
        mat.blend_name = blend;

        if (q + 60 > end) return false;
        q += 60;

        // Two std::string arrays (u32 count + lpstrs). The first holds the
        // texture file reference(s); the second the material name tokens.
        for (int arr = 0; arr < 2; arr++) {
            if (q + 4 > end) return false;
            uint32_t n = read_u32(q); q += 4;
            if (n > 64) return false;
            for (uint32_t i = 0; i < n; i++) {
                std::string s;
                if (!read_lpstr(q, end, s, next)) return false; q = next;
                if (arr == 0 && i == 0) mat.texture_filename = s;
            }
        }
    }

    std::vector<uint32_t> indexcount(ngroups);
    for (uint32_t g = 0; g < ngroups; g++) {
        std::string texname;
        if (!read_lpstr(q, end, texname, next)) return false; q = next;
        if (gm[g].texture_filename.empty()) gm[g].texture_filename = texname;
    }
    if (q + 8 * static_cast<size_t>(ngroups) > end) return false;
    for (uint32_t g = 0; g < ngroups; g++) { indexcount[g] = read_u32(q); q += 4; }
    q += 4 * static_cast<size_t>(ngroups); // trianglecount (== indexcount/3)

    for (uint32_t g = 0; g < ngroups; g++) {
        uint32_t n = indexcount[g];
        if (n % 3 != 0) return false;
        if (q + static_cast<size_t>(n) * 2 > end) return false;
        for (uint32_t t = 0; t < n / 3; t++) {
            uint32_t a = read_u16(q), b = read_u16(q + 2), c = read_u16(q + 4);
            q += 6;
            if (a >= nverts || b >= nverts || c >= nverts) continue;
            Face f; f.vert[0] = (uint16_t)a; f.vert[1] = (uint16_t)b; f.vert[2] = (uint16_t)c;
            f.mat = (uint16_t)g;
            ts.faces.push_back(f);
        }
    }

    for (uint32_t g = 0; g < ngroups; g++) {
        if (ts.tex_name.empty() && !gm[g].texture_filename.empty()) {
            ts.tex_name = gm[g].texture_filename; ts.tex_filename = gm[g].texture_filename;
        }
        ts.groups.push_back(std::move(gm[g]));
    }
    ts.parsed_vertex_data = true;
    return true;
}

// -- Tag dispatch ---------------------------------------------------------

static const uint8_t* parse_tag(const uint8_t* p, const uint8_t* end, TagState& ts) {
    if (p >= end) return nullptr;
    uint8_t tag = *p++;

    switch (tag) {
    case 0x07: { // Material
        Material mat;
        if (!parse_material_body(p, end, mat)) return nullptr;
        ts.outer.push_back(std::move(mat));
        break;
    }

    case 0x08: { // Texture: u8, lpstr name, lpstr filename, u32 w, u32 h
        if (p + 1 > end) return nullptr;
        p++;
        std::string name, fname; const uint8_t* next;
        if (!read_lpstr(p, end, name, next)) return nullptr; p = next;
        if (!read_lpstr(p, end, fname, next)) return nullptr; p = next;
        if (p + 8 > end) return nullptr;
        p += 8;
        if (ts.tex_name.empty()) { ts.tex_name = name; ts.tex_filename = fname; }
        break;
    }

    case 0x0A: { // Node: u8 type, lpstr name
        if (p + 1 > end) return nullptr;
        p++;
        std::string name; const uint8_t* next;
        if (!read_lpstr(p, end, name, next)) return nullptr; p = next;
        break;
    }

    case 0x0E: { // Bone: lpstr name, u16 parent, u8 flags (bitmask)
        // bit0 = position (3 floats), bit1 = rotation axis+angle (4 floats)
        std::string name; const uint8_t* next;
        if (!read_lpstr(p, end, name, next)) return nullptr; p = next;
        if (p + 3 > end) return nullptr;
        ts.bones.push_back(name);
        p += 2;
        uint8_t flags = *p++;
        if (flags & 1) { if (p + 12 > end) return nullptr; p += 12; }
        if (flags & 2) { if (p + 16 > end) return nullptr; p += 16; }
        break;
    }

    case 0x0F: { // Mesh (terminal)
        bool ok;
        if (ts.v3) {
            if (p + 2 > end) return nullptr;
            ok = p[1] == 0 && try_raw(p + 2, end, ts);
        } else {
            if (p + 3 > end) return nullptr;
            uint8_t flag = p[2];
            if (flag == 1)      ok = try_converted(p + 3, end, ts);
            else if (flag == 0) ok = try_raw(p + 3, end, ts);
            else                ok = false;
        }
        if (!ok) return nullptr;
        p = end;
        break;
    }

    default:
        return nullptr;
    }

    return p;
}

// -- Header -----------------------------------------------------------------

static uint32_t find_tag_start(const uint8_t* data, size_t size, bool& v3) {
    v3 = false;
    if (size < 40) throw std::runtime_error("G3D: file too small");
    if (data[8] == 0x03 && data[9] == 0x00) { v3 = true; return 0x25; }
    if (data[8] == 0x1c && data[9] == 0x01) return 0x27;
    char buf[64];
    snprintf(buf, sizeof(buf), "G3D: unsupported header version 0x%02x%02x", data[9], data[8]);
    throw std::runtime_error(buf);
}

// -- Main parser --------------------------------------------------

Mesh parseG3D(const std::string& path) {
    std::vector<uint8_t> data;
    if (!vfs::readBytes(path, data)) throw std::runtime_error(std::string("G3D: cannot open ") + path);
    size_t size = data.size();

    if (size < 8) throw std::runtime_error("G3D: file too small");
    if (memcmp(data.data(), "B3D 1.1 ", 8) != 0) {
        throw std::runtime_error(std::string("G3D: bad magic: ") +
                                 std::string(reinterpret_cast<const char*>(data.data()), 8));
    }

    TagState ts;
    uint32_t tag_start = find_tag_start(data.data(), size, ts.v3);
    if (tag_start >= size) throw std::runtime_error("G3D: tag_start beyond file size");

    const uint8_t* p = data.data() + tag_start;
    const uint8_t* end = data.data() + size;
    bool saw_mesh = false;
    while (p < end) {
        uint8_t tag = p[0];
        if (tag < 0x07 || tag > 0x0F) {
            throw std::runtime_error("G3D: unknown tag byte " + std::to_string((int)tag));
        }
        bool is_mesh = (tag == 0x0F);
        p = parse_tag(p, end, ts);
        if (!p) throw std::runtime_error("G3D: parse error in tag loop");
        if (is_mesh) { saw_mesh = true; break; }
    }

    if (!saw_mesh || !ts.parsed_vertex_data) {
        throw std::runtime_error("G3D: no valid vertex data found");
    }

    Mesh mesh;
    mesh.vertices = std::move(ts.vertices);
    mesh.faces = std::move(ts.faces);
    mesh.materials = std::move(ts.groups);   // Face::mat indexes this
    for (Material& g : mesh.materials) if (g.shader.empty()) for (const Material& o : ts.outer) if (o.name == g.name && !o.shader.empty()) { g.shader = o.shader; break; }       // the shader name lives in the file-level material tag
    mesh.bones = std::move(ts.bones);
    mesh.texture_name = std::move(ts.tex_name);
    mesh.texture_filename = std::move(ts.tex_filename);
    mesh.has_bbox = ts.has_bbox;
    memcpy(mesh.bbox_min, ts.bbox_min, sizeof(ts.bbox_min));
    memcpy(mesh.bbox_max, ts.bbox_max, sizeof(ts.bbox_max));

    return mesh;
}

bool humanAtlasRect(const std::string& stem, const Mesh& mesh, int texW, int texH, UVRect& out) {
    // The bare body mesh ("HumMNor") skins the top-left quadrant of the atlas with the full-sheet Skin_*.tga.
    const std::string bodyType = stem.size() == 7 ? stem.substr(4, 3) : std::string();     // Skl (skeleton monster) uses its texture sheet whole
    if (stem.size() == 7 && stem.compare(0, 3, "Hum") == 0 && (stem[3] == 'M' || stem[3] == 'F') && (bodyType == "Fat" || bodyType == "Nor" || bodyType == "Str" || bodyType == "Wir") && texW > 0 && texH > 0 && texW <= 512 && texH <= 512 && !mesh.vertices.empty()) {
        out = {0.f, 0.f, texW / 512.f, texH / 512.f}; return true;
    }
    // "Hum" + M|F + Fat|Nor|Str|Wir + "_" + 3-letter part code
    if (stem.size() < 12 || stem.compare(0, 3, "Hum") != 0 || (stem[3] != 'M' && stem[3] != 'F') || stem[7] != '_') return false;
    const std::string body = stem.substr(4, 3);
    if (body != "Fat" && body != "Nor" && body != "Str" && body != "Wir") return false;
    if (texW <= 0 || texH <= 0 || texW > 512 || texH > 512 || mesh.vertices.empty()) return false;
    const float su = texW / 512.f, sv = texH / 512.f;
    if (stem.compare(8, 3, "Bod") == 0) { out = {0.f, 0.5f, su, sv}; return true; }
    float mu = 1e9f, mv = 1e9f;
    for (const Vertex& v : mesh.vertices) { mu = std::min(mu, v.uv[0]); mv = std::min(mv, v.uv[1]); }
    out = {std::floor(mu / su + 1e-3f) * su, std::floor(mv / sv + 1e-3f) * sv, su, sv};
    return true;
}

std::vector<std::string> coveredGroups(const Mesh& base, const std::vector<const Mesh*>& overlays, float dist, float fraction) {
    std::vector<std::vector<uint32_t>> verts(base.materials.size());
    for (const Face& f : base.faces) if (f.mat < verts.size()) for (int k = 0; k < 3; k++) verts[f.mat].push_back(f.vert[k]);
    std::vector<std::string> out;
    const float d2 = dist * dist;
    for (size_t g = 0; g < verts.size(); g++) {
        std::vector<uint32_t>& v = verts[g];
        std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end());
        if (v.empty()) continue;
        size_t covered = 0;
        for (uint32_t vi : v) {
            const float* p = base.vertices[vi].pos; bool hit = false;
            for (const Mesh* m : overlays) {
                for (const Vertex& o : m->vertices) {
                    float dx = p[0] - o.pos[0], dy = p[1] - o.pos[1], dz = p[2] - o.pos[2];
                    if (dx * dx + dy * dy + dz * dz < d2) { hit = true; break; }
                }
                if (hit) break;
            }
            covered += hit;
        }
        if ((float)covered >= fraction * (float)v.size()) out.push_back(base.materials[g].name);
    }
    return out;
}

} // namespace vb

#pragma once
// Shared level + entity rendering for the Van Buren viewers/game: math helpers, world shaders, texture library, octree level loading
// (batches, markers, collision overlay) and the entity renderer (placed props/doors/characters, GPU skinning, idle animation).
#include "assets/vfs.h"
#include "assets/tre.h"
#include "assets/map.h"
#include "assets/col.h"
#include "assets/tga.h"
#include "assets/entity.h"
#include "assets/anim.h"
#include "common/gmodel.h"
#include "common/ui.h"
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------- math (column-major) ----
struct V3 { float x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { float l = std::sqrt(dot(a, a)); return l > 0 ? a * (1.f / l) : a; }

static void perspective(float* m, float fovyDeg, float aspect, float n, float f) {
    float t = 1.f / std::tan(fovyDeg * 3.14159265f / 360.f);
    std::memset(m, 0, 64);
    m[0] = t / aspect; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1.f; m[14] = 2.f * f * n / (n - f);
}
static void lookAt(float* m, V3 eye, V3 fwd) {
    // The engine's world is left-handed (D3D): looking down +Z, +X is to the right. This view matrix has determinant -1 (a mirror)
    // relative to a right-handed GL camera, so the maps appear the way the original engine shows them.
    V3 f = norm(fwd), s = norm(cross({0, 1, 0}, f)), u = cross(f, s);
    m[0] = s.x; m[4] = s.y; m[8] = s.z;  m[12] = -dot(s, eye);
    m[1] = u.x; m[5] = u.y; m[9] = u.z;  m[13] = -dot(u, eye);
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = dot(f, eye);
    m[3] = m[7] = m[11] = 0; m[15] = 1;
}

// ---------------------------------------------------------------- GL helpers / shaders ----
static const char* kWorldVS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec2 aLM; layout(location=3) in vec4 aCol;
uniform mat4 VP; out vec2 vUV; out vec2 vLM; out vec4 vCol; out vec2 vWorld; out float vH;
void main(){ vUV=aUV; vLM=aLM; vCol=aCol.bgra; vWorld=aPos.xz; vH=aPos.y; gl_Position=VP*vec4(aPos,1.0); })";
static const std::string kWorldFSsrc = std::string("#version 330 core\n") + gm::kFogGLSL + R"(in vec2 vUV; in vec2 vLM; in vec4 vCol; in vec2 vWorld; in float vH; out vec4 frag;
uniform sampler2D tex; uniform sampler2D lm; uniform int hasTex; uniform int hasLm; uniform int useVC; uniform float alphaCut; uniform float alphaMul = 1.0; uniform float cutY = 1e9;
void main(){
  vec4 base = hasTex!=0 ? texture(tex,vUV) : vec4(0.75);
  vec3 vc = useVC!=0 ? vCol.rgb*2.0 : vec3(1.0);
  vec3 l = hasLm!=0 ? texture(lm,vLM).rgb*2.0 : vec3(1.0);
  float a = base.a * (useVC!=0 ? vCol.a : 1.0) * (vH > cutY ? alphaMul : 1.0);
  if (a < alphaCut) discard;
  frag = vec4(base.rgb*vc*l*fogVisibility(vWorld, vH), a);
})";;
static const char* kWorldFS = kWorldFSsrc.c_str();

static const char* kLineVS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aCol; uniform mat4 VP; out vec3 vCol;
void main(){ vCol=aCol; gl_Position=VP*vec4(aPos,1.0); })";
static const char* kLineFS = R"(#version 330 core
in vec3 vCol; out vec4 frag; void main(){ frag=vec4(vCol,1.0); })";
// ---------------------------------------------------------------- 2D text ----
// ---------------------------------------------------------------- textures ----
struct TexLib {
    std::unordered_map<std::string, std::string> index;     // lowercase basename -> path
    std::unordered_map<std::string, GLuint> cache;
    int missing = 0;
    explicit TexLib(const std::string& assets) {
        for (const std::string& p : vb::vfs::walk(assets)) {
            fs::path fp(p); std::string n = fp.filename().string();
            for (auto& c : n) c = (char)std::tolower((unsigned char)c);
            if (fp.extension() == ".tga" || (n.size() > 4 && n.compare(n.size() - 4, 4, ".tga") == 0)) index.emplace(n, p);
        }
    }
    GLuint get(std::string name, bool clamp) {
        for (auto& c : name) c = (char)std::tolower((unsigned char)c);
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        name += ".tga";
        auto c = cache.find(name);
        if (c != cache.end()) return c->second;
        GLuint id = 0;
        auto it = index.find(name);
        if (it != index.end()) {
            vb::Texture t; std::string err;
            if (vb::loadTga(it->second, t, err)) {
                glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                GLenum wrap = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
            } else { std::fprintf(stderr, "texture %s: %s\n", name.c_str(), err.c_str()); missing++; }
        } else { std::fprintf(stderr, "texture not found: %s\n", name.c_str()); missing++; }
        cache[name] = id;
        return id;
    }
};

// ---------------------------------------------------------------- level ----
struct Batch { GLuint vao = 0, ebo = 0; int count = 0; GLuint tex = 0, lm = 0; bool blend = false, additive = false, noTexture = false; uint8_t flag = 0; int roofOf = 0; float cutY = 1e9f; };   // roofOf: building number whose top story (roof / upper floor) this batch is, 0 = none   // flag: the octree's FLGS byte without bit 0 (building / story marks)

struct Level {
    std::string name, dir, mapStem;               // mapStem: the .map file's stem incl. numeric prefix (00_03_Tutorial_Junktown)
    vb::Tre tre; vb::Map map; vb::Col col; bool haveMap = false, haveCol = false;
    std::vector<GLuint> vbos, ebos, vaos;
    std::vector<Batch> batches;
    // Buildings: the map's boundary triggers (EBTR, id = building number) outline each building and their height is the cut line: the geometry above it inside the outline
    // (roof, upper storey) fades out while the player is inside the building (the FLGS / material story bits are not what the engine uses for it, many roof triangles carry none).
    // bldFade is the current opacity of each building's top part (1 = shown).
    struct BldZone { int id = 0; float cutY = 0; std::vector<float> xz; };
    std::vector<BldZone> zones;
    float bldFade[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    static bool inPoly(const BldZone& zn, float x, float z) {
        bool in = false; size_t n = zn.xz.size() / 2;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            float xi = zn.xz[i * 2], zi = zn.xz[i * 2 + 1], xj = zn.xz[j * 2], zj = zn.xz[j * 2 + 1];
            if ((zi > z) != (zj > z) && x < (xj - xi) * (z - zi) / (zj - zi) + xi) in = !in;
        }
        return in;
    }
    int bldInside(float x, float y, float z) const { for (const BldZone& zn : zones) if (y < zn.cutY && inPoly(zn, x, z)) return zn.id; return 0; }   // not when standing above the cut line (on the roof)
    // building whose top part a triangle belongs to (centroid inside the outline and above the cut line), 0 = none
    static float distToOutline(const BldZone& zn, float x, float z) {
        float best = 1e30f; size_t n = zn.xz.size() / 2;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            float ax = zn.xz[j * 2], az = zn.xz[j * 2 + 1], bx = zn.xz[i * 2], bz = zn.xz[i * 2 + 1], dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
            float t = l2 > 1e-9f ? std::max(0.f, std::min(1.f, ((x - ax) * dx + (z - az) * dz) / l2)) : 0.f, qx = ax + dx * t - x, qz = az + dz * t - z;
            best = std::min(best, qx * qx + qz * qz);
        }
        return std::sqrt(best);
    }
    // Wall triangles that straddle the cut line belong to the building as well (the shader only fades their part above the line); the outline is the walls' middle, so the
    // test is a little generous. Floors / ground triangles below the line never qualify.
    int roofOfTri(const float* p0, const float* p1, const float* p2, unsigned flag = 0) const {
        float cx = (p0[0] + p1[0] + p2[0]) / 3.f, cz = (p0[2] + p1[2] + p2[2]) / 3.f, hy = std::max(p0[1], std::max(p1[1], p2[1]));
        for (const BldZone& zn : zones) if (hy > zn.cutY + 0.05f && (inPoly(zn, cx, cz) || distToOutline(zn, cx, cz) < 4.f || distToOutline(zn, p0[0], p0[2]) < 1.5f || distToOutline(zn, p1[0], p1[2]) < 1.5f || distToOutline(zn, p2[0], p2[2]) < 1.5f || (flag >> 5 == (unsigned)zn.id && (flag & 0x1c)))) return zn.id;
        return 0;
    }
    float cutOf(int id) const { for (const BldZone& zn : zones) if (zn.id == id) return zn.cutY; return 1e9f; }
    GLuint lvao = 0, lvbo = 0, colTex = 0, qvao = 0, qvbo = 0; int lineVerts = 0;
    struct EntDraw {
        std::vector<gm::GpuModel*> models; float M[16]; const vb::GrSkeleton* skel = nullptr; const vb::GrAnimation* anim = nullptr; float phase = 0;
        int mapIdx = -1;                                                       // index in Map::entities (the game world links to it)
        const vb::GrAnimation *walk = nullptr, *run = nullptr, *death = nullptr, *open = nullptr, *atkGun = nullptr, *atkMelee = nullptr, *atkUna = nullptr, *hit = nullptr,
                                *idle2 = nullptr, *idle2d = nullptr, *walk2 = nullptr, *atk2 = nullptr, *hit2 = nullptr, *lay = nullptr, *lying = nullptr, *getup = nullptr, *twitch = nullptr, *maint = nullptr;      // lay: lying down, lying: lying idle, getup (the dogs)      // the two-handed set (minigun, flamethrower ...)   // other animations of the model (characters: walk/run/death, doors: open)
        float animT = 0; int animState = 0;                                    // game-owned playback clock and last state
    };
    std::vector<EntDraw> ents;                    // placed entities with a drawable model (models owned by the viewer's cache)
    size_t tris = 0;
    V3 cmin{0, 0, 0}, cmax{0, 0, 0};

    void destroy() {
        for (GLuint v : vbos) glDeleteBuffers(1, &v);
        for (GLuint v : ebos) glDeleteBuffers(1, &v);
        for (GLuint v : vaos) glDeleteVertexArrays(1, &v);
        if (lvao) glDeleteVertexArrays(1, &lvao);
        if (lvbo) glDeleteBuffers(1, &lvbo);
        if (qvao) glDeleteVertexArrays(1, &qvao);
        if (qvbo) glDeleteBuffers(1, &qvbo);
        if (colTex) glDeleteTextures(1, &colTex);
        *this = Level();
    }
};

static bool loadLevel(const std::string& assets, const std::string& name, TexLib& lib, Level& L, std::string& err) {
    L.destroy();
    L.name = name;
    L.dir = assets + "/Maps/";
    if (!vb::vfs::exists(L.dir + name + ".tre")) L.dir = assets + "/Engine/";
    try { L.tre = vb::loadTre(L.dir + name + ".tre"); } catch (std::exception& e) { err = e.what(); return false; }
    // the .map file has a numeric / zz_TestMaps prefix (e.g. 00_03_Tutorial_Junktown.map): match by suffix
    for (const std::string& fn : vb::vfs::list(L.dir)) {
        if (fs::path(fn).extension() == ".map" && (fn == name + ".map" || (fn.size() > name.size() + 4 && fn.compare(fn.size() - name.size() - 4, name.size(), name) == 0))) {
            try { L.map = vb::loadMap(L.dir + fn); L.haveMap = true; L.mapStem = fs::path(fn).stem().string(); } catch (std::exception& ex) { std::fprintf(stderr, "map: %s\n", ex.what()); }
            break;
        }
    }
    try { L.col = vb::loadCol(L.dir + name + ".col"); L.haveCol = true; } catch (std::exception& e) { std::fprintf(stderr, "col: %s\n", e.what()); }
    const vb::Tre& tre = L.tre;
    for (auto& l : tre.leaves) for (auto& b : l.batches) L.tris += b.indices.size() / 3;

    for (auto& b : tre.buffers) {
        std::vector<float> v((size_t)b.count * 8);
        for (uint32_t i = 0; i < b.count; i++) {
            float* o = &v[(size_t)i * 8];
            o[0] = b.xyz[i*3]; o[1] = b.xyz[i*3+1]; o[2] = b.xyz[i*3+2];
            o[3] = b.uv[i*2]; o[4] = b.uv[i*2+1]; o[5] = b.lmuv[i*2]; o[6] = b.lmuv[i*2+1];
            std::memcpy(&o[7], &b.diffuse[i], 4);
        }
        GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
        L.vbos.push_back(vbo);
    }
    if (L.haveMap) for (const vb::MapTrigger& t : L.map.triggers) if (t.type == 6 && t.id >= 1 && t.id < 8 && t.verts.size() >= 9) {
        Level::BldZone z; z.id = (int)t.id; z.cutY = t.verts[1]; for (size_t i = 0; i + 2 < t.verts.size(); i += 3) { z.xz.push_back(t.verts[i]); z.xz.push_back(t.verts[i + 2]); }
        L.zones.push_back(std::move(z));
    }

    using Key = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>;   // buffer, material, lightmap, layer, roofOf << 8 | flag
    std::map<Key, std::vector<uint32_t>> merged;
    for (auto& l : tre.leaves) {
        size_t ti = 0; const vb::TreVertexBuffer& vbuf = tre.buffers[l.buffer];
        for (auto& b : l.batches) {
            for (size_t i = 0; i + 2 < b.indices.size(); i += 3, ti++) {
                uint32_t fl = ti < l.tri_flags.size() ? (l.tri_flags[ti] & 0xfeu) : 0u;
                uint32_t roof = L.zones.empty() ? 0u : (uint32_t)L.roofOfTri(&vbuf.xyz[b.indices[i] * 3], &vbuf.xyz[b.indices[i + 1] * 3], &vbuf.xyz[b.indices[i + 2] * 3], fl);
                auto& v = merged[{l.buffer, b.material, b.lightmap, b.layer, roof << 8 | fl}];
                for (int k = 0; k < 3; k++) v.push_back(b.indices[i + k]);
            }
        }
    }
    for (auto& [k, idx] : merged) {
        auto [buf, mat, lmNum, layer, fl] = k;
        Batch b; b.count = (int)idx.size(); b.flag = (uint8_t)(fl & 0xff); b.roofOf = (int)(fl >> 8); if (b.roofOf) b.cutY = L.cutOf(b.roofOf);
        glGenVertexArrays(1, &b.vao); glBindVertexArray(b.vao);
        glBindBuffer(GL_ARRAY_BUFFER, L.vbos[buf]);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);          glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (void*)12);         glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)20);         glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void*)28);  glEnableVertexAttribArray(3);
        glGenBuffers(1, &b.ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
        L.vaos.push_back(b.vao); L.ebos.push_back(b.ebo);
        if (layer > 0 && layer <= tre.textures.size()) b.tex = lib.get(tre.textures[layer - 1], false);
        if (lmNum > 0) {                           // 1-based lightmap number -> texture slot first_lightmap + n - 1
            size_t slot = tre.first_lightmap + lmNum - 1;
            if (slot >= 1 && slot <= tre.textures.size()) b.lm = lib.get(tre.textures[slot - 1], true);
        }
        const vb::TreMaterial& m = tre.materials[mat];
        b.blend = m.blend != "OPAQUE" && !m.blend.empty();
        b.additive = m.blend == "ADD";
        b.noTexture = m.shader == 0;                  // NO_TEXTURE shader
        L.batches.push_back(b);
    }
    std::stable_sort(L.batches.begin(), L.batches.end(), [](const Batch& a, const Batch& b) { return a.blend < b.blend; });

    // debug markers
    std::vector<float> ml;   // x y z r g b
    auto line = [&](V3 a, V3 b, V3 c) { for (V3 p : {a, b}) ml.insert(ml.end(), {p.x, p.y, p.z, c.x, c.y, c.z}); };
    if (L.haveMap) {
        const vb::Map& map = L.map;
        for (auto& e : map.entities) { V3 p{e.pos[0], e.pos[1], e.pos[2]}; V3 c{0.2f, 1, 0.2f}; line(p, p + V3{0, 3, 0}, c); line(p + V3{-.5f, 0, 0}, p + V3{.5f, 0, 0}, c); line(p + V3{0, 0, -.5f}, p + V3{0, 0, .5f}, c); }
        for (auto& e : map.effects) { V3 p{e.pos[0], e.pos[1], e.pos[2]}; line(p, p + V3{0, 4, 0}, {1, 0.6f, 0.1f}); }
        for (auto& e : map.sounds) { V3 p{e.pos[0], e.pos[1], e.pos[2]}; line(p, p + V3{0, 2, 0}, {0.9f, 0.9f, 0.2f}); }
        for (auto& e : map.entries) for (int i = 0; i < 6; i++) { V3 p{e.pos[i][0], e.pos[i][1], e.pos[i][2]}; line(p, p + V3{0, 5, 0}, {0.3f, 0.5f, 1}); }
        for (auto& t : map.triggers) {
            V3 c = t.type == 0 ? V3{1, 0.2f, 1} : t.type == 1 ? V3{1, 0.3f, 0.3f} : V3{0.5f, 0.5f, 0.5f};
            size_t n = t.verts.size() / 3;
            for (size_t i = 0; i < n; i++) { size_t j = (i + 1) % n; line({t.verts[i*3], t.verts[i*3+1] + .2f, t.verts[i*3+2]}, {t.verts[j*3], t.verts[j*3+1] + .2f, t.verts[j*3+2]}, c); }
        }
        for (auto& p : map.paths) for (size_t i = 0; i + 1 < p.pts.size() / 3; i++) line({p.pts[i*3], p.pts[i*3+1], p.pts[i*3+2]}, {p.pts[i*3+3], p.pts[i*3+4], p.pts[i*3+5]}, {0.2f, 1, 1});
        for (size_t i = 0; i < map.nav.size(); i++) {
            V3 a{map.nav[i].pos[0], map.nav[i].pos[1], map.nav[i].pos[2]};
            line(a, a + V3{0, 1.5f, 0}, {0.1f, 0.6f, 0.6f});
            for (uint8_t l : map.nav[i].links) if (l != 0xff && l < map.nav.size()) line(a + V3{0, .5f, 0}, V3{map.nav[l].pos[0], map.nav[l].pos[1] + .5f, map.nav[l].pos[2]}, {0.1f, 0.6f, 0.6f});
        }
    }
    for (auto& l : tre.lights) {            // static lights (LGTD): colored cross + range ring; spots also get a direction line
        V3 p{l.pos[0], l.pos[1], l.pos[2]}, c{l.color[0], l.color[1], l.color[2]};
        line(p + V3{0, -1, 0}, p + V3{0, 1, 0}, c); line(p + V3{-1, 0, 0}, p + V3{1, 0, 0}, c); line(p + V3{0, 0, -1}, p + V3{0, 0, 1}, c);
        for (int i = 0; i < 24; i++) {
            float a0 = i * 6.2831853f / 24, a1 = (i + 1) * 6.2831853f / 24;
            line(p + V3{std::cos(a0) * l.range, 0, std::sin(a0) * l.range}, p + V3{std::cos(a1) * l.range, 0, std::sin(a1) * l.range}, c * 0.5f);
        }
        if (l.type == 1 && std::isfinite(l.dir[0])) line(p, p + V3{l.dir[0], l.dir[1], l.dir[2]} * std::min(l.range, 10.f), c);
    }
    glGenVertexArrays(1, &L.lvao); glBindVertexArray(L.lvao);
    glGenBuffers(1, &L.lvbo); glBindBuffer(GL_ARRAY_BUFFER, L.lvbo);
    glBufferData(GL_ARRAY_BUFFER, ml.size() * 4, ml.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12); glEnableVertexAttribArray(1);
    L.lineVerts = (int)(ml.size() / 6);

    // collision overlay quad (2 cells per world unit, origin at the octree root min XZ)
    if (L.haveCol) {
        const vb::Col& col = L.col;
        std::vector<uint8_t> px((size_t)col.width * col.height * 4);
        for (int z = 0; z < col.height; z++) for (int x = 0; x < col.width; x++) {
            uint8_t v = col.at(x, z); uint8_t* o = &px[((size_t)z * col.width + x) * 4];
            if (v == 47) { o[0] = o[1] = o[2] = 0; o[3] = 0; continue; }
            o[0] = (uint8_t)(60 + (v * 53) % 196); o[1] = (uint8_t)(60 + (v * 97) % 196); o[2] = (uint8_t)(60 + (v * 31) % 196); o[3] = 150;
        }
        glGenTextures(1, &L.colTex); glBindTexture(GL_TEXTURE_2D, L.colTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, col.width, col.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        float x0 = tre.min[0], z0 = tre.min[2], x1 = x0 + col.width * 0.5f, z1 = z0 + col.height * 0.5f, y = tre.min[1] + 0.3f;
        uint32_t white = 0xff7f7f7f;
        float qv[4][8] = {{x0, y, z0, 0, 0, 0, 0, 0}, {x1, y, z0, 1, 0, 0, 0, 0}, {x1, y, z1, 1, 1, 0, 0, 0}, {x0, y, z1, 0, 1, 0, 0, 0}};
        for (auto& v : qv) std::memcpy(&v[7], &white, 4);
        glGenVertexArrays(1, &L.qvao); glBindVertexArray(L.qvao);
        glGenBuffers(1, &L.qvbo); glBindBuffer(GL_ARRAY_BUFFER, L.qvbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(qv), qv, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);          glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (void*)12);         glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)20);         glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void*)28);  glEnableVertexAttribArray(3);
    }
    L.cmin = {tre.min[0], tre.min[1], tre.min[2]}; L.cmax = {tre.max[0], tre.max[1], tre.max[2]};
    return true;
}

// ---------------------------------------------------------------- matrices / picking helpers ----
static void mat4mul(float* out, const float* a, const float* b) {            // column-major: out = a * b
    float r[16];
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int i = 0; i < 4; i++) s += a[i * 4 + rr] * b[c * 4 + i]; r[c * 4 + rr] = s; }
    std::memcpy(out, r, 64);
}
static bool mat4inverse(const float* m, float* inv) {
    float t[16], det;
    t[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    t[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    t[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    t[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    t[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    t[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    t[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    t[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    t[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    t[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    t[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    t[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    t[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    det = m[0]*t[0] + m[1]*t[4] + m[2]*t[8] + m[3]*t[12];
    if (std::fabs(det) < 1e-20f) return false;
    det = 1.f / det; for (int i = 0; i < 16; i++) inv[i] = t[i] * det;
    return true;
}
// World-space ray through window pixel (px, py) given the combined view-projection matrix.
static bool pickRay(const float* VP, float px, float py, int w, int h, V3& origin, V3& dir) {
    float inv[16]; if (!mat4inverse(VP, inv)) return false;
    float nx = 2.f * px / w - 1.f, ny = 1.f - 2.f * py / h;
    auto unproj = [&](float z) { float v[4] = {nx, ny, z, 1.f}, o[4]; for (int r = 0; r < 4; r++) o[r] = inv[r] * v[0] + inv[4 + r] * v[1] + inv[8 + r] * v[2] + inv[12 + r] * v[3]; return V3{o[0] / o[3], o[1] / o[3], o[2] / o[3]}; };
    V3 a = unproj(-1.f), b = unproj(1.f); origin = a; dir = norm(b - a);
    return true;
}

// ---------------------------------------------------------------- level queries ----
// Highest octree surface at (x, z) that is not more than `reach` above refY.
// Triangle lookup grid over the xz plane: groundHeight() runs for every colliding particle every frame, so the brute-force scan of
// all triangles it used to do is far too slow (frame time grew with the particle count).
struct GroundGrid {
    struct Tri { const float *p0, *p1, *p2; };
    const vb::Tre* tre = nullptr; const void* sig = nullptr; size_t sigN = 0;
    float minx = 0, minz = 0, cell = 4.f; int nx = 0, nz = 0; std::vector<std::vector<Tri>> cells;
};
static bool groundHeight(const vb::Tre& t, float x, float z, float refY, float reach, float& out) {
    static GroundGrid g;
    const void* sig = t.buffers.empty() ? nullptr : (const void*)t.buffers[0].xyz.data(); size_t sigN = t.leaves.size() * 1000003u + t.buffers.size();
    if (g.tre != &t || g.sig != sig || g.sigN != sigN) {
        g = GroundGrid(); g.tre = &t; g.sig = sig; g.sigN = sigN;
        float mnx = 1e30f, mnz = 1e30f, mxx = -1e30f, mxz = -1e30f;
        for (auto& l : t.leaves) { mnx = std::min(mnx, l.min[0]); mnz = std::min(mnz, l.min[2]); mxx = std::max(mxx, l.max[0]); mxz = std::max(mxz, l.max[2]); }
        if (mnx > mxx) { return false; }
        g.minx = mnx; g.minz = mnz; g.nx = std::max(1, std::min(512, (int)((mxx - mnx) / g.cell) + 1)); g.nz = std::max(1, std::min(512, (int)((mxz - mnz) / g.cell) + 1));
        g.cell = std::max(g.cell, std::max((mxx - mnx) / g.nx, (mxz - mnz) / g.nz) + 1e-3f);
        g.cells.assign((size_t)g.nx * g.nz, {});
        for (auto& l : t.leaves) {
            const vb::TreVertexBuffer& vbuf = t.buffers[l.buffer];
            for (auto& b : l.batches) for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
                const float* p0 = &vbuf.xyz[b.indices[i] * 3]; const float* p1 = &vbuf.xyz[b.indices[i + 1] * 3]; const float* p2 = &vbuf.xyz[b.indices[i + 2] * 3];
                float lx = std::min(p0[0], std::min(p1[0], p2[0])), hx = std::max(p0[0], std::max(p1[0], p2[0])), lz = std::min(p0[2], std::min(p1[2], p2[2])), hz = std::max(p0[2], std::max(p1[2], p2[2]));
                int x0 = std::max(0, (int)((lx - g.minx) / g.cell)), x1 = std::min(g.nx - 1, (int)((hx - g.minx) / g.cell)), z0 = std::max(0, (int)((lz - g.minz) / g.cell)), z1 = std::min(g.nz - 1, (int)((hz - g.minz) / g.cell));
                for (int cz = z0; cz <= z1; cz++) for (int cx = x0; cx <= x1; cx++) g.cells[(size_t)cz * g.nx + cx].push_back({p0, p1, p2});
            }
        }
    }
    int cx = (int)std::floor((x - g.minx) / g.cell), cz = (int)std::floor((z - g.minz) / g.cell);
    if (cx < 0 || cz < 0 || cx >= g.nx || cz >= g.nz) return false;
    bool found = false; float best = -1e30f;
    for (const GroundGrid::Tri& tr : g.cells[(size_t)cz * g.nx + cx]) {
        const float *p0 = tr.p0, *p1 = tr.p1, *p2 = tr.p2;
        float d = (p1[2] - p2[2]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[2] - p2[2]);
        if (std::fabs(d) < 1e-9f) continue;
        float a = ((p1[2] - p2[2]) * (x - p2[0]) + (p2[0] - p1[0]) * (z - p2[2])) / d;
        float bb = ((p2[2] - p0[2]) * (x - p2[0]) + (p0[0] - p2[0]) * (z - p2[2])) / d;
        float c = 1.f - a - bb;
        if (a < 0 || bb < 0 || c < 0) continue;
        float y = a * p0[1] + bb * p1[1] + c * p2[1];
        if (y <= refY + reach && y > best) { best = y; found = true; }
    }
    if (found) out = best;
    return found;
}
// Nearest floor-like surface (|normal.y| > minUp) hit by a world ray; returns the hit point.
static bool rayFloor(const vb::Tre& t, V3 o, V3 d, float minUp, V3& hit, const struct Level* lvl = nullptr) {   // lvl: triangles of a hidden building top (bldFade < 0.5) are skipped
    float bestT = 1e30f; bool found = false;
    for (auto& l : t.leaves) {
        // slab test against the leaf bounds
        float t0 = 0, t1 = 1e30f; bool ok = true;
        for (int k = 0; k < 3 && ok; k++) {
            float ok_ = k == 0 ? o.x : k == 1 ? o.y : o.z, dk = k == 0 ? d.x : k == 1 ? d.y : d.z;
            if (std::fabs(dk) < 1e-9f) { if (ok_ < l.min[k] || ok_ > l.max[k]) ok = false; continue; }
            float a = (l.min[k] - ok_) / dk, b = (l.max[k] - ok_) / dk; if (a > b) std::swap(a, b);
            t0 = std::max(t0, a); t1 = std::min(t1, b); if (t0 > t1) ok = false;
        }
        if (!ok || t0 > bestT) continue;
        const vb::TreVertexBuffer& vbuf = t.buffers[l.buffer];
        for (auto& b : l.batches) for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
            const float* p0 = &vbuf.xyz[b.indices[i] * 3]; const float* p1 = &vbuf.xyz[b.indices[i + 1] * 3]; const float* p2 = &vbuf.xyz[b.indices[i + 2] * 3];
            if (lvl && !lvl->zones.empty()) { int rf = lvl->roofOfTri(p0, p1, p2); if (rf && lvl->bldFade[rf] < 0.5f && std::min(p0[1], std::min(p1[1], p2[1])) >= lvl->cutOf(rf) - 0.05f) continue; }
            V3 e1{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2{p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            V3 n = cross(e1, e2); float nl = std::sqrt(dot(n, n)); if (nl < 1e-9f || std::fabs(n.y) / nl < minUp) continue;
            V3 pv = cross(d, e2); float det = dot(e1, pv); if (std::fabs(det) < 1e-12f) continue;
            float inv = 1.f / det; V3 tv{o.x - p0[0], o.y - p0[1], o.z - p0[2]};
            float u = dot(tv, pv) * inv; if (u < 0 || u > 1) continue;
            V3 qv = cross(tv, e1); float v = dot(d, qv) * inv; if (v < 0 || u + v > 1) continue;
            float tt = dot(e2, qv) * inv; if (tt > 0 && tt < bestT) { bestT = tt; found = true; }
        }
    }
    if (found) hit = o + d * bestT;
    return found;
}

// ---------------------------------------------------------------- level + entity renderer ----
struct LevelRenderer {
    struct Opts { bool useTex = true, useLm = true, wire = false, showNoTex = true, showEnts = true, animate = true; };
    std::string assets;
    TexLib lib;
    vb::EntityLibrary elib;
    vb::AnimLibrary alib;
    std::unordered_map<std::string, std::string> texIndex;
    gm::TexCache mtex;
    GLuint world = 0, lines = 0, modelProg = 0;
    std::map<std::string, std::unique_ptr<gm::GpuModel>> modelCache;      // key: g3d path | texture override | hidden groups
    std::vector<float> skinMats, boneWorld;       // boneWorld: model-space matrices of the bones of the last posed actor
    gm::Fog fog;                                        // fog of war (off unless the game turns it on)

    explicit LevelRenderer(const std::string& a) : assets(a), lib(a), elib(a), alib(a), texIndex(gm::indexTextures(a)), mtex(texIndex) {
        world = program(kWorldVS, kWorldFS); lines = program(kLineVS, kLineFS); modelProg = program(gm::kModelVS, gm::kModelFS);
    }

    gm::GpuModel* getModel(const vb::ModelPart& p) {
        std::string key = p.g3d_path + "|" + p.texture; for (auto& h : p.hideGroups) key += "|-" + h;
        auto it = modelCache.find(key);
        if (it != modelCache.end()) return it->second.get();
        std::unique_ptr<gm::GpuModel> m;
        try {
            vb::Mesh mesh = vb::parseG3D(p.g3d_path);
            m = std::make_unique<gm::GpuModel>(); std::string err;
            std::string stem = fs::path(p.g3d_path).stem().string();
            if (stem.rfind("WP_", 0) == 0 && mesh.skinned()) {              // a held weapon: its muzzle-flash / brass effect shapes (bound to the FX_* bones) are not part of the gun
                auto fx = [&](uint16_t v) { int b = mesh.vertices[v].bone[0]; return b >= 0 && (size_t)b < mesh.bones.size() && mesh.bones[(size_t)b].rfind("FX_", 0) == 0; };
                mesh.faces.erase(std::remove_if(mesh.faces.begin(), mesh.faces.end(), [&](const vb::Face& f) { return fx(f.vert[0]) || fx(f.vert[1]) || fx(f.vert[2]); }), mesh.faces.end());
            }
            std::vector<int> bmap;
            if (const vb::GrSkeleton* sk = mesh.skinned() ? alib.skeletonFor(stem) : nullptr) {
                bmap.assign(mesh.bones.size(), -1); bool any = false;
                for (size_t b = 0; b < mesh.bones.size(); b++) for (size_t k = 0; k < sk->bones.size() && k < 96; k++) if (sk->bones[k].name == mesh.bones[b]) { bmap[b] = (int)k; any = true; break; }
                if (!any) bmap.clear();
            }
            if (!gm::upload(*m, mesh, stem, p.texture, mtex, false, true, err, bmap, p.hideGroups)) { std::fprintf(stderr, "%s: %s\n", p.g3d_path.c_str(), err.c_str()); m.reset(); }
        } catch (std::exception& e) { std::fprintf(stderr, "%s: %s\n", p.g3d_path.c_str(), e.what()); }
        return (modelCache[key] = std::move(m)).get();
    }

    // Column-major model matrix for yaw (about Y), pitch (X), roll (Z) and a translation.
    static void modelMatrix(float* M, float yaw, float pitch, float roll, float x, float y, float z) {
        float cy = std::cos(yaw), sy = std::sin(yaw), cx = std::cos(pitch), sx = std::sin(pitch), cz = std::cos(roll), sz = std::sin(roll);
        float Rx[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}}, Ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}, Rz[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
        auto mm = [](float A[3][3], float B[3][3], float C[3][3]) { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { C[i][j] = 0; for (int k = 0; k < 3; k++) C[i][j] += A[i][k] * B[k][j]; } };
        float T[3][3], R[3][3]; mm(Rx, Rz, T); mm(Ry, T, R);
        for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) M[c * 4 + r] = R[r][c];
        M[3] = M[7] = M[11] = 0; M[12] = x; M[13] = y; M[14] = z; M[15] = 1;
    }

    void buildEntities(Level& Lv) {
        Lv.ents.clear();
        if (!Lv.haveMap) return;
        for (size_t mi = 0; mi < Lv.map.entities.size(); mi++) {
            const vb::MapEntity& e = Lv.map.entities[mi];
            Level::EntDraw d; d.mapIdx = (int)mi;
            std::string baseStem;
            for (const vb::ModelPart& p : elib.parts(e)) if (gm::GpuModel* g = getModel(p)) { if (d.models.empty()) baseStem = fs::path(p.g3d_path).stem().string(); d.models.push_back(g); }
            if (d.models.empty()) continue;
            bool anySkinned = false; for (auto* g : d.models) anySkinned |= g->skinned;
            if (anySkinned) {                       // characters/creatures: idle animation, desynchronised by position
                d.skel = alib.skeletonFor(baseStem);
                for (const std::string& n : alib.animationsFor(baseStem)) {
                    std::string l = n; for (auto& c : l) c = (char)std::tolower((unsigned char)c);
                    if (l.find("_com_una_walk_01") != std::string::npos) d.walk = alib.animation(n);
                    else if (l.find("_com_una_run_01") != std::string::npos) d.run = alib.animation(n);
                    else if (l.find("_com_una_laydown_proneb_01") != std::string::npos) d.lay = alib.animation(n);
                    else if (l.find("_proneb_una_idle_01") != std::string::npos) d.lying = alib.animation(n);
                    else if (l.find("_proneb_una_to_com_01") != std::string::npos || l.find("_proneb_1h_to_com_01") != std::string::npos) d.getup = alib.animation(n);
                    else if (l.find("_proneb_una_twitch_01") != std::string::npos) d.twitch = alib.animation(n);          // the broken eye bot
                    else if (l.find("_com_1h_maintenance") != std::string::npos) d.maint = alib.animation(n);             // the eye bot repairing something
                    else if (!d.walk && (l.find("_com_1h_walk_01") != std::string::npos)) d.walk = alib.animation(n);
                    else if (l.find("_proneb_una_death_01") != std::string::npos) d.death = alib.animation(n);
                    else if (l.find("_open_in_01") != std::string::npos) d.open = alib.animation(n);
                    else if (l.find("_com_2hh_idle_01") != std::string::npos) d.idle2 = alib.animation(n);
                    else if (l.find("_def_2hh_idle_01") != std::string::npos) d.idle2d = alib.animation(n);      // relaxed stance out of combat
                    else if (l.find("_com_2hh_walk_01") != std::string::npos) d.walk2 = alib.animation(n);
                    else if (l.find("_com_2hh_gun_shoot_01") != std::string::npos) d.atk2 = alib.animation(n);
                    else if (l.find("_com_2hh_dmgfr_01") != std::string::npos) d.hit2 = alib.animation(n);
                    else if (l.find("_com_1h_gun_shoot_01") != std::string::npos && l.find("side") == std::string::npos) d.atkGun = alib.animation(n);
                    else if (l.find("_com_1h_melee_slash_01") != std::string::npos) d.atkMelee = alib.animation(n);
                    else if (l.find("_com_una_punch_jab_01") != std::string::npos || l.find("_com_una_attack1_01") != std::string::npos || l.find("_com_1h_attack1_01") != std::string::npos) d.atkUna = alib.animation(n);
                    else if (l.find("_com_una_dmgfr_01") != std::string::npos || l.find("_com_1h_dmgfr_01") != std::string::npos) d.hit = alib.animation(n);
                }
                std::string idle = alib.idleFor(baseStem);
                if (!idle.empty()) d.anim = alib.animation(idle);
                d.phase = std::fmod(e.pos[0] * 0.37f + e.pos[2] * 0.61f, 3.f);
            }
            float py = e.pos[1];
            if (d.skel) { float gy; if (groundHeight(Lv.tre, e.pos[0], e.pos[2], e.pos[1], 2.f, gy)) py = gy; }      // characters/creatures stand on the floor
            modelMatrix(d.M, e.rot[0], e.rot[1], e.rot[2], e.pos[0], py, e.pos[2]);   // rot triple = (yaw, pitch, roll); in practice only yaw is non-zero
            if (std::getenv("VB_DEBUG_ENTS")) {
                float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                for (auto* g : d.models) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], g->center[k] - g->size[k] / 2); hi[k] = std::max(hi[k], g->center[k] + g->size[k] / 2); }
                std::fprintf(stderr, "ENT %-44s model %-32s pos %.1f %.1f %.1f  local bbox (%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n", e.file.c_str(), baseStem.c_str(), d.M[12], d.M[13], d.M[14], lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
            }
            Lv.ents.push_back(d);
        }
    }

    // World geometry (all octree batches).
    void drawWorld(const Level& L, const float* VP, const Opts& o) {
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, o.wire ? GL_LINE : GL_FILL);
        glUseProgram(world);
        fog.apply(world, true);
        glUniformMatrix4fv(glGetUniformLocation(world, "VP"), 1, GL_FALSE, VP);
        glUniform1i(glGetUniformLocation(world, "tex"), 0); glUniform1i(glGetUniformLocation(world, "lm"), 1);
        GLint uHasTex = glGetUniformLocation(world, "hasTex"), uHasLm = glGetUniformLocation(world, "hasLm"),
              uUseVC = glGetUniformLocation(world, "useVC"), uCut = glGetUniformLocation(world, "alphaCut");
        glUniform1i(uUseVC, 1);
        GLint uMul = glGetUniformLocation(world, "alphaMul"), uCutY = glGetUniformLocation(world, "cutY");
        if (const char* cu = std::getenv("VB_CULL")) { glEnable(GL_CULL_FACE); glFrontFace(cu[0] == 'c' && cu[1] == 'w' ? GL_CW : GL_CCW); } else glDisable(GL_CULL_FACE);
        bool wasBlend = false;
        for (int pass = 0; pass < 2; pass++)                                  // pass 1: the top stories that are fading (translucent, after everything else)
        for (auto& b : L.batches) {
            float fade = b.roofOf ? L.bldFade[b.roofOf] : 1.f;
            if ((pass == 1) != (fade > 0.004f && fade < 0.999f)) continue;
            if (b.noTexture && !o.showNoTex) continue;
            { static int hm = std::getenv("VB_HIDEFLAG") ? (int)std::strtol(std::getenv("VB_HIDEFLAG"), nullptr, 16) : 0; if (hm && (b.flag & hm)) continue; }
            bool bl = b.blend || pass == 1;
            if (bl != wasBlend) {
                wasBlend = bl;
                if (bl) { glEnable(GL_BLEND); glDepthMask(GL_FALSE); } else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
            }
            if (bl) glBlendFunc(GL_SRC_ALPHA, b.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            glUniform1f(uCut, bl || b.roofOf ? 0.02f : 0.0f); glUniform1f(uMul, fade); glUniform1f(uCutY, b.cutY);
            glUniform1i(uHasTex, o.useTex && b.tex); glUniform1i(uHasLm, o.useLm && b.lm);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, b.tex);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, b.lm);
            glBindVertexArray(b.vao);
            glDrawElements(GL_TRIANGLES, b.count, GL_UNSIGNED_INT, nullptr);
        }
        glUniform1f(uMul, 1.f); glUniform1f(uCutY, 1e9f);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND); glActiveTexture(GL_TEXTURE0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // Model program state shared by all actors in a frame.
    void beginModels(const float* VP) {
        glUseProgram(modelProg);
        fog.apply(modelProg, false);                   // actors are only seen while inside the lit disc
        glUniformMatrix4fv(glGetUniformLocation(modelProg, "VP"), 1, GL_FALSE, VP);
        glUniform1i(glGetUniformLocation(modelProg, "tex"), 0); glUniform1i(glGetUniformLocation(modelProg, "showNormals"), 0);
        glUniform2f(glGetUniformLocation(modelProg, "uvFlip"), 0, 0); glUniform1f(glGetUniformLocation(modelProg, "ambient"), 0.45f);
        glUniform3f(glGetUniformLocation(modelProg, "light"), 0.4f, 1.f, 0.3f); glUniform4f(glGetUniformLocation(modelProg, "fxMul"), 1.f, 1.f, 1.f, 1.f);
        glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glActiveTexture(GL_TEXTURE0);
    }
    // Fog treatment of the models drawn next: hide = people / creatures / loose items vanish outside the lit disc; otherwise (doors, containers, props) they are lit like the map itself
    // (dim in the tech-demo style, remembered where explored in the other style).
    void setActorFog(bool hide) { glUniform1i(glGetUniformLocation(modelProg, "fogHide"), hide); glUniform1i(glGetUniformLocation(modelProg, "fogMem"), !hide && fog.maskTex != 0); }
    void drawActor(const std::vector<gm::GpuModel*>& models, const float* M, const vb::GrSkeleton* skel, const vb::GrAnimation* anim, float animTime, bool useTex) {
        GLint uM = glGetUniformLocation(modelProg, "M"), uH = glGetUniformLocation(modelProg, "hasTex"), uA = glGetUniformLocation(modelProg, "useAlpha"), uT = glGetUniformLocation(modelProg, "tint");
        GLint uVA = glGetUniformLocation(modelProg, "vertAlpha"), uSk = glGetUniformLocation(modelProg, "skinned"), uBones = glGetUniformLocation(modelProg, "bones");
        glUniformMatrix4fv(uM, 1, GL_FALSE, M);
        bool posed = skel != nullptr;
        if (posed) {
            vb::poseSkeleton(*skel, anim, animTime, skinMats, &boneWorld);
            glUniformMatrix4fv(uBones, (GLsizei)std::min<size_t>(96, skel->bones.size()), GL_FALSE, skinMats.data());
        }
        for (gm::GpuModel* gmd : models) {
            glUniform1i(uSk, posed && gmd->skinned);
            glBindVertexArray(gmd->vao);
            for (const gm::Group& g : gmd->groups) {
                glUniform1i(uH, useTex && g.hasTex); glUniform1i(uA, g.useAlpha); glUniform1i(uVA, g.vertAlpha); glUniform4fv(uT, 1, g.tint);
                glBindTexture(GL_TEXTURE_2D, g.tex);
                if (g.vertAlpha) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }       // hair, beard, stubble: translucent over the head
                glDrawElements(GL_TRIANGLES, g.count, GL_UNSIGNED_INT, (void*)(intptr_t)(g.first * 4));
                if (g.vertAlpha) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
            }
        }
    }
    void drawEntities(const Level& L, const float* VP, const Opts& o, float time) {
        if (!o.showEnts || L.ents.empty()) return;
        beginModels(VP);
        for (auto& en : L.ents) drawActor(en.models, en.M, en.skel, o.animate ? en.anim : nullptr, time + en.phase, o.useTex);
    }
    // Translucent collision-grid overlay (C) and debug markers (M).
    void drawCollisionOverlay(const Level& L, const float* VP) {
        if (!L.haveCol) return;
        glUseProgram(world); glUniformMatrix4fv(glGetUniformLocation(world, "VP"), 1, GL_FALSE, VP);
        glUniform1i(glGetUniformLocation(world, "fogOn"), 0);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); glDisable(GL_DEPTH_TEST);   // overlay always visible
        glUniform1i(glGetUniformLocation(world, "useVC"), 0); glUniform1i(glGetUniformLocation(world, "hasTex"), 1); glUniform1i(glGetUniformLocation(world, "hasLm"), 0); glUniform1f(glGetUniformLocation(world, "alphaCut"), 0.f);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, L.colTex); glBindVertexArray(L.qvao);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
    }
    void drawMarkers(const Level& L, const float* VP) {
        if (!L.lineVerts) return;
        glUseProgram(lines); glUniformMatrix4fv(glGetUniformLocation(lines, "VP"), 1, GL_FALSE, VP);
        glBindVertexArray(L.lvao); glDrawArrays(GL_LINES, 0, L.lineVerts);
    }
};

#pragma once
// Shared GPU model support for the viewers: texture cache, G3D mesh upload (grouped by material), and the lit/textured model shader.
#include "assets/vfs.h"
#include "assets/g3d.h"
#include "assets/tga.h"
#include <GL/glew.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace gm {

inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// lowercase basename (no extension) -> path of every .tga under `root`
inline std::unordered_map<std::string, std::string> indexTextures(const std::string& root) {
    std::unordered_map<std::string, std::string> idx;
    for (const std::string& p : vb::vfs::walk(root)) {
        std::filesystem::path fp(p); if (lower(fp.extension().string()) == ".tga") idx.emplace(lower(fp.stem().string()), p);
    }
    return idx;
}

struct TexInfo { GLuint id = 0; int w = 0, h = 0; };

// Loads textures by name (path and extension ignored, so "Skin_HumM_default_LG.dds" finds the extracted .tga), with caching.
class TexCache {
public:
    explicit TexCache(const std::unordered_map<std::string, std::string>& index) : index_(index) {}
    TexInfo get(std::string name, bool* missing = nullptr, bool clamp = false) {
        name = lower(name);
        size_t sl = name.find_last_of("/\\"); if (sl != std::string::npos) name = name.substr(sl + 1);
        size_t dot = name.rfind('.'); if (dot != std::string::npos) name = name.substr(0, dot);
        auto c = cache_.find(name); if (c != cache_.end()) { if (!c->second.id && missing) *missing = true; return c->second; }
        TexInfo ti;
        auto it = index_.find(name); std::string path = it != index_.end() ? it->second : std::string(); if (path.empty()) { auto ex = extra_.find(name); if (ex != extra_.end()) path = ex->second; }
        vb::Texture t; std::string err;
        if (!path.empty() && vb::loadTga(path, t, err)) {
            glGenTextures(1, &ti.id); glBindTexture(GL_TEXTURE_2D, ti.id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            GLenum wrap = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
            ti.w = t.width; ti.h = t.height;
        } else if (missing) *missing = true;
        cache_[name] = ti;
        return ti;
    }
    void addFile(const std::string& name, const std::string& path) { extra_[lower(name)] = path; }       // a texture outside the game files (the port's own data)
private:
    std::unordered_map<std::string, std::string> extra_;
    const std::unordered_map<std::string, std::string>& index_;
    std::unordered_map<std::string, TexInfo> cache_;
};

struct Group { GLuint tex = 0; bool hasTex = false, useAlpha = false, vertAlpha = false; int first = 0, count = 0; float tint[4] = {0.7f, 0.7f, 0.7f, 1}; };

struct GpuModel {
    vb::Mesh mesh;
    GLuint vao = 0, vbo = 0, ebo = 0;
    std::vector<Group> groups;
    float center[3] = {0, 0, 0}, size[3] = {0, 0, 0}, radius = 1;
    int missingTex = 0; bool atlasApplied = false;
    bool skinned = false;               // vertex buffer carries skeleton bone indices + weights (attributes 4/5)
    void destroy() {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (ebo) glDeleteBuffers(1, &ebo);
        vao = vbo = ebo = 0; groups.clear();
    }
};

// Upload `mesh` (copied into M.mesh). `stem` is the file's base name (used for the human-critter atlas rule); `texOverride`, if
// non-empty, replaces every material's diffuse texture (skins, hair colours). swapYZ: debug Y/Z swap.
// boneMap: for skinned meshes, mesh bone index -> skeleton bone index (-1 = missing); empty = draw unskinned.
inline bool upload(GpuModel& M, const vb::Mesh& mesh, const std::string& stem, const std::string& texOverride, TexCache& tc, bool swapYZ, bool atlas, std::string& err, const std::vector<int>& boneMap = {}, const std::vector<std::string>& hideGroups = {}) {
    M.destroy();
    M.mesh = mesh;
    vb::Mesh& m = M.mesh;
    M.atlasApplied = false; M.missingTex = 0;
    auto textureOf = [&](size_t g) {
        if (!texOverride.empty()) return texOverride;
        std::string tn = g < m.materials.size() ? m.materials[g].texture_filename : "";
        return tn.empty() ? m.texture_filename : tn;
    };
    if (atlas && !m.vertices.empty()) {          // shared 512x512 character atlas (vb::humanAtlasRect)
        std::string tn = textureOf(0);
        TexInfo ti = tn.empty() ? TexInfo{} : tc.get(tn);
        vb::UVRect r;
        if (ti.id && vb::humanAtlasRect(stem, m, ti.w, ti.h, r)) {
            for (vb::Vertex& vx : m.vertices) { vx.uv[0] = (vx.uv[0] - r.u0) / r.su; vx.uv[1] = (vx.uv[1] - r.v0) / r.sv; }
            M.atlasApplied = true;
        }
    }
    if (m.vertices.empty()) { err = "no vertices"; return false; }
    M.skinned = !boneMap.empty();
    std::vector<float> v(m.vertices.size() * 20);
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < m.vertices.size(); i++) {
        const vb::Vertex& s = m.vertices[i];
        float* o = &v[i * 20];
        o[0] = s.pos[0]; o[1] = swapYZ ? s.pos[2] : s.pos[1]; o[2] = swapYZ ? s.pos[1] : s.pos[2];
        o[3] = s.norm[0]; o[4] = swapYZ ? s.norm[2] : s.norm[1]; o[5] = swapYZ ? s.norm[1] : s.norm[2];
        o[6] = s.uv[0]; o[7] = s.uv[1];
        o[8] = s.color[0]; o[9] = s.color[1]; o[10] = s.color[2]; o[11] = s.color[3];
        for (int k = 0; k < 4; k++) {
            int bi = s.bone[k] >= 0 && (size_t)s.bone[k] < boneMap.size() ? boneMap[(size_t)s.bone[k]] : -1;
            o[12 + k] = bi >= 0 ? (float)bi : 0.f; o[16 + k] = bi >= 0 ? s.weight[k] : 0.f;
        }
        for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], o[k]); hi[k] = std::max(hi[k], o[k]); }
    }
    for (int k = 0; k < 3; k++) { M.size[k] = hi[k] - lo[k]; M.center[k] = (lo[k] + hi[k]) / 2; }
    M.radius = 0.5f * std::sqrt(M.size[0] * M.size[0] + M.size[1] * M.size[1] + M.size[2] * M.size[2]);
    if (M.radius <= 0) M.radius = 1;

    size_t nmat = std::max<size_t>(1, m.materials.size());
    std::vector<std::vector<uint32_t>> byMat(nmat);
    for (const vb::Face& f : m.faces) {
        size_t g = f.mat < nmat ? f.mat : 0;
        byMat[g].insert(byMat[g].end(), {f.vert[0], f.vert[1], f.vert[2]});
    }
    std::vector<uint32_t> idx;
    for (size_t g = 0; g < nmat; g++) {
        if (byMat[g].empty()) continue;
        if (g < m.materials.size() && std::find(hideGroups.begin(), hideGroups.end(), m.materials[g].name) != hideGroups.end()) continue;   // covered by clothing
        Group gr; gr.first = (int)idx.size(); gr.count = (int)byMat[g].size();
        idx.insert(idx.end(), byMat[g].begin(), byMat[g].end());
        std::string tn = textureOf(g);
        if (!tn.empty()) {
            bool miss = false;
            gr.tex = tc.get(tn, &miss).id;
            if (miss && !texOverride.empty()) {            // unknown override (editor placeholder etc.): fall back to the mesh's own texture
                std::string own = g < m.materials.size() ? m.materials[g].texture_filename : "";
                if (own.empty()) own = m.texture_filename;
                if (!own.empty()) { miss = false; gr.tex = tc.get(own, &miss).id; tn = own; }
            }
            gr.hasTex = gr.tex != 0;
            if (miss) { M.missingTex++; std::fprintf(stderr, "texture not found: %s\n", tn.c_str()); }
        }
        if (g < m.materials.size()) {
            for (int k = 0; k < 4; k++) gr.tint[k] = m.materials[g].diffuse[k];
            const std::string& b = m.materials[g].blend_name;
            gr.vertAlpha = m.materials[g].shader.find("VERTEX_ALPHA") != std::string::npos;     // hair / beard / stubble: transparency comes from the vertex alpha (x2)
            gr.useAlpha = !b.empty() && b != "OPAQUE";     // texture alpha is transparency only for non-opaque materials
        }
        if (gr.tint[3] < 0.01f) gr.tint[3] = 1;
        M.groups.push_back(gr);
    }
    glGenVertexArrays(1, &M.vao); glBindVertexArray(M.vao);
    glGenBuffers(1, &M.vbo); glBindBuffer(GL_ARRAY_BUFFER, M.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 80, (void*)0);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 80, (void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 80, (void*)24); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 80, (void*)32); glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 80, (void*)48); glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 80, (void*)64); glEnableVertexAttribArray(5);
    glGenBuffers(1, &M.ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, M.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
    return true;
}

// ---- fog of war (shared by the world and model fragment shaders) ----
// visibility = 1 inside `fogRadii.x` of fogCenter (ground plane XZ), fading to `fogFloor` at `fogRadii.y`; with fogMem the explored-mask texture (unit 2, one texel per
// 0.5 world units over fogExtent from fogOrigin) keeps previously seen ground visible at fogDim brightness. Two styles (see vanburen): the tech demo's (whole map at
// fogFloor brightness, disc following the player, actors dimmed) and 'remembered' (black beyond, explored ground at fogDim, actors hidden outside the disc).
static const char* kFogGLSL = R"(
uniform int fogOn; uniform int fogMem; uniform int fogHide; uniform float fogFloor; uniform vec2 fogCenter; uniform vec2 fogRadii; uniform sampler2D fogMask; uniform vec2 fogOrigin; uniform vec2 fogExtent; uniform float fogDim;
uniform int fogLitOn; uniform sampler2D fogLit; uniform vec2 fogLitOrigin; uniform vec2 fogLitExtent; uniform float fogLightY;
// the player's light: a sphere (3D distance from the light point) that the surroundings cut into (the occlusion mask `fogLit`: lines of sight over the collision grid)
float fogLight(vec2 w, float h) {
  float dy = h - fogLightY; float d = sqrt(dot(w - fogCenter, w - fogCenter) + dy * dy);
  float v = 1.0 - smoothstep(fogRadii.x, fogRadii.y, d);
  if (fogLitOn != 0) v *= texture(fogLit, (w - fogLitOrigin) / fogLitExtent).r;
  return v;
}
float fogDisc(vec2 w, float h) { return fogOn == 0 ? 1.0 : fogLight(w, h); }   // the lit field alone (actors: no floor, no memory)
float fogVisibility(vec2 w, float h) {
  if (fogOn == 0) return 1.0;
  float v = fogLight(w, h);
  if (fogMem != 0) v = max(v, texture(fogMask, (w - fogOrigin) / fogExtent).r * fogDim);
  return max(v, fogFloor);
}
)";
struct Fog {
    bool on = false;
    float cx = 0, cz = 0, r0 = 20.5f, r1 = 26.f;      // visible disc around the party (soft edge between r0 and r1)
    GLuint maskTex = 0; float ox = 0, oz = 0, ex = 1, ez = 1;   // explored mask placement
    GLuint litTex = 0; float lox = 0, loz = 0, lex = 1, lez = 1, cy = 4.f;   // occlusion field of the light (lightfield.h) and the light point's height
    float dim = 0.5f;                                // brightness of explored-but-not-visible ground ('remembered' style)
    float floorBright = 0.f;                         // minimum brightness everywhere (tech-demo style: whole map dimmed instead of black)
    bool hideActors = true;                          // actors fade out/vanish outside the disc in both styles (the map itself uses floorBright / memory)
    void apply(GLuint prog, bool useMemory) const {
        glUniform1i(glGetUniformLocation(prog, "fogOn"), on); glUniform1i(glGetUniformLocation(prog, "fogMem"), useMemory && maskTex != 0);
        if (!on) return;
        glUniform2f(glGetUniformLocation(prog, "fogCenter"), cx, cz); glUniform2f(glGetUniformLocation(prog, "fogRadii"), r0, r1);
        glUniform2f(glGetUniformLocation(prog, "fogOrigin"), ox, oz); glUniform2f(glGetUniformLocation(prog, "fogExtent"), ex, ez); glUniform1f(glGetUniformLocation(prog, "fogDim"), dim);
        glUniform1f(glGetUniformLocation(prog, "fogFloor"), floorBright); glUniform1i(glGetUniformLocation(prog, "fogHide"), hideActors);
        glUniform1i(glGetUniformLocation(prog, "fogLitOn"), litTex != 0); glUniform1f(glGetUniformLocation(prog, "fogLightY"), cy);
        if (litTex) { glUniform2f(glGetUniformLocation(prog, "fogLitOrigin"), lox, loz); glUniform2f(glGetUniformLocation(prog, "fogLitExtent"), lex, lez); glUniform1i(glGetUniformLocation(prog, "fogLit"), 3); glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, litTex); glActiveTexture(GL_TEXTURE0); }
        glUniform1i(glGetUniformLocation(prog, "fogMask"), 2);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, maskTex); glActiveTexture(GL_TEXTURE0);
    }
};

// Model shader: world position = M * pos; clip = VP * world. Two-sided lambert from a world-space light direction.
static const char* kModelVS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUV; layout(location=3) in vec4 aCol;
layout(location=4) in vec4 aBone; layout(location=5) in vec4 aWeight;
uniform float outline = 0.0; uniform mat4 VP; uniform mat4 M; uniform vec2 uvFlip; uniform vec3 uvRot; uniform int skinned; uniform mat4 bones[96];
out vec3 vN; out vec2 vUV; out vec4 vCol; out vec2 vWorld; out float vH;
void main(){
  vec4 p = vec4(aPos,1.0); vec3 n = aNrm;
  if (skinned != 0) {
    vec4 sp = vec4(0.0); vec3 sn = vec3(0.0); float ws = 0.0;
    for (int k = 0; k < 4; k++) { float w = aWeight[k]; if (w > 0.0) { mat4 B = bones[int(aBone[k])]; sp += w * (B * p); sn += w * (mat3(B) * aNrm); ws += w; } }
    if (ws > 0.0) { p = vec4(sp.xyz / ws, 1.0); n = sn / ws; }
  }
  vN = mat3(M) * n; vUV = vec2(uvFlip.x>0.5?1.0-aUV.x:aUV.x, uvFlip.y>0.5?1.0-aUV.y:aUV.y); if (uvRot.z != 0.0) { vec2 d = vUV - uvRot.xy; float cs = cos(uvRot.z), sn = sin(uvRot.z); vUV = uvRot.xy + vec2(cs * d.x - sn * d.y, sn * d.x + cs * d.y); }   // texture rotation about a centre (the selection discs spin their picture)
  vCol = aCol; vec4 wp = M * p; if (outline > 0.0) wp.xyz += outline * normalize(mat3(M) * n); vWorld = wp.xz; vH = wp.y; gl_Position = VP * wp;
})";
static const std::string kModelFSsrc = std::string("#version 330 core\n") + kFogGLSL + R"(in vec3 vN; in vec2 vUV; in vec4 vCol; in vec2 vWorld; in float vH; out vec4 frag;
uniform sampler2D tex; uniform int hasTex; uniform int showNormals; uniform vec3 light; uniform vec4 tint; uniform int useAlpha; uniform int vertAlpha; uniform float ambient; uniform vec4 fxMul; uniform int lumaOnly; uniform vec4 flatCol = vec4(0.0);
void main(){
  vec3 n = normalize(vN);
  if (showNormals != 0) { frag = vec4(n*0.5+0.5, 1.0); return; }
  vec4 c = hasTex != 0 ? texture(tex, vUV) : tint;
  if (lumaOnly != 0) { float l = dot(c.rgb, vec3(0.3, 0.59, 0.11)); c.rgb = vec3(0.4 + 1.2 * l); }   // recoloured model: keeps only the texture's light and dark
  if (useAlpha == 0) c.a = 1.0;
  if (vertAlpha != 0) c.a *= min(1.0, vCol.a * 2.0);
  if (c.a < 0.02) discard;
  float fv = fogHide != 0 ? fogDisc(vWorld, vH) : fogVisibility(vWorld, vH);
  if (fogOn != 0 && fogHide != 0 && fv < 0.02) discard;   // actors (people, creatures, props, doors) vanish outside the lit disc; only the static map stays dimly visible
  if (flatCol.a > 0.0) { frag = vec4(flatCol.rgb, 1.0); return; }
  float d = abs(dot(n, normalize(light)));
  frag = vec4(c.rgb * (ambient + (1.0 - ambient) * d) * fv * fxMul.rgb, c.a * fxMul.a);
})";;
static const char* kModelFS = kModelFSsrc.c_str();


} // namespace gm

// vb_model — Van Buren G3D model viewer with a model selector.
//
//   vb_model                   open the model selector (all .g3d files, grouped by folder)
//   vb_model <name|file.g3d>   open a model directly (Tab returns to the selector)
//   vb_model -list [substr]    print matching model paths
//
// Selector: Up/Down/PgUp/PgDn/Home/End or mouse wheel/click, type to filter (e.g. "items/" or "revolver"), Enter to open, Esc to quit.
// Viewer:   left-drag orbit, wheel zoom, Space auto-spin, R reset view, [ / ] previous/next model in the same folder,
//           T textures, W wireframe, C culling, Y swap Y/Z, N normals, H HUD, F1 help, Tab/Esc selector.
// Options:  -assets DIR  -shot out.bmp  -yaw deg  -pitch deg  -dist d  -size W H  -swapyz  -cull  -anim substr  -time sec (freeze)
//
// Textures: each material group's diffuse texture is resolved against the extracted .tga assets (see G3D_FORMAT_FINDINGS.md).

#include "assets/g3d.h"
#include "assets/anim.h"
#include "assets/tga.h"
#include "common/ui.h"
#include "common/gmodel.h"
#include "common/gamedir.h"
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

static std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// ---- tiny matrix helpers (column-major) ----
using Mat4 = float[16];
static void mul(Mat4 out, const Mat4 a, const Mat4 b) {
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) { float s = 0; for (int i = 0; i < 4; i++) s += a[i*4+r] * b[c*4+i]; out[c*4+r] = s; }
}
static void perspective(Mat4 m, float fovyDeg, float aspect, float n, float f) {
    float t = 1.f / std::tan(fovyDeg * 3.14159265f / 360.f);
    std::memset(m, 0, 64);
    m[0] = t / aspect; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1.f; m[14] = 2.f * f * n / (n - f);
}
struct V3 { float x, y, z; };
static V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { float l = std::sqrt(dot(a, a)); return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a; }
// Left-handed view (the engine is Direct3D: see MAP_FORMAT_FINDINGS.md "Handedness").
static void lookAt(Mat4 m, V3 eye, V3 target) {
    V3 f = norm(sub(target, eye)), s = norm(cross({0, 1, 0}, f)), u = cross(f, s);
    m[0] = s.x; m[4] = s.y; m[8] = s.z;  m[12] = -dot(s, eye);
    m[1] = u.x; m[5] = u.y; m[9] = u.z;  m[13] = -dot(u, eye);
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = dot(f, eye);
    m[3] = m[7] = m[11] = 0; m[15] = 1;
}

// ---- asset lookup ----
struct Assets {
    std::string root;
    std::unordered_map<std::string, std::string> tga;      // lowercase basename (no ext) -> path
    std::vector<std::string> g3d;                          // sorted paths
    explicit Assets(const std::string& r) : root(r) {
        for (const std::string& p : vb::vfs::walk(r)) {
            std::string ext = lower(fs::path(p).extension().string());
            if (ext == ".tga") tga.emplace(lower(fs::path(p).stem().string()), p);
            else if (ext == ".g3d") g3d.push_back(p);
        }
        std::sort(g3d.begin(), g3d.end());
    }
};

struct Model {
    std::string path;
    gm::GpuModel g;
    const vb::GrSkeleton* skel = nullptr;       // skeleton the mesh skins to (nullptr = static mesh)
    std::vector<std::string> animNames;         // animations available for this model
    int animIdx = -1;                           // -1 = bind/rest pose
    float animTime = 0;
    void destroy() { g.destroy(); skel = nullptr; animNames.clear(); animIdx = -1; animTime = 0; }
};

struct Entry { std::string path, label; };    // label: path relative to the assets root, no extension

enum class Mode { Selector, Viewer };

int main(int argc, char** argv) {
    std::string assets, gameArg, arg, shot;
    float distArg = 2.6f;
    std::string animArg; float timeArg = 0; bool haveTime = false;
    bool list = false; float yaw = 35, pitch = 20; int W = 1920, H = 1080;
    bool atlas = true, flipU = false, flipV = false, swapYZ = false, tex = true, wire = false, cull = false, normals = false, hud = true, help = false, spin = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "-game" && i + 1 < argc) gameArg = argv[++i];
        else if (a == "-shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "-yaw" && i + 1 < argc) yaw = (float)std::atof(argv[++i]);
        else if (a == "-pitch" && i + 1 < argc) pitch = (float)std::atof(argv[++i]);
        else if (a == "-dist" && i + 1 < argc) distArg = (float)std::atof(argv[++i]);
        else if (a == "-size" && i + 2 < argc) { W = std::atoi(argv[++i]); H = std::atoi(argv[++i]); }
        else if (a == "-anim" && i + 1 < argc) animArg = argv[++i];
        else if (a == "-time" && i + 1 < argc) { timeArg = (float)std::atof(argv[++i]); haveTime = true; }
        else if (a == "-swapyz") swapYZ = true;
        else if (a == "-cull") cull = true;
        else if (a == "-noatlas") atlas = false;
        else if (a == "-flipu") flipU = true;
        else if (a == "-flipv") flipV = true;
        else if (a == "-list") list = true;
        else arg = a;
    }
    if (assets.empty() && !findAndMountGame(gameArg, assets)) return 1;       // (-assets DIR reads an extracted copy instead)
    if (!fs::exists(assets)) { std::fprintf(stderr, "assets dir not found: %s (use -game DIR)\n", assets.c_str()); return 1; }
    Assets A(assets);
    std::vector<Entry> entries;
    for (auto& p : A.g3d) {
        std::string rel = p.substr(std::min(p.size(), assets.size() + 1));
        rel = rel.substr(0, rel.size() - 4);   // strip ".g3d"
        entries.push_back({p, rel});
    }
    if (list) {
        std::string s = lower(arg);
        for (auto& e : entries) if (s.empty() || lower(e.label).find(s) != std::string::npos) std::printf("%s\n", e.path.c_str());
        return 0;
    }

    // direct open: an existing file, else the first (case-insensitive) match on the file name
    int initial = -1;
    if (!arg.empty()) {
        if (fs::exists(arg)) {
            std::string c = fs::canonical(arg).string();
            for (size_t i = 0; i < entries.size(); i++) if (fs::equivalent(entries[i].path, c)) { initial = (int)i; break; }
            if (initial < 0) { entries.push_back({c, fs::path(c).stem().string()}); initial = (int)entries.size() - 1; }
        } else {
            std::string s = lower(arg);
            for (size_t i = 0; i < entries.size(); i++) if (lower(fs::path(entries[i].path).filename().string()).find(s) != std::string::npos) { initial = (int)i; break; }
        }
        if (initial < 0) { std::fprintf(stderr, "no model matches '%s'\n", arg.c_str()); return 1; }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* win = SDL_CreateWindow("Van Buren model viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    glewExperimental = GL_TRUE; glewInit();
    SDL_GL_SetSwapInterval(shot.empty() ? 1 : 0);
    GLuint prog = program(gm::kModelVS, gm::kModelFS);
    Text text; text.init();
    gm::TexCache texCache(A.tga);
    vb::AnimLibrary anims(assets);
    std::vector<float> skinMats;
    bool playing = true;

    Model model; int cur = -1;
    std::string status;
    auto load = [&](int idx) -> bool {
        model.destroy();
        model.path = entries[idx].path; vb::Mesh mesh;
        try { mesh = vb::parseG3D(model.path); }
        catch (std::exception& e) { status = entries[idx].label + ": " + e.what(); return false; }
        std::string err;
        std::string stem = fs::path(model.path).stem().string();
        const vb::GrSkeleton* sk = mesh.skinned() ? anims.skeletonFor(stem) : nullptr;
        std::vector<int> bmap;
        if (sk) {
            bmap.assign(mesh.bones.size(), -1); bool any = false;
            for (size_t b = 0; b < mesh.bones.size(); b++) for (size_t k = 0; k < sk->bones.size() && k < 96; k++) if (sk->bones[k].name == mesh.bones[b]) { bmap[b] = (int)k; any = true; break; }
            if (!any) { sk = nullptr; bmap.clear(); }
        }
        if (!gm::upload(model.g, mesh, stem, "", texCache, swapYZ, atlas, err, bmap)) { status = entries[idx].label + ": " + err; return false; }
        model.skel = sk; model.animNames = sk ? anims.animationsFor(stem) : std::vector<std::string>(); model.animIdx = -1; model.animTime = 0;
        status.clear(); cur = idx;
        SDL_SetWindowTitle(win, ("Van Buren model viewer - " + entries[idx].label).c_str());
        return true;
    };

    // selector state
    Mode mode = Mode::Selector;
    std::string filter; std::vector<int> shown; int sel = 0, top = 0;
    const float rowH = 24;
    auto rowsFor = [&](int dh) { return std::max(1, (int)((dh - 110 - 70) / rowH)); };
    auto refilter = [&](int keep) {
        shown.clear();
        std::string f = lower(filter);
        for (int i = 0; i < (int)entries.size(); i++) if (f.empty() || lower(entries[i].label).find(f) != std::string::npos) shown.push_back(i);
        sel = 0;
        for (int k = 0; k < (int)shown.size(); k++) if (shown[k] == keep) sel = k;
        top = std::max(0, sel - 5);
    };
    refilter(-1);
    auto open = [&](int idx) { if (load(idx)) { mode = Mode::Viewer; if (!shot.empty()) return; } };
    if (initial >= 0) open(initial);

    if (initial >= 0 && !animArg.empty()) {
        for (size_t k = 0; k < model.animNames.size(); k++) if (lower(model.animNames[k]).find(lower(animArg)) != std::string::npos) { model.animIdx = (int)k; break; }
        if (haveTime) { model.animTime = timeArg; playing = false; }
    }
    float dist = distArg, spinAngle = 0; bool drag = false, done = false; int frame = 0;
    auto step = [&](int d) {     // browse neighbours in the same folder, skipping unparsable files
        std::string dir = fs::path(entries[cur].path).parent_path().string();
        int n = (int)entries.size();
        for (int k = 1; k < n; k++) {
            int j = ((cur + d * k) % n + n) % n;
            if (fs::path(entries[j].path).parent_path().string() != dir) continue;
            if (load(j)) return;
        }
    };
    Uint32 last = SDL_GetTicks();
    while (!done) {
        int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
        int rows = rowsFor(dh);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { done = true; continue; }
            if (mode == Mode::Selector) {
                if (e.type == SDL_TEXTINPUT) { filter += e.text.text; refilter(shown.empty() ? -1 : shown[sel]); }
                else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: if (!filter.empty()) { filter.clear(); refilter(shown.empty() ? -1 : shown[sel]); } else if (cur >= 0) mode = Mode::Viewer; else done = true; break;
                        case SDLK_BACKSPACE: if (!filter.empty()) { filter.pop_back(); refilter(shown.empty() ? -1 : shown[sel]); } break;
                        case SDLK_UP: sel = std::max(0, sel - 1); break;
                        case SDLK_DOWN: sel = std::min((int)shown.size() - 1, sel + 1); break;
                        case SDLK_PAGEUP: sel = std::max(0, sel - rows); break;
                        case SDLK_PAGEDOWN: sel = std::min((int)shown.size() - 1, sel + rows); break;
                        case SDLK_HOME: sel = 0; break;
                        case SDLK_END: sel = (int)shown.size() - 1; break;
                        case SDLK_RETURN: case SDLK_KP_ENTER: if (!shown.empty()) open(shown[sel]); break;
                    }
                    sel = std::max(0, sel);
                    if (sel < top) top = sel; if (sel >= top + rows) top = sel - rows + 1;
                } else if (e.type == SDL_MOUSEWHEEL) {
                    top = std::max(0, std::min(std::max(0, (int)shown.size() - rows), top - e.wheel.y * 3));
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int k = (int)((e.button.y - 110) / rowH) + top;
                    if (e.button.y >= 110 && k >= 0 && k < (int)shown.size()) { if (k == sel) open(shown[k]); else sel = k; }
                }
            } else {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) drag = true;
                else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) drag = false;
                else if (e.type == SDL_MOUSEMOTION && drag) { yaw += e.motion.xrel * 0.4f; pitch = std::max(-89.f, std::min(89.f, pitch + e.motion.yrel * 0.4f)); }
                else if (e.type == SDL_MOUSEWHEEL) dist = std::max(1.2f, std::min(20.f, dist * (e.wheel.y > 0 ? 0.9f : 1.1f)));
                else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: case SDLK_TAB: mode = Mode::Selector; drag = false; filter.clear(); refilter(cur);
                            if (sel < top) top = sel; if (sel >= top + rows) top = sel - rows + 1; break;
                        case SDLK_t: tex = !tex; break;
                        case SDLK_w: wire = !wire; break;
                        case SDLK_c: cull = !cull; break;
                        case SDLK_n: normals = !normals; break;
                        case SDLK_h: hud = !hud; break;
                        case SDLK_F1: help = !help; break;
                        case SDLK_SPACE: spin = !spin; break;
                        case SDLK_r: yaw = 35; pitch = 20; dist = 2.6f; break;
                        case SDLK_a: atlas = !atlas; if (cur >= 0) load(cur); break;
                        case SDLK_u: flipU = !flipU; break;
                        case SDLK_v: flipV = !flipV; break;
                        case SDLK_y: swapYZ = !swapYZ; if (cur >= 0) load(cur); break;
                        case SDLK_p: playing = !playing; break;
                        case SDLK_COMMA: case SDLK_PERIOD:
                            if (!model.animNames.empty()) {
                                int n = (int)model.animNames.size() + 1;       // slot 0 = rest pose
                                int slot = ((model.animIdx + 1) + (e.key.keysym.sym == SDLK_PERIOD ? 1 : -1) + n) % n;
                                model.animIdx = slot - 1; model.animTime = 0;
                            } break;
                        case SDLK_LEFTBRACKET: case SDLK_LEFT: step(-1); break;
                        case SDLK_RIGHTBRACKET: case SDLK_RIGHT: step(1); break;
                    }
                }
            }
        }
        Uint32 now = SDL_GetTicks(); float dt = (now - last) / 1000.f; last = now;
        if (spin && mode == Mode::Viewer) yaw += 40.f * dt;
        if (playing && mode == Mode::Viewer && model.animIdx >= 0) model.animTime += dt;
        (void)spinAngle;

        glViewport(0, 0, dw, dh);
        glClearColor(0.08f, 0.09f, 0.11f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (mode == Mode::Viewer && model.g.vao) {
            float yr = yaw * 3.14159265f / 180.f, pr = pitch * 3.14159265f / 180.f;
            V3 dir{std::sin(yr) * std::cos(pr), std::sin(pr), std::cos(yr) * std::cos(pr)};
            float s = 1.f / model.g.radius;           // normalise the model to a unit sphere around the origin
            V3 eye{dir.x * dist, dir.y * dist, dir.z * dist};
            Mat4 Pm, Vm, VP, Mm;
            perspective(Pm, 45.f, (float)dw / dh, 0.05f, 100.f); lookAt(Vm, eye, {0, 0, 0});
            std::memset(Mm, 0, 64); Mm[0] = Mm[5] = Mm[10] = s; Mm[15] = 1;
            Mm[12] = -model.g.center[0] * s; Mm[13] = -model.g.center[1] * s; Mm[14] = -model.g.center[2] * s;
            mul(VP, Pm, Vm);
            glUseProgram(prog);
            glUniformMatrix4fv(glGetUniformLocation(prog, "VP"), 1, GL_FALSE, VP);
            glUniformMatrix4fv(glGetUniformLocation(prog, "M"), 1, GL_FALSE, Mm);
            glUniform1f(glGetUniformLocation(prog, "ambient"), 0.35f);
            glUniform1i(glGetUniformLocation(prog, "tex"), 0);
            glUniform2f(glGetUniformLocation(prog, "uvFlip"), flipU, flipV);
            glUniform1i(glGetUniformLocation(prog, "showNormals"), normals);
            glUniform3f(glGetUniformLocation(prog, "light"), -dir.x, -dir.y, -dir.z);
            glEnable(GL_DEPTH_TEST);
            if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
            glBindVertexArray(model.g.vao);
            glUniform1i(glGetUniformLocation(prog, "skinned"), model.g.skinned && model.skel);
            if (model.g.skinned && model.skel) {
                const vb::GrAnimation* an = model.animIdx >= 0 ? anims.animation(model.animNames[(size_t)model.animIdx]) : nullptr;
                vb::poseSkeleton(*model.skel, an, model.animTime, skinMats);
                glUniformMatrix4fv(glGetUniformLocation(prog, "bones"), (GLsizei)std::min<size_t>(96, model.skel->bones.size()), GL_FALSE, skinMats.data());
            }
            for (const gm::Group& g : model.g.groups) {
                glUniform1i(glGetUniformLocation(prog, "hasTex"), tex && g.hasTex);
                glUniform1i(glGetUniformLocation(prog, "useAlpha"), g.useAlpha);
                glUniform4fv(glGetUniformLocation(prog, "tint"), 1, g.tint);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g.tex);
                glDrawElements(GL_TRIANGLES, g.count, GL_UNSIGNED_INT, (void*)(intptr_t)(g.first * 4));
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            if (hud || help) {
                text.begin(dw, dh);
                if (hud) {
                    char buf[512];
                    std::snprintf(buf, sizeof buf, "%s   %zu verts  %zu tris  %zu material groups   -yaw %.1f -pitch %.1f -dist %.2f", entries[cur].label.c_str(), model.g.mesh.vertices.size(), model.g.mesh.faces.size(), model.g.groups.size(),
                                  std::fmod(std::fmod(yaw, 360.f) + 360.f, 360.f), pitch, dist);
                    text.rect(0, 0, text.width(buf, 1.f) + 16, 26, 0, 0, 0, 0.55f); text.draw(8, 3, 1.f, 1, 1, 1, buf);
                    float y = 30;
                    for (size_t g = 0; g < model.g.mesh.materials.size() && g < 6; g++) {
                        const vb::Material& m = model.g.mesh.materials[g];
                        std::string t = "  " + (m.name.empty() ? std::string("(material)") : m.name) + "  ->  " + (m.texture_filename.empty() ? std::string("(no texture)") : m.texture_filename);
                        text.draw(8, y, 1.f, 0.75f, 0.9f, 1.f, t); y += 20;
                    }
                    std::snprintf(buf, sizeof buf, "size %.2f x %.2f x %.2f", model.g.size[0], model.g.size[1], model.g.size[2]);
                    text.draw(8, y, 1.f, 0.7f, 0.8f, 0.7f, buf); y += 20;
                    if (model.skel) {
                        const vb::GrAnimation* an = model.animIdx >= 0 ? anims.animation(model.animNames[(size_t)model.animIdx]) : nullptr;
                        char ab[256];
                        std::snprintf(ab, sizeof ab, "skeleton %s (%zu bones)   animation %d/%zu: %s%s   t=%.2f%s", model.skel->name.c_str(), model.skel->bones.size(), model.animIdx + 1, model.animNames.size(),
                                      model.animIdx >= 0 ? model.animNames[(size_t)model.animIdx].c_str() : "(rest pose)", an ? "" : "", model.animTime, playing ? "" : "  [paused]");
                        text.draw(8, y, 1.f, 0.9f, 0.8f, 1.f, ab); y += 20;
                    }
                    std::string flags = std::string(tex ? "" : "textures OFF ") + (wire ? "wireframe " : "") + (cull ? "culling " : "") + (normals ? "normals " : "") + (swapYZ ? "Y/Z swapped " : "") + (model.g.atlasApplied ? "char-atlas UV remap " : "") + (spin ? "spin " : "") + (model.g.missingTex ? "MISSING TEXTURES " : "");
                    if (!flags.empty()) text.draw(8, y, 1.f, 1, 0.9f, 0.4f, flags);
                    text.draw(8, dh - 24.f, 1.f, 0.8f, 0.8f, 0.8f, "Tab: models   [ ]: prev/next   F1: help   H: hide HUD", 0.8f);
                }
                if (help) {
                    const char* lines_[] = {"Controls", "", "Left-drag     orbit", "Mouse wheel   zoom", "Space         auto-spin", "R             reset view", "[ and ]       previous / next model in this folder",
                                            "T  textures   W  wireframe   C  culling", "N  normals     Y  swap Y/Z   A  character atlas remap", "U / V  flip texture U / V (debug)", ", and .    previous / next animation   P  pause", "H  HUD   F1  this help", "Tab / Esc     model selector"};
                    float w = 0; for (auto* l : lines_) w = std::max(w, text.width(l, 1.f));
                    float x = dw - w - 40, y = 40;
                    text.rect(x - 12, y - 8, w + 24, 13 * 22.f + 16, 0, 0, 0, 0.7f);
                    for (auto* l : lines_) { text.draw(x, y, 1.f, 1, 1, 1, l); y += 22; }
                }
                text.end();
            }
        } else if (mode == Mode::Selector) {
            text.begin(dw, dh);
            text.draw(24, 18, 2.2f, 1.f, 0.85f, 0.35f, "VAN BUREN  -  model viewer");
            text.draw(24, 68, 1.2f, 0.8f, 0.9f, 1.f, "Filter: " + filter + "_");
            char cnt[64]; std::snprintf(cnt, sizeof cnt, "%zu / %zu models", shown.size(), entries.size());
            text.draw(dw - text.width(cnt, 1.f) - 24, 72, 1.f, 0.6f, 0.6f, 0.6f, cnt);
            for (int r = 0; r < rows; r++) {
                int k = top + r; if (k >= (int)shown.size()) break;
                float y = 110 + r * rowH;
                bool isCur = shown[k] == cur;
                if (k == sel) text.rect(16, y - 1, dw - 32.f, rowH, 0.25f, 0.4f, 0.7f, 0.55f);
                std::string lab = entries[shown[k]].label;
                size_t maxc = (size_t)((dw - 200) / text.width("M", 1.2f));
                if (lab.size() > maxc && maxc > 4) lab = lab.substr(0, maxc - 3) + "...";
                text.draw(28, y, 1.2f, isCur ? 0.5f : 1.f, 1.f, isCur ? 0.5f : 1.f, lab);
                if (isCur) text.draw(dw - 140.f, y, 1.f, 0.5f, 1.f, 0.5f, "(open)");
            }
            if (shown.empty()) text.draw(28, 110, 1.2f, 1.f, 0.5f, 0.5f, "no models match");
            text.draw(24, dh - 52.f, 1.f, 0.7f, 0.7f, 0.7f, "Up/Down/PgUp/PgDn/mouse: choose    type: filter (try \"items/\", \"critters/\", \"props/\")    Enter / double-click: open    Esc: back / quit");
            if (!status.empty()) text.draw(24, dh - 28.f, 1.f, 1.f, 0.4f, 0.4f, status);
            text.end();
        }
        SDL_GL_SwapWindow(win);
        if (!shot.empty() && ++frame >= 3) {
            std::vector<uint8_t> px((size_t)dw * dh * 4);
            glReadPixels(0, 0, dw, dh, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < dh / 2; y++) for (int x = 0; x < dw * 4; x++) std::swap(px[(size_t)y * dw * 4 + x], px[(size_t)(dh - 1 - y) * dw * 4 + x]);
            SDL_Surface* sf = SDL_CreateRGBSurfaceFrom(px.data(), dw, dh, 32, dw * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            if (sf) { SDL_SaveBMP(sf, shot.c_str()); SDL_FreeSurface(sf); }
            std::printf("screenshot: %s\n", shot.c_str());
            done = true;
        }
    }
    model.destroy();
    SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

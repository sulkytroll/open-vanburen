// vanburen — the start of the Van Buren game engine (forked from the map viewer; level/entity rendering lives in common/level.h).
//
//   vanburen                 the main menu (Start New Game, Map Browser, Object Browser, Exit Game)
//   vanburen [mapname]       straight into a map, e.g. 03_Tutorial_Junktown (the new-game screen: the party arrives at entry point 1, Cpl. Armstrong nearby)
//
// Reads the tech demo's own packaged files: run it next to F3.exe, or give -game DIR (or set VB_GAME_DIR); -assets DIR -game DIR reads an extracted copy instead.
//
// Camera (fixed tilt, isometric-style): WASD pan, Q / E rotate continuously around the camera target, mouse wheel zoom, Space re-centre on the player.
// Player: left-click the ground to walk there (A* over the collision grid, solid objects and walls block).
// Fog of war: F cycles three styles -- 1 the tech demo's (whole map at reduced brightness, lit disc following the player; people and objects vanish outside the disc), 2 'remembered' (black beyond the disc,
// explored ground stays at 50%), 0 off.
// World: the map's entities run their original scripts (src/game/world.cpp): people idle, wander and patrol, doors open, trigger zones fire, and clicking a person with something to say
// starts the dialogue (1-9 or click to answer). Click a door/container/person to walk up and use it. J journal, Z toggles whether speech text shows in the fog.
// Debug: C collision overlay, M markers, T textures, L lightmaps, X wireframe, H HUD, F1 help, [ / ] previous/next map, Esc quit.
// Options: -game DIR  -assets DIR  -size W H  -shot out.bmp  -fog 0|1|2  -nofog  -click px py (inject a click)  -simtime sec (simulate before the screenshot)  -col  -list

#include "common/level.h"
#include "common/gamedir.h"
#include "common/nav.h"
#include "common/actionbar.h"
#include "common/invwindow.h"
#include "common/vfxrt.h"
#include "common/audio.h"
#include "common/lightfield.h"
#include <fstream>
#include "game/world.h"
#include <array>
#include <chrono>
#include <deque>

static const float kPi = 3.14159265f;

static bool projectToScreen(const float* VP, float x, float y, float z, int w, int h, float& sx, float& sy) {
    float cx = VP[0] * x + VP[4] * y + VP[8] * z + VP[12], cy = VP[1] * x + VP[5] * y + VP[9] * z + VP[13], cw = VP[3] * x + VP[7] * y + VP[11] * z + VP[15];
    if (cw <= 0.0001f) return false;
    sx = (cx / cw * 0.5f + 0.5f) * (float)w; sy = (1.f - (cy / cw * 0.5f + 0.5f)) * (float)h; return true;
}
// greedy word wrap for the fixed-width font
static std::vector<std::string> wrapText(const std::string& s, size_t cols) {
    std::vector<std::string> out; std::string line, word;
    auto flush = [&]() { if (!line.empty()) { out.push_back(line); line.clear(); } };
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == ' ') {
            if (!word.empty()) { if (!line.empty() && line.size() + 1 + word.size() > cols) flush(); if (!line.empty()) line += ' '; line += word; word.clear(); }
        } else word.push_back(s[i]);
    }
    flush(); if (out.empty()) out.push_back(""); return out;
}

struct GameCamera {
    // Parameters from the game's own Engine/sys.ini [Camera]: the distance is fixed (350) and zoom is a very narrow field of view (6..15 degrees, a near-orthographic
    // telephoto look); elevation ranges 38.5..89 degrees (we keep the minimum, the tilt stays fixed for now); pan speed 96 units/s; azimuth speed 360 degrees/s.
    V3 target{0, 0, 0};
    float yaw = 0, goalYaw = 0;            // yaw 0 looks down +Z (the original start view: Cpl. Armstrong, who stands further along +Z, appears up and to the left)
    float pitch = 38.5f * kPi / 180.f;
    static constexpr float kDist = 350.f;
    float fov = 8.f;                       // vertical field of view in degrees (zoom); default fitted to the new-game screenshot (player ~85 px tall in a 770 px window)
    static constexpr float kMinFov = 6.f, kMaxFov = 15.f, kPanSpeed = 96.f;
    static constexpr float kRotSpeed = 2.1f;      // radians/s while Q / E is held (about 120 degrees/s; the game's sys.ini lists an azimuth speed of 360 degrees/s)
    V3 forward() const { return {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)}; }
    V3 eye() const { return target - forward() * kDist; }
};

struct Player {
    V3 pos{0, 0, 0};
    float yaw = 0;                               // facing: model forward is +Z at yaw 0
    std::vector<std::pair<float, float>> path;   // remaining waypoints (x, z)
    size_t next = 0;
    bool moving = false;
    float animT = 0;
    std::vector<gm::GpuModel*> models;
    const vb::GrSkeleton* skel = nullptr;
    const vb::GrAnimation *idle = nullptr, *walk = nullptr, *atkGun = nullptr, *atkMelee = nullptr, *atkUna = nullptr, *hit = nullptr, *death = nullptr, *idle2 = nullptr, *idle2d = nullptr, *walk2 = nullptr, *atk2 = nullptr, *hit2 = nullptr, *useA = nullptr, *pickDef = nullptr, *pickCom = nullptr;       // useA / pick*: reaching for a door, container, computer / picking something up
    float speed = vb::World::kWalkSpeed;         // world units per second
};

// Death: the 3D scene is drawn into an off-screen target, then put on screen turned gray and slightly blurred; `amt` 0..1 is the fade (the interface is drawn afterwards, untouched).
struct DeathFx {
    GLuint fbo = 0, tex = 0, rb = 0, prog = 0, vao = 0; int w = 0, h = 0;
    void ensure(int W, int H) {
        if (!prog) {
            prog = program("#version 330 core\nout vec2 uv; void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }",
                           "#version 330 core\nin vec2 uv; uniform sampler2D tex; uniform vec2 px; uniform float amt; out vec4 o; void main(){\n"
                           " vec3 c = vec3(0.0); float ws = 0.0; float r = amt * 1.4;\n"
                           " for (int i = -2; i <= 2; i++) for (int j = -2; j <= 2; j++) { float wgt = exp(-float(i * i + j * j) / 3.0); c += texture(tex, uv + vec2(float(i), float(j)) * px * r).rgb * wgt; ws += wgt; }\n"
                           " c /= ws; float g = dot(c, vec3(0.299, 0.587, 0.114)); o = vec4(mix(c, vec3(g), amt), 1.0); }");
            glGenVertexArrays(1, &vao);
        }
        if (fbo && W == w && H == h) return;
        if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex); glDeleteRenderbuffers(1, &rb); }
        w = W; h = H; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
    }
    void bind(int W, int H) { ensure(W, H); glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
    void resolve(float amt) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glUseProgram(prog); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex); glUniform1i(glGetUniformLocation(prog, "tex"), 0);
        glUniform2f(glGetUniformLocation(prog, "px"), 1.f / (float)w, 1.f / (float)h); glUniform1f(glGetUniformLocation(prog, "amt"), amt);
        glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); glEnable(GL_DEPTH_TEST);
    }
};

// Explored-ground memory for the fog of war: one texel per 0.5 world units (same resolution as the collision grid), uploaded as an R8 texture.
struct FogMask {
    std::vector<uint8_t> px; int w = 0, h = 0; float ox = 0, oz = 0; GLuint tex = 0; int lastCx = -99999, lastCz = -99999; bool dirty = false;
    void init(float minX, float minZ, float maxX, float maxZ) {
        ox = minX; oz = minZ; w = (int)std::ceil((maxX - minX) / 0.5f) + 1; h = (int)std::ceil((maxZ - minZ) / 0.5f) + 1;
        px.assign((size_t)w * h, 0); lastCx = lastCz = -99999; dirty = true;
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {0, 0, 0, 0}; glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    }
    // mark the disc around (x, z) as seen (full inside r0, soft falloff to r1); only does work when the player moves to a new cell
    template <class LitFn> void reveal(float x, float z, float r0, float r1, LitFn lit) {
        int cx = (int)std::floor((x - ox) / 0.5f), cz = (int)std::floor((z - oz) / 0.5f);
        if (cx == lastCx && cz == lastCz) return;
        lastCx = cx; lastCz = cz;
        int rc = (int)std::ceil(r1 / 0.5f) + 1;
        for (int dz = -rc; dz <= rc; dz++) for (int dx = -rc; dx <= rc; dx++) {
            int ix = cx + dx, iz = cz + dz; if (ix < 0 || iz < 0 || ix >= w || iz >= h) continue;
            float vis = lit(ox + (ix + 0.5f) * 0.5f, oz + (iz + 0.5f) * 0.5f);
            uint8_t v = (uint8_t)(vis * 255.f); uint8_t& c = px[(size_t)iz * w + ix]; if (v > c) { c = v; dirty = true; }
        }
    }
    void upload() { if (!dirty) return; glBindTexture(GL_TEXTURE_2D, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, px.data()); dirty = false; }
};

static std::vector<std::string> listMaps(const std::string& assets) {
    std::vector<std::string> out;
    for (const char* sub : {"Maps", "Engine"}) {
        for (const std::string& fn : vb::vfs::list(assets + "/" + sub)) if (fs::path(fn).extension() == ".tre") out.push_back(fs::path(fn).stem().string());
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        std::string x = a, y = b; for (auto& c : x) c = (char)std::tolower((unsigned char)c); for (auto& c : y) c = (char)std::tolower((unsigned char)c); return x < y; });
    return out;
}

static bool gProf = std::getenv("VB_PROF") != nullptr;
static double gProfAcc[12], gProfMax[12], gProfCur[12]; static const char* gProfName[12] = {"events","world.update","vfx.update","fog","drawWorld","ents","player","vfx.draw","ui","text","swap","other"};
static double profNow() { return SDL_GetPerformanceCounter() * 1000.0 / SDL_GetPerformanceFrequency(); }
static double gProfT;
static void profMark(int slot) { if (!gProf) return; glFinish(); double t = profNow(); gProfAcc[slot] += t - gProfT; gProfCur[slot] += t - gProfT; gProfT = t; }

void registerEmbeddedFiles();
int main(int argc, char** argv) {
    std::string initial = "03_Tutorial_Junktown", assets, shot, gameDir, talkTo, useName;
    const bool kRememberedFog = std::getenv("VB_FOG_REMEMBERED") != nullptr;   // the "remembered" fog of war (mode 2) is switched off for now (the tech demo has none); VB_FOG_REMEMBERED=1 brings it back
    int fogMode = 1; bool colOverlay = false, markers = false, useLm = true, useTex = true, wire = false, hud = true, help = false, listOnly = false, haveClick = false;
    int argMenuClick = -1; int argPickSkill = -1; int W = 1280, H = 960, frames = 3, yawQuarters = 0; float zoomArg = 0; float clickX = 0, clickY = 0, simTime = 0; int argPanel = 0, argArm = -1; bool argAutoPass = false; std::vector<std::string> giveItems; std::string clickEnt; bool argTakeAll = false, argEquipAll = false; int argSelect = -1; bool nameGiven = false, haveAt = false, haveMouseArg = false; float argMouseX = 0, argMouseY = 0; float atX = 0, atZ = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "-game" && i + 1 < argc) gameDir = argv[++i];
        else if (a == "-talk" && i + 1 < argc) talkTo = argv[++i];
        else if (a == "-use" && i + 1 < argc) useName = argv[++i];
        else if (a == "-panel" && i + 1 < argc) argPanel = std::atoi(argv[++i]);
        else if (a == "-takeall") argTakeAll = true;
        else if (a == "-equipall") argEquipAll = true;
        else if (a == "-pickskill" && i + 1 < argc) argPickSkill = std::atoi(argv[++i]);
        else if (a == "-give" && i + 1 < argc) giveItems.push_back(argv[++i]);
        else if (a == "-select" && i + 1 < argc) argSelect = std::atoi(argv[++i]);
        else if (a == "-mouse" && i + 2 < argc) { argMouseX = (float)std::atof(argv[++i]); argMouseY = (float)std::atof(argv[++i]); haveMouseArg = true; }
        else if (a == "-at" && i + 2 < argc) { atX = (float)std::atof(argv[++i]); atZ = (float)std::atof(argv[++i]); haveAt = true; }
        else if (a == "-shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "-size" && i + 2 < argc) { W = std::atoi(argv[++i]); H = std::atoi(argv[++i]); }
        else if (a == "-frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "-clickent" && i + 1 < argc) clickEnt = argv[++i];
        else if (a == "-arm" && i + 1 < argc) argArm = std::atoi(argv[++i]);
        else if (a == "-autopass") argAutoPass = true;
        else if (a == "-click" && i + 2 < argc) { clickX = (float)std::atof(argv[++i]); clickY = (float)std::atof(argv[++i]); haveClick = true; }
        else if (a == "-simtime" && i + 1 < argc) simTime = (float)std::atof(argv[++i]);
        else if (a == "-yawq" && i + 1 < argc) yawQuarters = std::atoi(argv[++i]);
        else if (a == "-zoom" && i + 1 < argc) zoomArg = (float)std::atof(argv[++i]);
        else if (a == "-nofog") fogMode = 0;
        else if (a == "-menuclick" && i + 1 < argc) argMenuClick = std::atoi(argv[++i]);        // (test hook: press menu button n after a few frames)
        else if (a == "-fog" && i + 1 < argc) { fogMode = std::atoi(argv[++i]) % 3; if (fogMode == 2 && !kRememberedFog) fogMode = 1; }
        else if (a == "-col") colOverlay = true;
        else if (a == "-list") listOnly = true;
        else if (a == "-nohud") hud = false;                                  // (no debug text: for screenshots)
        else if (a == "-help") help = true;                                     // (start with the controls popup open)
        else if (a[0] != '-' && !nameGiven) { initial = a; nameGiven = true; }
    }
    bool menuMode = !nameGiven; if (menuMode) { initial = "Mainmenu"; hud = false; }       // no map named: the main menu (Maps/Mainmenu.map, a 3D scene with its own camera: Engine/sys.ini [Mainmenu])
    registerEmbeddedFiles();
    if (assets.empty()) {       // the normal case: the tech demo's own folder, read from its packaged files (-game DIR, $VB_GAME_DIR, the folder of this executable, the current folder)
        if (!findAndMountGame(gameDir, gameDir)) {       // (started from a menu or a file manager there is no terminal to read the message from)
            if (SDL_Init(SDL_INIT_VIDEO) == 0) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open Van Buren", "The Fallout 3 Van Buren tech demo folder was not found.\n\nPut this program in the folder that holds F3.exe, resource.rht and data/, or start it with -game /path/to/that/folder (or set VB_GAME_DIR).", nullptr); SDL_Quit(); }
            return 1;
        }
        assets = gameDir;
    } else if (gameDir.empty()) gameDir = (fs::path(assets).parent_path() / "Fallout 3 Van Buren Tech Demo").string();       // -assets DIR: an extracted copy (vb_extract output), for comparisons
    std::vector<std::string> maps = listMaps(assets);
    std::string startMap = "03_Tutorial_Junktown";      // Engine/sys.ini "Start map = 00_03_Tutorial_Junktown.map" (the Vault demo folder names the Vault); the .map has a "NN_" prefix the .tre name lacks
    { vb::vfs::Stream ini(assets + "/Engine/sys.ini"); std::string ln; while (std::getline(ini, ln)) {
        size_t eq = ln.find('='); if (eq == std::string::npos || ln.compare(0, 9, "Start map") != 0) continue;
        std::string v = ln.substr(eq + 1); while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back(); size_t i0 = 0; while (i0 < v.size() && std::isspace((unsigned char)v[i0])) i0++; v = v.substr(i0);
        if (v.size() > 4 && v.compare(v.size() - 4, 4, ".map") == 0) v.resize(v.size() - 4);
        for (const std::string& m : maps) if (m == v || (v.size() > m.size() && v.compare(v.size() - m.size(), m.size(), m) == 0 && v[v.size() - m.size() - 1] == '_')) { startMap = m; break; }
        break; } }
    if (maps.empty()) { std::fprintf(stderr, "no maps under %s (use -assets DIR)\n", assets.c_str()); return 1; }
    if (listOnly) { for (auto& m : maps) std::printf("%s\n", m.c_str()); return 0; }
    auto mi = std::find(maps.begin(), maps.end(), initial);
    if (mi == maps.end()) { std::fprintf(stderr, "unknown map '%s' (run with -list)\n", initial.c_str()); return 1; }
    int curMap = (int)(mi - maps.begin());

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* win = SDL_CreateWindow("Van Buren", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    SDL_ShowCursor(SDL_DISABLE);                               // the game draws its own cursor
    glewExperimental = GL_TRUE; glewInit();
    SDL_GL_SetSwapInterval(shot.empty() ? 1 : 0);

    LevelRenderer R(assets);
    DeathFx deathFx;
    float Vm_[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};      // the current view matrix (camera axes for the effect billboards)
    VfxRuntime vfx; vfx.init(&R, &R.mtex, assets);                         // the game's own effect files
    Audio audio; audio.init(assets);                                         // OpenAL (VB_NOSOUND=1 turns the sound off)
    vfx.soundStart = [&](const std::string& ps, const std::string&, bool loop, V3 p) { if (std::getenv("VB_DEBUG_SOUND")) std::fprintf(stderr, "effect sound %s at %.1f %.1f %.1f loop=%d\n", ps.c_str(), p.x, p.y, p.z, (int)loop); return audio.play(ps, p.x, p.y, p.z, loop); };
    vfx.soundMove = [&](int id, V3 p) { audio.move(id, p.x, p.y, p.z); }; vfx.soundStop = [&](int id) { audio.stop(id); };
    std::map<int, int> shotSeen, hitSeen;                                   // per entity: the last attack whose weapon / hit effects were started
    std::map<int, int> heldGroup, vegGroup;
    struct MapEmit { std::string group, file; float M[16]; int gid = 0; }; std::vector<MapEmit> mapEmits;       // the map's effect emitters ("Life_SupportEffects_003" belongs to group "Life_SupportEffects"), switched by MapEffectSetActive
    std::map<int, std::string> heldFile;      // looping effects of held weapons (the flamethrower's pilot flame)
    std::map<int, float> stepPhase;                                         // footstep cue tracking
    struct HeldPose { float W2[16]; std::map<std::string, std::array<float, 16>> bone; bool valid = false; };
    std::map<int, HeldPose> heldPose, vegPose;   /* vegPose: bones of entities that carry an attached effect */                                       // world matrices of each holder's weapon bones, refreshed whenever the weapon is drawn
    Text text; text.init();
    Level L; NavGrid nav; GameCamera cam; Player pl; FogMask fogMask; LightField lightField;
    // How lit a world position is (0 .. 1): the spherical fall-off of the player's light (3D distance from the light point, which sits 4 units above the player's feet) times the field of
    // visibility (lines of sight over the collision grid: walls, props and closed doors cast shadows). 1 everywhere with the fog of war off.
    auto litAt = [&](float x, float z, float y) -> float {
        if (fogMode == 0) return 1.f;
        float dy = y - (pl.pos.y + 4.f), d = std::sqrt((x - pl.pos.x) * (x - pl.pos.x) + (z - pl.pos.z) * (z - pl.pos.z) + dy * dy);
        float t = std::max(0.f, std::min(1.f, (d - R.fog.r0) / (R.fog.r1 - R.fog.r0))); float v = 1.f - t * t * (3.f - 2.f * t);
        return nav.valid() ? v * lightField.at(x, z) : v;
    };
    const float kLitMin = 0.05f;                                                 // below this something counts as being in the dark
    // light on an entity: the best of its centre and a ring 1.5 units around it, so a terminal standing against a wall (its own cell is part of the wall) is lit when the floor in front of it is
    auto litEnt = [&](float x, float z, float y) -> float {
        float best = litAt(x, z, y); if (best >= kLitMin || fogMode == 0) return best;
        for (int k = 0; k < 8; k++) { float a = k * 0.7853982f; best = std::max(best, litAt(x + 1.5f * std::cos(a), z + 1.5f * std::sin(a), y)); }
        return best;
    };
    vb::World world(assets, gameDir);
    UiDraw ui; ui.init();
    ActionBar bar; bar.load(assets, gameDir, R.mtex, &world.stf);
    ItemWindows iw; iw.load(R.mtex, &bar, &world);
    iw.sfx = [&](const char* w) { audio.play(w, 0, 0, 0, false, false); };
    std::deque<std::string> msgLog;                                         // the message screen on the left of the bottom bar
    std::string barPressed; int panel = 0;                                  // button held down; open overlay: 1 inventory, 2 character, 3 options
    std::function<bool(const vb::Entity&, const Level::EntDraw*&, float*, float*)> doorBounds_;
    std::map<int, std::vector<int>> doorBlock;                              // door entity id -> cells blocked while it is closed
    std::map<int, std::vector<int>> propBlock;                              // prop / container entity id -> grid cells its footprint blocks
    std::map<int, std::vector<int>> doorCells;                              // door entity id -> collision cells opened by it
    // mouse cursor: the game's own cursor images (Engine/DefaultCursor, Interface/*Cursor.tga); the context decides which one
    enum Cur { CurDefault, CurGui, CurWait, CurTalk, CurUse, CurPickup, CurAttack, CurTarget, CurCount };
    static const char* kCurFile[CurCount] = {"DefaultCursor", "GUI_DefaultCursor", "WaitCursor", "TalkCursor", "UseCursor", "PickupCursor", "AttackCursor", "TargetCursor"};
    static const bool kCurCentre[CurCount] = {false, false, true, false, false, false, true, true};     // hot spot: the arrow tip (top-left) or the middle (crosshair, watch)
    GLuint curProg = program("#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec2 uv; uniform vec2 screen; out vec2 v; void main(){ v = uv; vec2 n = p / screen * 2.0 - 1.0; gl_Position = vec4(n.x, -n.y, 0.0, 1.0); }",
                             "#version 330 core\nin vec2 v; uniform sampler2D tex; out vec4 o; void main(){ o = texture(tex, v); if (o.a < 0.02) discard; }");
    GLuint curVao = 0, curVbo = 0; glGenVertexArrays(1, &curVao); glBindVertexArray(curVao); glGenBuffers(1, &curVbo); glBindBuffer(GL_ARRAY_BUFFER, curVbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);
    gm::TexInfo curTex[CurCount]; for (int i = 0; i < CurCount; i++) { curTex[i] = R.mtex.get(kCurFile[i], nullptr, true); if (curTex[i].id) { glBindTexture(GL_TEXTURE_2D, curTex[i].id); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); } }
    float mouseX = -1, mouseY = -1; bool mouseIn = false;
    std::vector<std::array<float, 4>> uiRects;                              // screen rectangles of the dialogue / journal windows from the last frame (the cursor is a plain arrow over them)
    // attackHand: weapon button armed for a target click (0 / 1); attackOrder: the target being walked up to / attacked
    // skillArm: a kit button (0 / 1) or the Use Skill button (2) waits for a target click (yellow crosshair); then the target's skill menu (skm) opens at the click; the chosen entry is carried out on arrival (pendingIsSkill)
    int skillArm = -1; struct { bool open = false; int ent = 0; float x = 0, y = 0; std::vector<vb::SkillOption> opts; } skm; std::vector<std::array<float, 5>> skRects; vb::SkillOption pendingSkill; bool pendingIsSkill = false;
    // the "use" animation: on arrival the player first plays it (HumM_Def_Una_Use_01, or the pickup animation for loose items) and the action happens at its cue (useAct)
    float pickupCue = 1.2f; { vb::vfs::Stream ini(assets + "/Critters/HumM_Def_Una_Pickup_01.ini"); std::string ln; while (std::getline(ini, ln)) if (ln.rfind("Time1", 0) == 0) { size_t eq = ln.find('='); if (eq != std::string::npos) pickupCue = (float)std::atof(ln.c_str() + eq + 1) / 1000.f; } }       // cue PU of the pickup animation
    auto reachCue = [&](const vb::GrAnimation* an) -> float {         // time at which the right hand is farthest from the pelvis (horizontally) = the moment of touching the thing
        static std::map<const vb::GrAnimation*, float> cache; auto it = cache.find(an); if (it != cache.end()) return it->second;
        int hb = -1, pb = -1; if (pl.skel) for (size_t b = 0; b < pl.skel->bones.size(); b++) { if (pl.skel->bones[b].name == "R Hand") hb = (int)b; if (pl.skel->bones[b].name == "Pelvis") pb = (int)b; }
        float best = an->duration * 0.5f, bd = -1.f;
        if (hb >= 0 && pb >= 0) for (float f = 0.f; f <= 1.0001f; f += 0.02f) { std::vector<float> sk, w; vb::poseSkeleton(*pl.skel, an, an->duration * std::min(f, 0.9999f), sk, &w); float dx = w[(size_t)hb * 16 + 12] - w[(size_t)pb * 16 + 12], dz = w[(size_t)hb * 16 + 14] - w[(size_t)pb * 16 + 14], d = dx * dx + dz * dz; if (d > bd) { bd = d; best = an->duration * f; } }
        cache[an] = best; return best; };
    float useT = 0, useCue = 0, useDur = 0; const vb::GrAnimation* useAnimP = nullptr; bool useFired = true; std::function<void()> useAct;
    int pendingUse = 0, attackOrder = 0, attackHand = -1; bool outOfAp = false; float deathT = 0; bool speechInFog = false, journalOpen = false;
    std::deque<std::pair<std::string, float>> notes;                       // transient messages (bottom left), with their age
    struct ReplyRect { float y0, y1; int idx; }; std::vector<ReplyRect> replyRects;
    auto note = [&](const std::string& t) { if (!t.empty()) { msgLog.push_back(t); while (msgLog.size() > 60) msgLog.pop_front(); } };
    GLuint markVao = 0, markVbo = 0;
    glGenVertexArrays(1, &markVao); glBindVertexArray(markVao); glGenBuffers(1, &markVbo); glBindBuffer(GL_ARRAY_BUFFER, markVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12); glEnableVertexAttribArray(1);
    bool haveDest = false; float destX = 0, destZ = 0, destY = 0;

    // The new-game character is the `Default_PC` template: HumMNor + bald head, stubble, prison suit, shoes, Vault 13 back piece and Pip-Boy.
    std::string pmBody, pmHead;                                             // the armor the player model was built with
    auto buildPlayerModel = [&]() {
        pl.models.clear();
        // the engine's default player template (HumMNor + prison suit parts), with the worn body armor / helmet replacing the outfit / hair
        pmBody = world.equipped[vb::SlotBody]; pmHead = world.equipped[vb::SlotHead];
        for (const vb::ModelPart& p : R.elib.assemblePlayer("Default_PC.CRT", pmBody, pmHead)) if (gm::GpuModel* g = R.getModel(p)) pl.models.push_back(g);
        pl.skel = R.alib.skeletonFor("HumMNor"); if (std::getenv("VB_DEBUG_BONES") && pl.skel) for (size_t bi = 0; bi < pl.skel->bones.size(); bi++) std::fprintf(stderr, "bone %zu %s parent %d\n", bi, pl.skel->bones[bi].name.c_str(), pl.skel->bones[bi].parent);
        if (std::getenv("VB_DEBUG_STRIDE") && pl.skel) for (const char* an : {"com_una_walk_01", "com_una_run_01"}) for (const std::string& n : R.alib.animationsFor("HumMNor")) { std::string l = n; for (auto& c : l) c = (char)std::tolower((unsigned char)c); if (l.find(an) == std::string::npos) continue;
            const vb::GrAnimation* A = R.alib.animation(n); if (!A) continue; std::vector<float> sk0, w0, sk1, w1; vb::poseSkeleton(*pl.skel, A, 0.f, sk0, &w0); vb::poseSkeleton(*pl.skel, A, A->duration * 0.9999f, sk1, &w1);
            std::fprintf(stderr, "anim %s duration %.3f\n", n.c_str(), A->duration); for (size_t b = 0; b < pl.skel->bones.size() && b < 3; b++) std::fprintf(stderr, "  bone %zu %s start (%.2f %.2f %.2f) end (%.2f %.2f %.2f)\n", b, pl.skel->bones[b].name.c_str(), w0[b * 16 + 12], w0[b * 16 + 13], w0[b * 16 + 14], w1[b * 16 + 12], w1[b * 16 + 13], w1[b * 16 + 14]);
            for (float f = 0; f < 1.001f; f += 0.125f) { std::vector<float> sk, w; vb::poseSkeleton(*pl.skel, A, A->duration * std::min(f, 0.9999f), sk, &w); std::fprintf(stderr, "   t=%.3f", f * A->duration); for (size_t b = 0; b < pl.skel->bones.size(); b++) if (pl.skel->bones[b].name == "L Foot" || pl.skel->bones[b].name == "R Foot") std::fprintf(stderr, " %s z=%.2f y=%.2f", pl.skel->bones[b].name.c_str(), w[b * 16 + 14], w[b * 16 + 13]); std::fprintf(stderr, "\n"); } }
        pl.idle = R.alib.animation(R.alib.idleFor("HumMNor"));
        for (const std::string& n : R.alib.animationsFor("HumMNor")) { std::string l = n; for (auto& c : l) c = (char)std::tolower((unsigned char)c); if (l.find("com_una_walk_01") != std::string::npos) pl.walk = R.alib.animation(n);
            else if (l.find("_com_1h_gun_shoot_01") != std::string::npos && l.find("side") == std::string::npos) pl.atkGun = R.alib.animation(n);
            else if (l.find("_com_1h_melee_slash_01") != std::string::npos) pl.atkMelee = R.alib.animation(n);
            else if (l.find("_com_una_punch_jab_01") != std::string::npos) pl.atkUna = R.alib.animation(n);
            else if (l.find("_com_una_dmgfr_01") != std::string::npos) pl.hit = R.alib.animation(n);
            else if (l.find("_proneb_una_death_01") != std::string::npos) pl.death = R.alib.animation(n);
            else if (l.find("_com_2hh_idle_01") != std::string::npos) pl.idle2 = R.alib.animation(n);
            else if (l.find("_def_2hh_idle_01") != std::string::npos) pl.idle2d = R.alib.animation(n);
            else if (l.find("_com_2hh_walk_01") != std::string::npos) pl.walk2 = R.alib.animation(n);
            else if (l.find("_com_2hh_gun_shoot_01") != std::string::npos) pl.atk2 = R.alib.animation(n);
            else if (l.find("_com_2hh_dmgfr_01") != std::string::npos) pl.hit2 = R.alib.animation(n);
            else if (l.find("_def_una_use_01") != std::string::npos) pl.useA = R.alib.animation(n);
            else if (l.find("_def_una_pickup_01") != std::string::npos) pl.pickDef = R.alib.animation(n);
            else if (l.find("_com_una_pickup_01") != std::string::npos) pl.pickCom = R.alib.animation(n); }
    };
    buildPlayerModel();

    auto openMap = [&](int idx, int entryId = -1) -> bool {
        std::string err;
        if (!loadLevel(assets, maps[idx], R.lib, L, err)) { std::fprintf(stderr, "%s: %s\n", maps[idx].c_str(), err.c_str()); return false; }
        // the map's own Default_PC entity is the player: take its spawn and don't draw it as a static NPC
        bool havePcEnt = false; float pcX = 0, pcZ = 0, pcYaw = kPi;
        if (L.haveMap) for (size_t i = 0; i < L.map.entities.size(); i++) {
            std::string f = L.map.entities[i].file; for (auto& c : f) c = (char)std::tolower((unsigned char)c);
            if (f == "default_pc.crt") { havePcEnt = true; pcX = L.map.entities[i].pos[0]; pcZ = L.map.entities[i].pos[2]; pcYaw = L.map.entities[i].rot[0]; L.map.entities.erase(L.map.entities.begin() + (long)i); break; }
        }
        R.buildEntities(L);
        if (L.haveCol) nav.build(L.col, L.tre.min[0], L.tre.min[2]); else nav = NavGrid();
        // spawn at the map's first party entry point, else the middle of the map
        float sx = (L.cmin.x + L.cmax.x) * 0.5f, sz = (L.cmin.z + L.cmax.z) * 0.5f;
        if (std::getenv("VB_LIST_ENTRIES") && L.haveMap) for (const auto& e : L.map.entries) std::fprintf(stderr, "entry %d: party slot 0 at %.1f %.1f %.1f angle %.2f\n", (int)e.id, e.pos[0][0], e.pos[0][1], e.pos[0][2], e.angle[0]);
        if (std::getenv("VB_LIST_ENTRIES") && L.haveMap) for (const auto& t : L.map.triggers) if (t.type == 0) std::fprintf(stderr, "transition -> %s entry %d\n", t.target.c_str(), (int)t.entry);
        if (havePcEnt) { sx = pcX; sz = pcZ; }
        else if (L.haveMap && !L.map.entries.empty()) { const auto& e = L.map.entries[0]; if (e.pos[0][0] != 0 || e.pos[0][2] != 0) { sx = e.pos[0][0]; sz = e.pos[0][2]; pcYaw = e.angle[0]; havePcEnt = true; } }
        if (entryId >= 0 && L.haveMap && !L.map.entries.empty()) {             // arriving through a map transition: the named entry point
            const vb::MapEntry* en = &L.map.entries[0]; for (const auto& e : L.map.entries) if (e.id == entryId) en = &e;
            sx = en->pos[0][0]; sz = en->pos[0][2]; pcYaw = en->angle[0]; havePcEnt = true;
        }
        if (nav.valid()) { int cx, cz, rx, rz; nav.cellOf(sx, sz, cx, cz); if (nav.nearest(nav.clear, cx, cz, 400, rx, rz)) nav.centerOf(rx, rz, sx, sz); }
        pl.pos = {sx, 0, sz}; float gy = 0; if (groundHeight(L.tre, sx, sz, 5.f, 8.f, gy)) pl.pos.y = gy;
        pl.path.clear(); pl.moving = false; pl.yaw = havePcEnt ? pcYaw : kPi; pl.animT = 0; haveDest = false;
        cam.target = pl.pos; cam.yaw = cam.goalYaw = 0;
        fogMask.init(L.cmin.x, L.cmin.z, L.cmax.x, L.cmax.z); if (nav.valid()) { lightField.init(nav.w, nav.h, nav.ox, nav.oz); if (std::getenv("VB_DEBUG_LIGHT")) std::fprintf(stderr, "nav grid %d x %d origin %.1f %.1f\n", nav.w, nav.h, nav.ox, nav.oz); }
        // the script runtime for this map
        world.host.findPath = [&](float sx2, float sz2, float gx, float gz, std::vector<std::pair<float, float>>& p) { std::vector<int> opened; if (nav.valid()) for (size_t id = 2; id <= world.count(); id++) { vb::Entity* de = world.find((int)id); if (de && de->kind == vb::EntKind::Door && !de->open && !de->locked && de->vm && doorBlock.count(de->id)) { nav.openCells(doorBlock[de->id]); opened.push_back(de->id); } }      // people can walk through closed doors that are not locked (the door opens for them)
            bool ok = nav.valid() ? nav.findPath(sx2, sz2, gx, gz, p) : (p.assign(1, {gx, gz}), true);
            for (int did : opened) nav.closeCells(doorBlock[did]); if (std::getenv("VB_DEBUG_PATH")) { std::fprintf(stderr, "findPath (%.1f %.1f) -> (%.1f %.1f): ok=%d %zu points:", sx2, sz2, gx, gz, (int)ok, p.size()); for (auto& q : p) std::fprintf(stderr, " (%.1f %.1f)", q.first, q.second); std::fprintf(stderr, "\n"); } return ok; };
        world.host.attachSound = [&](int ent, const std::string& wav, bool loop) { vb::Entity* e = world.find(ent); if (e && std::getenv("VB_DEBUG_SOUND")) std::fprintf(stderr, "  sound entity %s at %.1f %.1f %.1f, player %.1f %.1f\n", e->instance.c_str(), e->x, e->y, e->z, pl.pos.x, pl.pos.z); return e ? audio.play(wav, e->x, e->y + 3.f, e->z, loop) : 0; };
        world.host.removeSound = [&](int id) { audio.stop(id); };
        world.host.uiSound = [&](const char* w) { if (std::getenv("VB_DEBUG_SOUND")) std::fprintf(stderr, "ui sound %s\n", w); audio.play(w, 0, 0, 0, false, false); };
        world.host.underFog = [&](float x, float z) { return fogMode != 0 && litAt(x, z, pl.pos.y + 1.5f) < kLitMin; };
        world.host.lineOfSight = [&](float x0, float z0, float x1, float z1) { return !nav.valid() || nav.lineClear(x0, z0, x1, z1, nav.walk); };
        world.host.walkable = [&](float x, float z) { return !nav.valid() || nav.walkableAt(x, z); };
        doorCells.clear(); doorBlock.clear();
        // Doors and collision: the baked grid has the vault entrance blocked but most doors (inside the vault) are missing from it. Closed doors block their footprint; opening a door
        // clears the cells it blocks (and any baked blocked cells inside its footprint); closing restores both.
        auto doorBounds = [&](const vb::Entity& e, const Level::EntDraw*& en, float* lo, float* hi) {
            en = nullptr; for (const auto& d : L.ents) if (d.mapIdx == e.mapIdx) { en = &d; break; }
            if (!en) return false;
            for (int k = 0; k < 3; k++) { lo[k] = 1e9f; hi[k] = -1e9f; }
            for (auto* g : en->models) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], g->center[k] - g->size[k] / 2); hi[k] = std::max(hi[k], g->center[k] + g->size[k] / 2); }
            return lo[0] <= hi[0];
        };
        world.host.doorToggled = [&, doorBounds](const vb::Entity& e, bool open) {
            const Level::EntDraw* en; float lo[3], hi[3]; if (!doorBounds(e, en, lo, hi)) return;
            if (open) { if (!doorCells.count(e.id)) doorCells[e.id] = nav.openBox(en->M, lo, hi, 0.5f); nav.openCells(doorBlock[e.id]); nav.setDoorOcc(doorBlock[e.id], false); }
            else { if (doorCells.count(e.id)) { nav.closeCells(doorCells[e.id]); doorCells.erase(e.id); } nav.closeCells(doorBlock[e.id]); nav.setDoorOcc(doorBlock[e.id], true); }
        };
        doorBounds_ = doorBounds;
        world.host.animSeconds = [&](int id, const char* pose) -> float { vb::Entity* we = world.find(id); if (!we) return 0.f; for (auto& en : L.ents) if (en.mapIdx == we->mapIdx) { std::string ps = pose; const vb::GrAnimation* a = ps == "laydown" ? en.lay : ps == "getup" ? en.getup : ps == "twitch" ? en.twitch : ps == "maint" ? en.maint : nullptr; return a ? a->duration : 0.f; } return 0.f; };
        world.host.groundY = [&](float x, float z, float& y) { float gy; if (groundHeight(L.tre, x, z, y, 2.5f, gy)) { y = gy; return true; } return false; };
        if (L.haveMap) world.load(L.map, maps[idx], L.mapStem, pl.pos.x, pl.pos.y, pl.pos.z, pl.yaw);
        if (L.haveMap && nav.valid()) for (size_t id = 2; id <= world.count(); id++) {            // closed doors block walking (thin footprints only: the vault entrance is in the baked grid already)
            vb::Entity* e = world.find((int)id); if (!e || e->kind != vb::EntKind::Door || e->open) continue;
            const Level::EntDraw* en; float lo[3], hi[3]; if (!doorBounds_(*e, en, lo, hi)) continue;
            float ex = hi[0] - lo[0], ez = hi[2] - lo[2];
            if (std::min(ex, ez) <= 3.f && std::max(ex, ez) <= 12.f) { doorBlock[e->id] = nav.blockBox(en->M, lo, hi, 0.3f); nav.setDoorOcc(doorBlock[e->id], true); }
            if (std::getenv("VB_DEBUG_ENTS") && e->instance == "Door01Vault13_007") {   // path across the door: closed vs open
                std::vector<std::pair<float, float>> pa; float fx = std::sin(e->yaw), fz = std::cos(e->yaw);
                bool r1 = nav.findPath(e->x - fx * 5, e->z - fz * 5, e->x + fx * 5, e->z + fz * 5, pa); float len1 = 0, px = e->x - fx * 5, pz = e->z - fz * 5; for (auto& q : pa) { len1 += std::hypot(q.first - px, q.second - pz); px = q.first; pz = q.second; }
                std::fprintf(stderr, "door 007 crossing while closed: found=%d pathlen=%.1f (straight 10)\n", (int)r1, len1);
            }
            if (std::getenv("VB_DEBUG_ENTS")) std::fprintf(stderr, "door %s extent %.1f x %.1f blocked %zu cells\n", e->instance.c_str(), ex, ez, doorBlock.count(e->id) ? doorBlock[e->id].size() : 0);
        }
        // Placed props and containers block their footprint (the baked grid only knows the static map). Skipped: flat things (rugs, decals), very large pieces (building shells, which the
        // baked grid already holds, and whose openings must stay passable), and anything without a model. The footprint is the model's bounding box, shrunk a little.
        propBlock.clear();
        if (L.haveMap && nav.valid()) {
            int nBlocked = 0, nCells = 0;
            for (size_t id = 2; id <= world.count() && !std::getenv("VB_NOPROPBLOCK"); id++) {
                vb::Entity* e = world.find((int)id); if (!e || (e->kind != vb::EntKind::Prop && e->kind != vb::EntKind::Container) || !e->active || e->destroyed) continue;
                const Level::EntDraw* en; float lo[3], hi[3]; if (!doorBounds_(*e, en, lo, hi)) continue;
                float ex = hi[0] - lo[0], ez = hi[2] - lo[2], eh = hi[1] - lo[1];
                if (std::getenv("VB_DEBUG_PROPS")) std::fprintf(stderr, "  prop %s (%s) ext %.1f x %.1f h %.1f\n", e->instance.c_str(), e->file.c_str(), ex, ez, eh);
                if (eh < 2.f || std::max(ex, ez) > 14.f || std::min(ex, ez) < 0.4f) continue;
                std::vector<int> cells = nav.blockBox(en->M, lo, hi, -0.25f);
                if (!cells.empty()) { nBlocked++; nCells += (int)cells.size(); propBlock[e->id] = std::move(cells); }
            }
            if (std::getenv("VB_DEBUG_ENTS")) std::fprintf(stderr, "props: %d blocked, %d cells\n", nBlocked, nCells);
            if (std::getenv("VB_DEBUG_ENTS")) {          // can the spawn still reach every door / container / person?
                int ok = 0, bad = 0; for (size_t id = 2; id <= world.count(); id++) { vb::Entity* e = world.find((int)id); if (!e || (e->kind != vb::EntKind::Door && e->kind != vb::EntKind::Container && e->kind != vb::EntKind::Critter) || !e->active) continue;
                    std::vector<std::pair<float, float>> pa; if (nav.findPathWithin(pl.pos.x, pl.pos.z, e->x, e->z, 4.3f, pa)) ok++; else { bad++; std::fprintf(stderr, "  unreachable: %s (%.1f, %.1f)\n", e->instance.c_str(), e->x, e->z); } }
                std::fprintf(stderr, "reachable from spawn: %d ok, %d not\n", ok, bad);
            }
        }
        for (auto& d : world.debug) std::fprintf(stderr, "script: %s\n", d.c_str());
        pendingUse = 0; vegGroup.clear(); notes.clear(); vfx.clear(); audio.stopAll(); if (menuMode) audio.musicStop(); else audio.musicPiece("MA1");       // (the menu has no music) shotSeen.clear(); hitSeen.clear(); heldGroup.clear(); heldFile.clear(); heldPose.clear(); stepPhase.clear();
        mapEmits.clear();
        if (L.haveMap && !std::getenv("VB_NOAMBIENT")) for (const vb::MapEffect& me : L.map.effects) for (const std::string& en : me.effects) {      // the map's own effect emitters (fires, smoke, steam, dust, console lights)
            MapEmit em; em.file = en; em.group = me.name; size_t us = em.group.rfind('_'); if (us != std::string::npos && us + 1 < em.group.size() && std::isdigit((unsigned char)em.group[us + 1])) em.group.resize(us);
            LevelRenderer::modelMatrix(em.M, me.rot[0], 0, 0, me.pos[0], me.pos[1], me.pos[2]); mapEmits.push_back(em);
            if (std::getenv("VB_LIST_FX")) std::fprintf(stderr, "emitter \"%s\": %s at %.1f %.1f %.1f\n", me.name.c_str(), en.c_str(), me.pos[0], me.pos[1], me.pos[2]);
        }
        for (const vb::MapSound& ms : L.map.sounds) audio.play(ms.sound, ms.pos[0], ms.pos[1], ms.pos[2], true);       // the map's sound emitters (EMSD): wind, coyotes, computer bleeps, a fire
        curMap = idx; SDL_SetWindowTitle(win, ("Van Buren - " + maps[idx]).c_str());
        std::printf("%s: %zu triangles, %zu entities, player at %.1f %.1f %.1f\n", maps[idx].c_str(), L.tris, L.ents.size(), pl.pos.x, pl.pos.y, pl.pos.z);
        return true;
    };
    vfx.groundY = [&](float x, float z, float& y) { float gy; if (groundHeight(L.tre, x, z, pl.pos.y, 1.5f, gy) || groundHeight(L.tre, x, z, pl.pos.y, 30.f, gy)) { y = gy; return true; } return false; };   // near the player's level first (inside a building the 30-unit reach found the roof)
    vfx.nearCamera = [&](float x, float z) { return std::hypot(x - cam.target.x, z - cam.target.z) < 90.f; };
    vfx.fogAlpha = [&](float x, float z) { return litAt(x, z, pl.pos.y + 1.5f); };      // the same light as the shaders'
    vfx.visibleAt = [&](float x, float z) { return fogMode == 0 || litAt(x, z, pl.pos.y + 1.5f) >= kLitMin; };
    if (!openMap(curMap)) return 1;
    auto menuCast = [&]() { for (size_t id = 1; id <= world.count(); id++) if (vb::Entity* ae = world.find((int)id)) if (ae->instance == "Cpl_Armstrong00_TM_000") ae->weapon = "Weapons223_Minigun.WEA"; };       // the demo's menu shows Armstrong with a minigun (the map gives him a revolver)
    if (menuMode) menuCast();
    panel = argPanel; world.autoEndPlayerTurn = argAutoPass;
    for (const std::string& gi : giveItems) { world.addItem(gi); int sl = world.defaultSlot(gi); if (sl >= 0) for (size_t bi = 0; bi < world.bag.size(); bi++) if (world.bag[bi].file == gi) { world.equip((int)bi, sl); break; } }       // (test hook: -give FILE gives and equips an item)
    if (haveAt) { pl.pos.x = atX; pl.pos.z = atZ; groundHeight(L.tre, atX, atZ, 5.f, 8.f, pl.pos.y); cam.target = pl.pos; world.setPlayer(pl.pos.x, pl.pos.y, pl.pos.z, pl.yaw); }

    if (zoomArg > 0) cam.fov = zoomArg;
    cam.yaw = cam.goalYaw = yawQuarters * kPi / 2;
    float lastVP[16] = {0}; int lastW = W, lastH = H; double fpsSum = 0; int fpsCount = 0;
    auto walkTo = [&](float gx, float gz) {
        std::vector<std::pair<float, float>> path;
        if (nav.valid() ? nav.findPath(pl.pos.x, pl.pos.z, gx, gz, path) : (path.push_back({gx, gz}), true)) {
            if (path.empty()) return;
            pl.path = path; pl.next = 0; pl.moving = true;
            destX = path.back().first; destZ = path.back().second; haveDest = true;
            destY = pl.pos.y; groundHeight(L.tre, destX, destZ, pl.pos.y, 2.5f, destY);
        }
    };
    // Walk to within reach of an entity (a closed door or a crate stands on blocked cells, so the goal is "any reachable cell near it"). False when nothing near it can be reached.
    auto reachOf = [&](const vb::Entity* e) { return !e ? 0.f : e->kind == vb::EntKind::Door ? 6.f : e->kind == vb::EntKind::Item ? 3.f : 5.f; };
    auto walkToEntity = [&](const vb::Entity* e) -> bool {
        std::vector<std::pair<float, float>> path;
        if (!nav.valid()) { walkTo(e->x, e->z); return true; }
        if (!nav.findPathWithin(pl.pos.x, pl.pos.z, e->x, e->z, reachOf(e) - 0.7f, path)) { if (std::getenv("VB_DEBUG_ENTS")) { int cx, cz; nav.cellOf(pl.pos.x, pl.pos.z, cx, cz); std::fprintf(stderr, "no path to %s from %.1f,%.1f (walk=%d clear=%d)\n", e->instance.c_str(), pl.pos.x, pl.pos.z, (int)nav.walkableAt(pl.pos.x, pl.pos.z), nav.in(cx, cz) ? (int)nav.clear[(size_t)cz * nav.w + cx] : -1); } return false; }
        if (path.empty()) return true;
        pl.path = path; pl.next = 0; pl.moving = true; destX = path.back().first; destZ = path.back().second; haveDest = true;
        destY = pl.pos.y; groundHeight(L.tre, destX, destZ, pl.pos.y, 2.5f, destY);
        return true;
    };
    // Held weapons: the weapon's own model, attached to the right hand's "Handle" bone of the posed skeleton (humans only). Call right after drawing the actor (the bone matrices are from that pose).
    struct WModel { std::vector<gm::GpuModel*> models; const vb::GrSkeleton* skel = nullptr; std::string stem; int mz = -1, br = -1; };
    std::map<std::string, WModel> weaponModels;

    // Weapon classes: 0 one-handed gun, 1 knife, 2 two-handed (minigun, flamethrower, ...: held with the two-handed animation set).
    auto weaponClass = [&](const std::string& f) -> int {
        if (f.empty()) return -1; std::string m = world.itemInfo(f).model; for (auto& c : m) c = (char)std::tolower((unsigned char)c);
        for (const char* k : {"minigun", "flamethrower", "railgun", "arraygun", "shotgun"}) if (m.find(k) != std::string::npos) return 2;
        for (const char* k : {"knife", "shiv", "switchblade"}) if (m.find(k) != std::string::npos) return 1;
        return 0;
    };
    // Offsets in the hand's "Handle" bone space (the models sit right with an identity offset; these nudge the grip). VB_W0 / VB_W1 / VB_W2 = "tx ty tz" per class for tuning; the knife is turned so the blade points forward.
    float off0[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};
    float off1[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};
    float off2[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};
    if (const char* wo = std::getenv("VB_W0")) std::sscanf(wo, "%f %f %f", off0 + 12, off0 + 13, off0 + 14);
    if (const char* wo = std::getenv("VB_W1")) std::sscanf(wo, "%f %f %f", off1 + 12, off1 + 13, off1 + 14);
    if (const char* wo = std::getenv("VB_W2")) std::sscanf(wo, "%f %f %f", off2 + 12, off2 + 13, off2 + 14);
    // ---- combat effects from the effect files
    std::map<std::string, std::vector<std::pair<std::string, int>>> cueCache;     // animation .ini cue tables: name -> (cue, ms)
    auto cues = [&](const std::string& stem) -> const std::vector<std::pair<std::string, int>>& {
        auto it = cueCache.find(stem); if (it != cueCache.end()) return it->second;
        std::vector<std::pair<std::string, int>> v; vb::vfs::Stream f(assets + "/Critters/" + stem + ".ini"); std::string line; std::vector<std::string> names;
        while (std::getline(f, line)) { size_t eq = line.find('='); if (eq == std::string::npos) continue; std::string k = line.substr(0, eq), val = line.substr(eq + 1); auto trim = [](std::string& x) { while (!x.empty() && std::isspace((unsigned char)x.back())) x.pop_back(); size_t i = 0; while (i < x.size() && std::isspace((unsigned char)x[i])) i++; x = x.substr(i); }; trim(k); trim(val);
            if (k.rfind("Cue", 0) == 0) names.push_back(val); else if (k.rfind("Time", 0) == 0 && !names.empty()) { v.push_back({names[std::min(names.size() - 1, (size_t)(std::atoi(k.c_str() + 4) - 1))], std::atoi(val.c_str())}); } }
        return cueCache[stem] = v;
    };
    auto cueSec = [&](const std::string& stem, const char* cue, float def) { for (auto& c : cues(stem)) if (c.first == cue) return (float)c.second / 1000.f; return def; };
    auto anchorAt = [&](float x, float y, float z, float yaw) { VfxRuntime::Anchor a; LevelRenderer::modelMatrix(a.M, yaw, 0, 0, x, y, z); return a; };
    auto vegAnchor = [&](const vb::Entity* e) {                                                   // an effect attached to an entity: follows it, named bones from its last drawn pose
        VfxRuntime::Anchor a = anchorAt(e->x, e->y, e->z, e->yaw); int id = e->id;
        a.bone = [&vegPose, id](const std::string& n, float* M) { auto it = vegPose.find(id); if (it == vegPose.end() || !it->second.valid) return false;
            if (n.empty()) { std::memcpy(M, it->second.W2, 64); return true; } auto b = it->second.bone.find(n); if (b == it->second.bone.end()) return false; std::memcpy(M, b->second.data(), 64); return true; };
        return a; };
    auto heldAnchor = [&](const vb::Entity* e) {                                                  // the holder's weapon: named bones come from the pose of the last draw
        VfxRuntime::Anchor a = anchorAt(e->x, e->y + 3.5f, e->z, e->yaw); int id = e->id;
        a.bone = [&heldPose, id](const std::string& n, float* M) { auto it = heldPose.find(id); if (it == heldPose.end() || !it->second.valid) return false;
            if (n.empty()) { std::memcpy(M, it->second.W2, 64); return true; } auto b = it->second.bone.find(n); if (b == it->second.bone.end()) return false; std::memcpy(M, b->second.data(), 64);
            // the weapon skeletons put the barrel on the bone's Z axis (X is the slide's up); the effect files are authored with X along the barrel, Y up and Z to the side
            const float* B = b->second.data(); for (int k = 0; k < 3; k++) { M[k] = B[8 + k]; M[4 + k] = B[k]; M[8 + k] = B[4 + k]; }
            return true; };
        return a; };
    // Called every frame for an attacker: starts the weapon's effect group (guns at the animation's release cue, blows at the start) and the hit effect on the victim once per attack.
    auto shotFx = [&](vb::Entity* ent, const std::string& weaponFile) {
        if (!ent || ent->attackT <= 0.f) return;
        float el = ent->attackDur - ent->attackT; vb::WeaponStats ws = world.weaponStats(weaponFile); bool gun = !ws.melee && !ws.fists;
        std::string stem = gun ? (ws.twoHanded ? "HumM_Com_2HH_Gun_Shoot_01" : "HumM_Com_1H_Gun_Shoot_01") : ws.melee ? "HumM_Com_1H_Melee_Slash_01" : "HumM_Com_Una_Punch_Jab_01";
        float rl = cueSec(stem, "RL", 0.4f), dm = cueSec(stem, "DM", rl);
        vb::Entity* te = world.find(ent->shotTarget); if (!te) return;
        float dx = te->x - ent->x, dz = te->z - ent->z, yaw = std::atan2(dx, dz);
        if (!ws.effect.empty() && el >= (gun ? rl : 0.f) && shotSeen[ent->id] != ent->shotSerial) {
            shotSeen[ent->id] = ent->shotSerial; VfxRuntime::Anchor self = heldAnchor(ent), tgt = anchorAt(te->x, te->y + 3.4f, te->z, yaw); vfx.start(ws.effect, self, &tgt);
        }
        if (ent->shotHit && el >= (gun ? rl : dm) + 0.04f && hitSeen[ent->id] != ent->shotSerial) {
            hitSeen[ent->id] = ent->shotSerial; std::string eff;
            if (ws.dmgType == 5) eff = ws.effect.find("lamer") != std::string::npos ? "FX_HitFire_01" : "FX_HitLaser_01";
            else if (ws.dmgType == 2) eff = "FX_HitElectrical_01";
            else if (te->race == 5) eff = "FX_HitRicochetMetal_01";                                   // robots do not bleed
            else eff = (gun && ws.twoHanded) ? "FX_HitBloodHighVelocity_01" : "FX_HitBloodStandard_01";
            VfxRuntime::Anchor v = anchorAt(te->x, te->y + 3.4f, te->z, yaw); vfx.start(eff, v, &v);
        }
    };
    // held weapons: the weapon's own model (WP_x) on the right hand's "Handle" bone, drawn on its own skeleton with its firing animation; also keeps the weapon's pose for the effects
    auto drawHeld = [&](const std::string& weaponFile, const float* M, const vb::GrSkeleton* skel, vb::Entity* ent) {
        auto endHeld = [&]() { if (ent) { auto g = heldGroup.find(ent->id); if (g != heldGroup.end()) { vfx.stop(g->second); heldGroup.erase(g); heldFile.erase(ent->id); } heldPose[ent->id].valid = false; } };
        if (weaponFile.empty() || !skel || R.boneWorld.empty()) { endHeld(); if (ent) shotFx(ent, ""); return; }
        std::vector<float> handBones = R.boneWorld;          // the actor's pose (drawing the weapon below replaces R.boneWorld with the weapon skeleton's)
        int hb = -1; for (size_t i = 0; i < skel->bones.size(); i++) if (skel->bones[i].name == "R Handle_1") hb = (int)i;
        if (hb < 0 || (size_t)(hb + 1) * 16 > handBones.size()) return;
        auto it = weaponModels.find(weaponFile);
        if (it == weaponModels.end()) {
            WModel wm; vb::ItemInfo wi = world.itemInfo(weaponFile);
            // the item template names the ground-drop model (GD_WP_x); the weapon in the hand is a separate model WP_x (named in GITM) with its own skeleton: Weapon_1 = main grip, Weapon_2 = off-hand grip, FX_Muzzle, FX_Brass
            std::string gp = R.elib.g3dPath(wi.heldModel);
            if (gp.empty()) { std::string stem = wi.model; if (stem.rfind("GD_WP_", 0) == 0) stem = stem.substr(6); else if (stem.rfind("GD_", 0) == 0) stem = stem.substr(3); gp = R.elib.g3dPath("WP_" + stem); }
            if (gp.empty()) gp = R.elib.g3dPath(wi.model);
            if (!gp.empty()) { vb::ModelPart mp; mp.g3d_path = gp; if (gm::GpuModel* g = R.getModel(mp)) wm.models.push_back(g); wm.stem = fs::path(gp).stem().string(); wm.skel = R.alib.skeletonFor(wm.stem); }
            it = weaponModels.emplace(weaponFile, std::move(wm)).first;
        }
        WModel& wm = it->second; if (wm.models.empty()) return;
        int wc = weaponClass(weaponFile); float W1[16], W2[16]; mat4mul(W1, M, &handBones[(size_t)hb * 16]); mat4mul(W2, W1, wc == 1 ? off1 : wc == 2 ? off2 : off0);
        const vb::GrAnimation* wa = nullptr; float wt = 0.f;                                  // the weapon's own firing animation (recoil, barrel spin) while attacking
        if (ent && ent->attackT > 0.f && wm.skel) { wa = R.alib.animation(wm.stem + (wc == 2 ? "_Burst" : "_Single")); if (!wa) wa = R.alib.animation(wm.stem + "_Burst"); if (!wa) wa = R.alib.animation(wm.stem + "_Single"); wt = ent->attackDur - ent->attackT; if (wa && wa->duration > 0) wt = std::min(wt, wa->duration - 0.001f); }
        R.drawActor(wm.models, W2, wm.skel, wa, wt, useTex);
        if (ent) {                                                                            // remember the weapon bones' world matrices for the effects
            HeldPose& hp = heldPose[ent->id]; hp.valid = true; std::memcpy(hp.W2, W2, 64); hp.bone.clear();
            if (wm.skel) for (size_t i = 0; i < wm.skel->bones.size() && (i + 1) * 16 <= R.boneWorld.size(); i++) { float T[16]; mat4mul(T, W2, &R.boneWorld[i * 16]); std::array<float, 16> a; std::memcpy(a.data(), T, 64); hp.bone[wm.skel->bones[i].name] = a; }
            if (std::getenv("VB_DEBUG_MUZZLE")) { auto mz = hp.bone.find("FX_Muzzle"), g1 = hp.bone.find("Weapon_1"); if (mz != hp.bone.end() && g1 != hp.bone.end()) { const auto& m = mz->second; const auto& g = g1->second; std::fprintf(stderr, "%s: muzzle-grip dir (%.2f %.2f %.2f)  muzzle X (%.2f %.2f %.2f) Y (%.2f %.2f %.2f) Z (%.2f %.2f %.2f)\n", weaponFile.c_str(), m[12] - g[12], m[13] - g[13], m[14] - g[14], m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]); } }
            vb::WeaponStats ws = world.weaponStats(weaponFile);
            if (!ws.heldEffect.empty() && !ent->dead) {                                       // a looping effect while the weapon is held
                auto g = heldGroup.find(ent->id);
                if (g != heldGroup.end() && heldFile[ent->id] != weaponFile) { vfx.stop(g->second); heldGroup.erase(g); g = heldGroup.end(); }
                if (g == heldGroup.end() || !vfx.alive(g->second)) { heldGroup[ent->id] = vfx.start(ws.heldEffect, heldAnchor(ent)); heldFile[ent->id] = weaponFile; }
            } else if (heldGroup.count(ent->id)) { vfx.stop(heldGroup[ent->id]); heldGroup.erase(ent->id); heldFile.erase(ent->id); }
            shotFx(ent, weaponFile);
        }
    };
    // footsteps: the walk / run animation's FL / FR cue times start the footprint / dust effect of the ground under the foot
    std::vector<float> actorBones;                                             // the pose of the actor drawn last (before its weapon replaces R.boneWorld)
    auto footsteps = [&](const vb::Entity* ent, const vb::GrAnimation* an, float t, bool run, const vb::GrSkeleton* sk, const float* M, const std::vector<float>& bones) {
        if (!an || an->duration <= 0.f || !ent) return;
        std::string stem = run ? "HumM_Com_Una_Run_01" : "HumM_Com_Una_Walk_01"; float ph = std::fmod(std::max(0.f, t), an->duration), prev = stepPhase.count(ent->id) ? stepPhase[ent->id] : ph; stepPhase[ent->id] = ph;
        for (auto& c : cues(stem)) {
            if (c.first != "FL" && c.first != "FR") continue; float ct = (float)c.second / 1000.f;
            bool crossed = prev <= ph ? (prev < ct && ct <= ph) : (ct > prev || ct <= ph); if (!crossed) continue;
            int cx, cz; nav.cellOf(ent->x, ent->z, cx, cz); int cell = nav.valid() && nav.in(cx, cz) ? L.col.at(cx, cz) : 1;
            const char* surf = L.mapStem.find("Vault") != std::string::npos ? "Metal" : cell == 9 ? "Asphalt" : "Sand";
            // the foot that is planted at the cue (the lower one) takes the print, wherever the cue's own name puts it
            int fl = -1, fr = -1; for (size_t bi = 0; bi < sk->bones.size(); bi++) { if (sk->bones[bi].name == "L Foot") fl = (int)bi; else if (sk->bones[bi].name == "R Foot") fr = (int)bi; }
            float px = ent->x, pz = ent->z; bool leftFoot = c.first == "FL";
            if (fl >= 0 && fr >= 0 && (size_t)(std::max(fl, fr) + 1) * 16 <= bones.size()) {
                float TL[16], TR[16]; mat4mul(TL, M, &bones[(size_t)fl * 16]); mat4mul(TR, M, &bones[(size_t)fr * 16]);
                leftFoot = TL[13] <= TR[13]; const float* T = leftFoot ? TL : TR; px = T[12]; pz = T[14];
            }
            VfxRuntime::Anchor a = anchorAt(px, ent->y, pz, ent->yaw);
            int gid = vfx.start(std::string("FX_Foot") + (leftFoot ? "L" : "R") + "_Hum_" + surf + "_Base_01", a); if (std::getenv("VB_DEBUG_FX")) std::fprintf(stderr, "footstep %s %s group %d at %.1f %.1f\n", c.first.c_str(), surf, gid, ent->x, ent->z);
        }
    };
    // Fog of war and entities. People, creatures and loose items are only seen inside the lit disc; doors, containers and props are scenery: they stay drawn (dim, like the map)
    // and can be clicked. `entVisible` is the single test used for drawing, picking and the cursor.
    auto fogHides = [&](const vb::Entity* e) { return e && (e->kind == vb::EntKind::Critter || e->kind == vb::EntKind::Item); };
    auto entVisible = [&](const vb::Entity& e) {
        if (fogMode == 0) return true;
        if (fogHides(&e)) return litEnt(e.x, e.z, e.y + 3.f) >= kLitMin;
        if (fogMode == 1) return true;                                             // tech-demo style: the whole map is dimly visible
        if (litEnt(e.x, e.z, e.y + 3.f) >= kLitMin) return true;
        int cx = (int)std::floor((e.x - fogMask.ox) / 0.5f), cz = (int)std::floor((e.z - fogMask.oz) / 0.5f);       // remembered style: only where already explored
        return cx >= 0 && cz >= 0 && cx < fogMask.w && cz < fogMask.h && fogMask.px[(size_t)cz * fogMask.w + cx] > 0;
    };
    // nearest interactive entity under the mouse ray (people, doors, containers): distance from the ray to a sphere around the entity
    auto pickEntity = [&](const V3& o, const V3& d) -> int {
        int best = 0; float bestT = 1e30f;
        for (size_t id = 2; id <= world.count(); id++) {
            vb::Entity* e = world.find((int)id);
            if (!e || !e->active || e->destroyed || !entVisible(*e) || !world.usable(*e)) continue;
            if (fogMode != 0 && litEnt(e->x, e->z, e->y + 3.f) < kLitMin) continue;       // outside the light circle nothing can be picked (no glow, no cursor, no click), also scenery
            float r;
            if (e->kind == vb::EntKind::Critter) r = e->dead ? (e->cargo.empty() ? 0.f : 2.8f) : 3.2f;       // a body with something on it is a container
            else if (e->kind == vb::EntKind::Door) r = 4.5f;
            else if (e->kind == vb::EntKind::Container) r = 3.2f;
            else if (e->kind == vb::EntKind::Usable) r = 4.f;
            else if (e->kind == vb::EntKind::Item) r = 2.2f;
            else continue;
            if (r <= 0) continue;
            V3 c{e->x, e->y + (e->kind == vb::EntKind::Door ? 4.f : e->dead ? 1.2f : 3.f), e->z}, oc = c - o;
            float t = dot(oc, d); if (t < 0) continue;
            V3 closest = o + d * t; V3 df = c - closest;
            if (dot(df, df) < r * r && t < bestT) { bestT = t; best = (int)id; }
        }
        return best;
    };
    auto chooseSkill = [&](int idx) {                              // an entry of the skill menu (anything else = Cancel): walk up to the target, the skill runs on arrival
        if (idx >= 0 && idx < (int)skm.opts.size()) { vb::Entity* te = world.find(skm.ent); if (te && walkToEntity(te)) { pendingUse = skm.ent; pendingSkill = skm.opts[(size_t)idx]; pendingIsSkill = true; attackOrder = 0; } else note("You can't reach that."); }
        if (std::getenv("VB_DEBUG_SKILL")) std::fprintf(stderr, "chooseSkill %d of %zu\n", idx, skm.opts.size());
        skm.open = false;
    };
    auto handleClick = [&](float px, float py) {
        V3 o, d;
        if (!pickRay(lastVP, px, py, lastW, lastH, o, d)) return;
        if (world.dialogue().active || world.playerDead()) return;
        if (useT > 0.f) return;                                    // busy with the use animation
        int hitEnt = pickEntity(o, d); pendingIsSkill = false;
        if (skillArm >= 0) {                                       // a kit / the Use Skill button is armed: the target's script lists what the skill can do to it
            int arm = skillArm; skillArm = -1;
            std::vector<vb::SkillOption> all = hitEnt ? world.usableSkills(hitEnt) : std::vector<vb::SkillOption>(), opts;
            std::string only = arm < 2 ? world.itemInfo(world.equipped[vb::SlotHand1 + arm]).skill : std::string();     // a kit offers only its own skill
            for (const vb::SkillOption& so : all) if (only.empty() || so.label.compare(0, only.size(), only) == 0) opts.push_back(so);
            if (opts.empty()) note(hitEnt ? "Nothing can be done with that." : "");
            else { skm.open = true; skm.ent = hitEnt; skm.x = px; skm.y = py; skm.opts = opts; }
            return;
        }
        if (attackHand >= 0) {                                     // a weapon button is armed: the next click picks the target
            vb::Entity* te = hitEnt ? world.find(hitEnt) : nullptr;
            if (te && te->kind == vb::EntKind::Critter && !te->dead) { world.selectHand(attackHand); attackOrder = hitEnt; pendingUse = 0; }
            else attackHand = -1;                                  // anything else cancels
            return;
        }
        attackOrder = 0;
        if (world.inCombat() && !world.playerTurn()) { note("It is not your turn."); return; }
        if (hitEnt) { vb::Entity* e = world.find(hitEnt); if (!walkToEntity(e)) { note("You can't reach that."); pendingUse = 0; return; } pendingUse = hitEnt; return; }       // people, doors, containers, items: walk up (and use them)
        pendingUse = 0;
        V3 hit;
        if (rayFloor(L.tre, o, d, 0.5f, hit, &L)) walkTo(hit.x, hit.z);
    };

    struct SelDisc { std::string name; gm::GpuModel* model = nullptr; float scale = 0.3f, alphaMin = 0.4f, alphaFreq = 1.f, cu = 0.5f, cv = 0.5f, spin = 0.f; std::array<float, 3> col{0.27f, 1.f, 0.98f}; };
    bool selLoaded = false; vb::VfxFile selFx; std::vector<SelDisc> selDiscs;       // the hover selection effect, read from the game's own effect file
    auto windowUp = [&]() { return panel != 0 || iw.containerId || journalOpen; };       // any window (inventory, container, character, options, journal): the game is paused and the camera frozen
    auto camLocked = [&]() { return windowUp() || world.dialogue().active; };              // a conversation freezes the camera too (the world keeps running: speech and replies are driven by it)
    bool done = false; int frame = 0, fpsFrames = 0; float fps = 0; Uint32 last = SDL_GetTicks(), fpsT = last;
    float simLeft = simTime; bool clickDone = false; bool talkDone = talkTo.empty();
    // Main menu: the seven buttons of Interface/f3_front_end_buttons.tga (192 x 276, one row each), laid out in 1024 x 768 design units like the original (left column, the logo bottom right)
    static const float kMenuRowY[4] = {0, 66, 132, 198}, kMenuRowH = 51.f, kMenuBtnW = 256.f, kMenuX = 24.f, kMenuY = 24.f;       // data/menu_buttons.png: 256 x 248, four 51-unit rows (Start New Game, Map Browser, Object Browser, Exit Game)
    R.mtex.addFile("vb_menu_buttons", "embedded:/menu_buttons.tga");
    bool toMenuReq = false;                                                 // Options > Quit to main menu
    int menuDown = -1, menuPress = -1, menuFrames = 0; float menuT = std::getenv("VB_MENUT") ? (float)std::atof(std::getenv("VB_MENUT")) : 0.f;       // seconds since the menu appeared (VB_MENUT=s: test offset)
    auto menuScale = [&](int dw, int dh, float& S, float& x0, float& y0) { S = (float)dh / 768.f; if (1024.f * S > (float)dw) S = (float)dw / 1024.f; x0 = ((float)dw - 1024.f * S) * 0.5f; y0 = ((float)dh - 768.f * S) * 0.5f; };
    bool objOpen = false, mbOpen = false;                                    // Object Browser / Map Browser open: the main menu buttons are hidden
    auto menuAt = [&](float mx, float my, int dw, int dh) { if (objOpen || mbOpen) return -1; float S, x0, y0; menuScale(dw, dh, S, x0, y0); for (int i = 0; i < 4; i++) { float bx = x0 + kMenuX * S, by = y0 + (kMenuY + kMenuRowY[i]) * S; if (mx >= bx && mx < bx + kMenuBtnW * S && my >= by && my < by + kMenuRowH * S) return i; } return -1; };
    // Map Browser (main menu button 2): a list of every map on the left; the chosen map is shown on its own in a free camera (no player, no sounds, no fog of war).
    // the game's own camera: WASD pan, Q / E rotate, wheel zoom (a wider zoom range than in play), dragging with either mouse button tilts / turns the view
    int mbSel = -1, mbTop = 0, mbWant = -1; bool mbWantDrawn = false, mvOk = false; int mbDrag = 0;       // mbDrag: 1 = left button (turn / tilt), 2 = right button (pan)
     std::vector<int> mbMaps; Level MV; std::string mvName, mvErr; GameCamera mvGc;
    for (size_t i = 0; i < maps.size(); i++) if (maps[i] != "Mainmenu") mbMaps.push_back((int)i);
    struct MbBox { float x, y, w, h, lh, my, vx; int rows; };
    auto mbBox = [&](int dw, int dh) { MbBox b; b.lh = 20.f; b.x = 16.f; b.w = std::min(340.f, std::max(220.f, (float)dw * 0.3f)); b.y = 16.f; b.h = (float)dh - 32.f; b.my = b.y + 40.f; b.rows = std::max(4, (int)((b.h - 40.f - 28.f) / b.lh)); b.vx = b.x + b.w + 8.f; return b; };
    auto mbClamp = [&](int dw, int dh) { mbTop = std::max(0, std::min(mbTop, std::max(0, (int)mbMaps.size() - mbBox(dw, dh).rows))); };
    auto mbVisible = [&](int dw, int dh) { MbBox b = mbBox(dw, dh); if (mbSel >= 0) { if (mbSel < mbTop) mbTop = mbSel; if (mbSel >= mbTop + b.rows) mbTop = mbSel - b.rows + 1; } mbClamp(dw, dh); };
    auto mbAt = [&](float mx, float my, int dw, int dh) -> int { MbBox b = mbBox(dw, dh); if (mx < b.x + 6.f || mx >= b.x + b.w - 6.f || my < b.my || my >= b.my + (float)b.rows * b.lh) return -1;
        int r = mbTop + (int)((my - b.my) / b.lh); return r >= 0 && r < (int)mbMaps.size() ? r : -1; };
    bool mvShowEnts = !(std::getenv("VB_MAPOBJ") && std::atoi(std::getenv("VB_MAPOBJ")) == 0), mbToggleDown = false;                            // O / the check box at the bottom of the list: show or hide the map's objects (people, creatures, doors, containers, props)
    auto mbToggleAt = [&](float mx, float my, int dw, int dh) { MbBox b = mbBox(dw, dh); return mx >= b.x + 8.f && mx < b.x + b.w * 0.62f && my >= b.y + b.h - 28.f && my < b.y + b.h - 4.f; };
    auto mbSelect = [&](int k) { if (k < 0 || k >= (int)mbMaps.size()) return; mbSel = k; mbWant = mbMaps[(size_t)k]; mbWantDrawn = false; };       // (loaded a frame later, after "Loading ..." has been shown)
    auto mbLoad = [&](int idx) {
        mvName = maps[(size_t)idx]; mvErr.clear(); mvOk = loadLevel(assets, mvName, R.lib, MV, mvErr); if (!mvOk) return;
        R.buildEntities(MV); V3 c = (MV.cmin + MV.cmax) * 0.5f; float ext = std::max(MV.cmax.x - MV.cmin.x, MV.cmax.z - MV.cmin.z), gy = c.y;
        groundHeight(MV.tre, c.x, c.z, c.y, 1000.f, gy); mvGc.target = {c.x, gy, c.z}; mvGc.yaw = mvGc.goalYaw = 0.f; mvGc.pitch = 38.5f * kPi / 180.f;
        mvGc.fov = std::max(GameCamera::kMinFov, std::min(60.f, 2.f * std::atan(ext * 0.55f / GameCamera::kDist) * 180.f / kPi));       // starts zoomed out to fit the whole map
        if (const char* cm = std::getenv("VB_MAPCAM")) { float a[5]; if (std::sscanf(cm, "%f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4]) == 5) { mvGc.target.x = a[0]; mvGc.target.z = a[1]; mvGc.yaw = mvGc.goalYaw = a[2] * kPi / 180.f; mvGc.pitch = a[3] * kPi / 180.f; mvGc.fov = a[4]; } }       // test hook: target x z, yaw / pitch degrees, fov
        std::printf("map browser: %s: %zu triangles, %zu entities\n", mvName.c_str(), MV.tris, MV.ents.size()); };
    auto mbClose = [&]() { mbOpen = false; mbDrag = 0; MV.destroy(); mvOk = false; mbWant = -1; mvName.clear(); };
    // Object Browser (main menu button 3): every model of the game; the typed text filters the list, a click shows the model (drag = orbit, wheel = zoom, Left / Right = animation)
    bool objSpin = true, objDrag = false, objBuilt = false; int objSel = -1, objTop = 0, objAnimTop = 0, objAnim = -1; float objYaw = 35.f, objPitch = 15.f, objDist = 2.6f, objAnimT = 0.f;
    std::string objFilter, objPickEnv; std::vector<std::string> objLabels, objLabelsLow, objAnims; std::vector<int> objShown; gm::GpuModel* objModel = nullptr; const vb::GrSkeleton* objSkel = nullptr; std::string objStem;
    struct ObjBox { float x, y, w, h, lh, my, ayt, ay, vx; int mrows, arows; };      // the left panel: model list (my = first row), animation list (ayt = its title, ay = first row); vx = where the 3D view starts
    auto objBox = [&](int dw, int dh) { ObjBox b; b.lh = 20.f; b.x = 16.f; b.w = std::min(340.f, std::max(220.f, (float)dw * 0.3f)); b.y = 16.f; b.h = (float)dh - 32.f; b.my = b.y + 64.f;
        int R = std::max(8, (int)((b.h - 64.f - 22.f - 34.f) / b.lh)); b.mrows = std::max(4, R * 55 / 100); b.arows = std::max(4, R - b.mrows);
        b.ayt = b.my + (float)b.mrows * b.lh + 8.f; b.ay = b.ayt + 24.f; b.vx = b.x + b.w + 8.f; return b; };
    auto objRefilter = [&](int keepLabel) {
        objShown.clear(); std::string f = objFilter; for (char& c : f) c = (char)std::tolower((unsigned char)c);
        for (size_t k = 0; k < objLabels.size(); k++) if (f.empty() || objLabelsLow[k].find(f) != std::string::npos) objShown.push_back((int)k);
        objSel = -1; for (size_t k = 0; k < objShown.size(); k++) if (objShown[k] == keepLabel) objSel = (int)k;
        objTop = std::max(0, std::min(objTop, std::max(0, (int)objShown.size() - 3))); };
    auto objSelect = [&](int shownIdx) {
        if (shownIdx < 0 || shownIdx >= (int)objShown.size()) return; objSel = shownIdx;
        vb::ModelPart mp; mp.g3d_path = assets + "/" + objLabels[(size_t)objShown[(size_t)shownIdx]] + ".g3d";
        objModel = R.getModel(mp); objSkel = nullptr; objAnims.clear(); objAnim = -1; objAnimTop = 0; objAnimT = 0.f; objStem = fs::path(mp.g3d_path).stem().string();
        if (objModel && objModel->skinned) {
            objSkel = R.alib.skeletonFor(objStem); objAnims = R.alib.animationsFor(objStem);       // (starts in the rest pose: no animation until one is picked)
        } objAnimTop = 0; };
    auto objClamp = [&](int dw, int dh) { ObjBox b = objBox(dw, dh); objTop = std::max(0, std::min(objTop, std::max(0, (int)objShown.size() - b.mrows))); objAnimTop = std::max(0, std::min(objAnimTop, std::max(0, (int)objAnims.size() + 1 - b.arows))); };      // scrolling stays inside the lists only
    auto objVisible = [&](int dw, int dh) { ObjBox b = objBox(dw, dh); if (objSel >= 0) { if (objSel < objTop) objTop = objSel; if (objSel >= objTop + b.mrows) objTop = objSel - b.mrows + 1; }       // (after a selection change: bring the highlight into view)
        if (objAnim + 1 < objAnimTop) objAnimTop = objAnim + 1; if (objAnim + 1 >= objAnimTop + b.arows) objAnimTop = objAnim + 2 - b.arows; objClamp(dw, dh); };
    auto objOpenClose = [&](bool on) {
        objOpen = on; if (on) { if (mbOpen) mbClose(); SDL_StartTextInput();
            if (!objBuilt) { objBuilt = true; for (const std::string& pth : vb::vfs::walk(assets)) { if (pth.size() < 5 || pth.compare(pth.size() - 4, 4, ".g3d") != 0) continue;
                std::string l = pth.substr(assets.size() + 1); l.resize(l.size() - 4); objLabels.push_back(l); }
                std::sort(objLabels.begin(), objLabels.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
                for (const std::string& l : objLabels) { std::string x = l; for (char& c : x) c = (char)std::tolower((unsigned char)c); objLabelsLow.push_back(x); }
                objRefilter(-1); }
        } else { SDL_StopTextInput(); objDrag = false; } };
    auto mbOpenNow = [&]() { if (objOpen) objOpenClose(false); mbOpen = true; mbDrag = 0; };
    auto objAt = [&](float mx, float my, int dw, int dh) -> int { ObjBox b = objBox(dw, dh); if (mx < b.x + 6.f || mx >= b.x + b.w - 6.f || my < b.my || my >= b.my + (float)b.mrows * b.lh) return -1;
        int r = objTop + (int)((my - b.my) / b.lh); return r >= 0 && r < (int)objShown.size() ? r : -1; };
    auto objAnimAt = [&](float mx, float my, int dw, int dh) -> int { ObjBox b = objBox(dw, dh); if (objAnims.empty() || mx < b.x + 6.f || mx >= b.x + b.w - 6.f || my < b.ay || my >= b.ay + (float)b.arows * b.lh) return -1;
        int r = objAnimTop + (int)((my - b.ay) / b.lh); return r >= 0 && r <= (int)objAnims.size() ? r : -1; };       // 0 = rest pose, k = animation k - 1
    int mbDownRow = -1, objDownRow = -1, objAnimDownRow = -1;               // the list row a left press started on: releasing over another row (after a drag that began elsewhere) selects nothing
    while (!done) {
        int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh); int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { done = true; continue; }
            if (menuMode) {                                    // the main menu takes only its buttons (and Escape = leave)
                if (objOpen && shot.empty() && e.type == SDL_TEXTINPUT) { objFilter += e.text.text; objRefilter(objSel >= 0 ? objShown[(size_t)objSel] : -1); }
                else if (objOpen && shot.empty() && e.type == SDL_KEYDOWN && e.key.keysym.sym != SDLK_ESCAPE) {
                    SDL_Keycode kc = e.key.keysym.sym; int n = (int)objShown.size(), pg = objBox(dw, dh).mrows - 1;
                    if (kc == SDLK_BACKSPACE && !objFilter.empty()) { objFilter.pop_back(); objRefilter(objSel >= 0 ? objShown[(size_t)objSel] : -1); }
                    else if (kc == SDLK_UP || kc == SDLK_DOWN || kc == SDLK_PAGEUP || kc == SDLK_PAGEDOWN || kc == SDLK_HOME || kc == SDLK_END) {
                        int t = objSel < 0 ? 0 : objSel; if (kc == SDLK_UP) t--; else if (kc == SDLK_DOWN) t++; else if (kc == SDLK_PAGEUP) t -= pg; else if (kc == SDLK_PAGEDOWN) t += pg; else if (kc == SDLK_HOME) t = 0; else t = n - 1;
                        if (n) { objSelect(std::max(0, std::min(n - 1, t))); objVisible(dw, dh); } }
                    else if ((kc == SDLK_LEFT || kc == SDLK_RIGHT) && !objAnims.empty()) { int cnt = (int)objAnims.size() + 1; objAnim = ((objAnim + 1) + (kc == SDLK_RIGHT ? 1 : -1) + cnt) % cnt - 1; objAnimT = 0.f; objVisible(dw, dh); }
                    else if (kc == SDLK_SPACE && objFilter.empty()) objSpin = !objSpin; }
                else if (objOpen && shot.empty() && e.type == SDL_MOUSEMOTION && objDrag) { objYaw += e.motion.xrel * 0.4f; objPitch = std::max(-89.f, std::min(89.f, objPitch + e.motion.yrel * 0.4f)); objSpin = false; }
                else if (objOpen && shot.empty() && e.type == SDL_MOUSEWHEEL) { ObjBox ob = objBox(dw, dh);
                    if (mouseX >= ob.x && mouseX < ob.x + ob.w && mouseY >= ob.y && mouseY < ob.ayt) { objTop -= e.wheel.y * 3; objClamp(dw, dh); }
                    else if (mouseX >= ob.x && mouseX < ob.x + ob.w && mouseY >= ob.ayt && mouseY < ob.y + ob.h) { objAnimTop -= e.wheel.y * 3; objClamp(dw, dh); }
                    else objDist = std::max(1.2f, std::min(20.f, objDist * (e.wheel.y > 0 ? 0.9f : 1.1f))); }
                else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && shot.empty()) { if (objOpen) { objOpenClose(false); audio.play("f3_messagebox_close.wav", 0, 0, 0, false, false); } else if (mbOpen) { mbClose(); audio.play("f3_messagebox_close.wav", 0, 0, 0, false, false); } else done = true; }
                else if (mbOpen && shot.empty() && e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_o) mvShowEnts = !mvShowEnts;
                else if (mbOpen && shot.empty() && e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN || e.key.keysym.sym == SDLK_PAGEUP || e.key.keysym.sym == SDLK_PAGEDOWN || e.key.keysym.sym == SDLK_HOME || e.key.keysym.sym == SDLK_END)) {
                    SDL_Keycode kc = e.key.keysym.sym; int n = (int)mbMaps.size(), t = mbSel < 0 ? 0 : mbSel, pg = mbBox(dw, dh).rows - 1;
                    if (kc == SDLK_UP) t--; else if (kc == SDLK_DOWN) t++; else if (kc == SDLK_PAGEUP) t -= pg; else if (kc == SDLK_PAGEDOWN) t += pg; else if (kc == SDLK_HOME) t = 0; else t = n - 1;
                    mbSelect(std::max(0, std::min(n - 1, t))); mbVisible(dw, dh); }
                else if (mbOpen && shot.empty() && e.type == SDL_MOUSEWHEEL) { MbBox b = mbBox(dw, dh); if (mouseX >= b.x && mouseX < b.x + b.w) { mbTop -= e.wheel.y * 3; mbClamp(dw, dh); } else mvGc.fov = std::max(GameCamera::kMinFov, std::min(60.f, mvGc.fov * (e.wheel.y > 0 ? 0.9f : 1.0f / 0.9f))); }
                else if (mbOpen && shot.empty() && e.type == SDL_MOUSEMOTION && mbDrag) {
                    float dx = e.motion.xrel * (float)dw / ww, dy = e.motion.yrel * (float)dh / wh;
                    if (mbDrag == 1) { mvGc.yaw += dx * 0.004f; mvGc.goalYaw = mvGc.yaw; mvGc.pitch = std::max(10.f * kPi / 180.f, std::min(89.f * kPi / 180.f, mvGc.pitch + dy * 0.004f)); }       // left: turn and tilt
                    else {       // right: pan, the ground follows the cursor (units per pixel at the target; a vertical pixel covers 1 / sin(tilt) of ground)
                        float wpp = 2.f * GameCamera::kDist * std::tan(mvGc.fov * kPi / 360.f) / (float)dh; V3 f{std::sin(mvGc.yaw), 0, std::cos(mvGc.yaw)}, r = norm(cross({0, 1, 0}, f));
                        mvGc.target = mvGc.target - r * (dx * wpp) + f * (dy * wpp / std::sin(mvGc.pitch));
                        mvGc.target.x = std::max(MV.cmin.x, std::min(MV.cmax.x, mvGc.target.x)); mvGc.target.z = std::max(MV.cmin.z, std::min(MV.cmax.z, mvGc.target.z)); } }
                else if (mbOpen && shot.empty() && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) { if (e.button.x * (float)dw / ww >= mbBox(dw, dh).vx) mbDrag = 2; }
                else if (mbOpen && shot.empty() && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) { if (mbDrag == 2) mbDrag = 0; }
                else if (shot.empty() && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) { { float px = e.button.x * (float)dw / ww, py = e.button.y * (float)dh / wh; mbDownRow = mbOpen ? mbAt(px, py, dw, dh) : -1; objDownRow = objOpen ? objAt(px, py, dw, dh) : -1; objAnimDownRow = objOpen ? objAnimAt(px, py, dw, dh) : -1; mbToggleDown = mbOpen && mbToggleAt(px, py, dw, dh); }
                    menuDown = menuAt(e.button.x * (float)dw / ww, e.button.y * (float)dh / wh, dw, dh); if (menuDown >= 0) audio.play("f3_button_down_01.wav", 0, 0, 0, false, false); else if (objOpen && e.button.x * (float)dw / ww >= objBox(dw, dh).vx) objDrag = true; else if (mbOpen && e.button.x * (float)dw / ww >= mbBox(dw, dh).vx) mbDrag = 1; }
                else if (shot.empty() && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) { int b = menuAt(e.button.x * (float)dw / ww, e.button.y * (float)dh / wh, dw, dh); if (menuDown >= 0) audio.play("f3_button_up_01.wav", 0, 0, 0, false, false); objDrag = false; if (b >= 0 && b == menuDown) menuPress = b; else if (objOpen && menuDown < 0) { int r = objAt(e.button.x * (float)dw / ww, e.button.y * (float)dh / wh, dw, dh); if (r >= 0 && r == objDownRow) { objSelect(r); audio.play("f3_button_down_01.wav", 0, 0, 0, false, false); } else if (r < 0) { int a = objAnimAt(e.button.x * (float)dw / ww, e.button.y * (float)dh / wh, dw, dh); if (a >= 0 && a == objAnimDownRow) { objAnim = a - 1; objAnimT = 0.f; audio.play("f3_button_down_01.wav", 0, 0, 0, false, false); } } } else if (mbOpen && menuDown < 0) { int r = mbAt(e.button.x * (float)dw / ww, e.button.y * (float)dh / wh, dw, dh); if (r >= 0 && r == mbDownRow) { mbSelect(r); audio.play("f3_button_down_01.wav", 0, 0, 0, false, false); } else if (mbToggleDown && mbToggleAt(e.button.x * (float)dw / ww, e.button.y * (float)dh / wh, dw, dh)) { mvShowEnts = !mvShowEnts; audio.play("f3_button_down_01.wav", 0, 0, 0, false, false); } } menuDown = -1; if (mbDrag == 1) mbDrag = 0; }
                continue;
            }
            if (!shot.empty()) continue;                       // scripted runs ignore real input
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: if (skm.open) skm.open = false; else if (skillArm >= 0) skillArm = -1; else if (world.dialogue().active) world.endDialogue(); else if (attackHand >= 0) attackHand = -1; else if (iw.containerId) iw.containerId = 0; else if (journalOpen) journalOpen = false; else if (panel) panel = 0; else panel = 3; break;
                    case SDLK_j: journalOpen = !journalOpen; break;
                    case SDLK_RETURN: if (world.playerTurn() && !world.playerDead()) { world.endTurn(); attackHand = -1; attackOrder = 0; pl.moving = false; pl.path.clear(); haveDest = false; } else if (world.playerDead()) { world.revivePlayer(); deathT = 0; attackHand = -1; attackOrder = 0; openMap(curMap); note("You wake up where you started."); } break;
                    case SDLK_i: panel = panel == 1 ? 0 : 1; break;
                    case SDLK_k: panel = panel == 2 ? 0 : 2; break;
                    case SDLK_z: speechInFog = !speechInFog; note(speechInFog ? "speech text is shown in the fog" : "speech text is hidden in the fog"); break;
                    case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: case SDLK_6: case SDLK_7: case SDLK_8: case SDLK_9:
                        if (world.dialogue().active) world.chooseReply(e.key.keysym.sym - SDLK_1);
                        break;
                    case SDLK_SPACE: if (!camLocked()) cam.target = pl.pos; break;
                    case SDLK_c: colOverlay = !colOverlay; break;
                    case SDLK_m: markers = !markers; break;
                    case SDLK_t: useTex = !useTex; break;
                    case SDLK_l: useLm = !useLm; break;
                    case SDLK_f: fogMode = (fogMode + 2) % 3; if (fogMode == 2 && !kRememberedFog) fogMode = (fogMode + 2) % 3; break;       // 1 -> 2 -> 0 -> 1
                    case SDLK_x: wire = !wire; break;
                    case SDLK_h: hud = !hud; break;
                    case SDLK_F1: help = !help; break;
                    case SDLK_LEFTBRACKET: openMap((curMap + (int)maps.size() - 1) % (int)maps.size()); break;
                    case SDLK_RIGHTBRACKET: openMap((curMap + 1) % (int)maps.size()); break;
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                if (panel == 1 && iw.invOver(mouseX, mouseY, dw, dh)) { iw.invWheel(mouseX, mouseY, dw, dh, e.wheel.y > 0 ? 1 : -1); continue; }
                if (camLocked()) continue;
                cam.fov = std::max(GameCamera::kMinFov, std::min(GameCamera::kMaxFov, cam.fov * (e.wheel.y > 0 ? 0.9f : 1.0f / 0.9f)));      // wheel up = zoom in = narrower FOV
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                float mx = e.button.x * (float)dw / ww, my = e.button.y * (float)dh / wh;
                if (panel == 1) iw.invUp(mx, my, dw, dh);
                std::string b = bar.buttonAt(mx, my, dw, dh);
                if (!barPressed.empty()) audio.play("f3_button_up_01.wav", 0, 0, 0, false, false);
                if (!barPressed.empty() && b == barPressed) {
                    if (b == "btnInventory") panel = panel == 1 ? 0 : 1;
                    else if (b == "btnCharacter") panel = panel == 2 ? 0 : 2;
                    else if (b == "btnOptions") panel = panel == 3 ? 0 : 3;
                    else if (b == "btnPipBoy") journalOpen = !journalOpen;
                    else if (b == "btnSneak") {                                                // End Turn (only live in combat)
                        if (world.playerTurn() && !world.playerDead()) { world.endTurn(); attackHand = -1; attackOrder = 0; pl.moving = false; pl.path.clear(); haveDest = false; }
                        else if (world.inCombat() && !world.playerDead()) note("It is not your turn.");
                    }
                    else if (b == "btnUseSkill") { if (world.playerDead() || (world.inCombat() && !world.playerTurn())) {} else { skillArm = skillArm == 2 ? -1 : 2; attackHand = -1; skm.open = false; } }
                    else if (b == "btnWeapon01" || b == "btnWeapon02") {                       // the weapon buttons are the combat actions: attack (arm it, then click a target) or, with an empty gun, reload
                        int h = b == "btnWeapon01" ? 0 : 1;
                        if (world.playerDead()) {}
                        else if (world.inCombat() && !world.playerTurn()) note("It is not your turn.");
                        else {
                            world.selectHand(h); const std::string& wf = world.equipped[vb::SlotHand1 + h];
                            bool kit = !wf.empty() && !world.itemInfo(wf).skill.empty();
                            if (kit) { skillArm = skillArm == h ? -1 : h; attackHand = -1; skm.open = false; }       // a kit in the hand: "USE ON" arms its skill
                            bool gun = !wf.empty() && world.itemInfo(wf).kind == "weapon" && !world.weaponStats(wf).melee;
                            int clip = world.clipOf(vb::SlotHand1 + h);
                            if (kit) {}
                            else if (gun && clip == 0) { attackHand = -1; world.reload(); }
                            else { attackHand = attackHand == h ? -1 : h; skillArm = -1; }
                        }
                    }
                }
                barPressed.clear();
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                float mx = e.button.x * (float)dw / ww, my = e.button.y * (float)dh / wh;
                if (skm.open) {                                                                // the skill menu takes the click: an entry (the last one is Cancel) or anywhere else closes it
                    int pick = -1; for (const auto& r : skRects) if (mx >= r[0] && mx < r[2] && my >= r[1] && my < r[3]) pick = (int)r[4];
                    chooseSkill(pick); continue;
                }
                if (iw.containerId) { if (iw.containerOver(mx, my, dw, dh)) iw.containerDown(mx, my, dw, dh); continue; }       // the container window is modal
                if (bar.over(mx, my, dw, dh)) { barPressed = bar.buttonAt(mx, my, dw, dh); if (!barPressed.empty()) audio.play("f3_button_down_01.wav", 0, 0, 0, false, false); continue; }       // clicks on the bottom bar never reach the map
                bool hitReply = false;
                if (world.dialogue().active) for (const ReplyRect& rr : replyRects) if (my >= rr.y0 && my < rr.y1) { world.chooseReply(rr.idx); hitReply = true; break; }
                if (panel == 3) { for (const ReplyRect& rr : replyRects) if (my >= rr.y0 && my < rr.y1) { if (rr.idx == 0) panel = 0; else if (rr.idx == 1) toMenuReq = true; else done = true; hitReply = true; break; } }
                if (panel == 1 && iw.invOver(mx, my, dw, dh)) { iw.invDown(mx, my, dw, dh); continue; }
                if (!hitReply && (panel || journalOpen)) continue;                 // an open window takes the click: the world never sees it
                if (!hitReply) handleClick(mx, my);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                float mx = e.button.x * (float)dw / ww, my = e.button.y * (float)dh / wh;
                if (panel == 1 && iw.invOver(mx, my, dw, dh)) iw.invRight(mx, my, dw, dh);
            }
        }
        profMark(0);
        Uint32 now = SDL_GetTicks(); float dt = std::min(0.1f, (now - last) / 1000.f); last = now; if (menuMode) menuT += dt;
        if (!shot.empty()) dt = 1.f / 30.f;                      // deterministic stepping for screenshots
        if (windowUp() && shot.empty()) dt = 0.f;
        {   // message-box style windows (the F1 help, the Esc options / exit prompt) play the engine's MessageBox sounds
            static bool wasBox = false; bool box = !menuMode && (help || panel == 3);
            if (box != wasBox) { audio.play(box ? "f3_messagebox_open.wav" : "f3_messagebox_close.wav", 0, 0, 0, false, false); wasBox = box; }
        }
        static double gameClock = 0; gameClock += dt;
        if (++fpsFrames >= 20) { fps = fpsFrames * 1000.f / std::max<Uint32>(1, now - fpsT); fpsFrames = 0; fpsT = now; fpsSum += fps; fpsCount++; }

        { static int qm = std::getenv("VB_QUITMENU") ? std::atoi(std::getenv("VB_QUITMENU")) : -1; static int qf = 0; if (qm >= 0 && ++qf == qm) toMenuReq = true; }       // test hook: VB_QUITMENU=n quits to the menu at frame n
        if (toMenuReq) {      // back to the main menu: forget the game in progress and show the menu scene again
            toMenuReq = false; auto mm = std::find(maps.begin(), maps.end(), std::string("Mainmenu"));
            if (mm != maps.end()) {
                world.endDialogue(); world.resetGame(); msgLog.clear(); panel = 0; help = false; journalOpen = false; iw.containerId = 0; skm.open = false; skillArm = -1; attackHand = -1;
                menuMode = true; hud = false; if (mbOpen) mbClose(); if (objOpen) objOpenClose(false); menuT = 0.f; menuDown = -1; menuFrames = 0; openMap((int)(mm - maps.begin())); menuCast();
            }
        }
        if (menuPress >= 0) {      // a menu button was pressed: Start New Game opens Junktown, Exit Game leaves (Map Browser and Object Browser are not implemented yet)
            int b = menuPress; menuPress = -1;
            if (b == 3) done = true;
            else if (b == 2) { objOpenClose(!objOpen); audio.play(objOpen ? "f3_messagebox_open.wav" : "f3_messagebox_close.wav", 0, 0, 0, false, false); }
            else if (b == 1) { mbOpenNow(); audio.play("f3_messagebox_open.wav", 0, 0, 0, false, false); }
            else if (b == 0) { auto jt = std::find(maps.begin(), maps.end(), startMap); if (jt != maps.end()) { menuMode = false; hud = true; curMap = (int)(jt - maps.begin()); openMap(curMap); } }
        }
        if (mbWant >= 0 && mbWantDrawn) { int w = mbWant; mbWant = -1; mbWantDrawn = false; mbLoad(w); }       // Map Browser: the chosen map loads once "Loading ..." has been on screen
        // ---- camera: WASD pan on the ground plane, smooth 90-degree orbit steps
        const Uint8* k = SDL_GetKeyboardState(nullptr); static const Uint8 kNoKeys[SDL_NUM_SCANCODES] = {0}; if (camLocked()) k = kNoKeys;       // pan / rotate keys do nothing while a window is open
        V3 f{std::sin(cam.yaw), 0, std::cos(cam.yaw)}, r = norm(cross({0, 1, 0}, f));
        float sp = GameCamera::kPanSpeed * ((k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? 2.f : 1.f) * dt;
        if (k[SDL_SCANCODE_W]) cam.target = cam.target + f * sp;
        if (k[SDL_SCANCODE_S]) cam.target = cam.target - f * sp;
        if (k[SDL_SCANCODE_D]) cam.target = cam.target + r * sp;
        if (k[SDL_SCANCODE_A]) cam.target = cam.target - r * sp;
        cam.target.x = std::max(L.cmin.x, std::min(L.cmax.x, cam.target.x)); cam.target.z = std::max(L.cmin.z, std::min(L.cmax.z, cam.target.z));
        { float gy; if (groundHeight(L.tre, cam.target.x, cam.target.z, cam.target.y, 3.f, gy)) cam.target.y += (gy - cam.target.y) * std::min(1.f, dt * 8.f); }
        // Q / E: continuous rotation around the camera target while held
        { float rot = (k[SDL_SCANCODE_Q] ? 1.f : 0.f) - (k[SDL_SCANCODE_E] ? 1.f : 0.f); if (shot.empty()) { cam.goalYaw += rot * GameCamera::kRotSpeed * dt; cam.yaw = cam.goalYaw; } else cam.yaw += (cam.goalYaw - cam.yaw) * std::min(1.f, dt * 9.f); }

        // ---- player
        auto stepPlayer = [&](float step) {
            if (pl.moving && pl.next < pl.path.size()) {
                float tx = pl.path[pl.next].first - pl.pos.x, tz = pl.path[pl.next].second - pl.pos.z, d = std::sqrt(tx * tx + tz * tz);
                float want = std::atan2(tx, tz);
                float diff = std::remainder(want - pl.yaw, 2 * kPi); pl.yaw += std::max(-12.f * step, std::min(12.f * step, diff));
                float mv = pl.speed * step;
                if (world.inCombat()) {                                     // in combat every step costs action points, and only on the player's turn
                    float need = std::min(mv, d) / vb::World::kUnitsPerAp;
                    if (!world.playerTurn() || !world.spendPlayerAp(need)) { pl.moving = false; pl.path.clear(); haveDest = false; outOfAp = world.playerTurn(); return; }
                }
                if (mv >= d) { pl.pos.x = pl.path[pl.next].first; pl.pos.z = pl.path[pl.next].second; pl.next++; if (pl.next >= pl.path.size()) { pl.moving = false; haveDest = false; } }
                else { pl.pos.x += tx / d * mv; pl.pos.z += tz / d * mv; }
            }
            float gy; if (groundHeight(L.tre, pl.pos.x, pl.pos.z, pl.pos.y, 2.f, gy)) pl.pos.y += (gy - pl.pos.y) * std::min(1.f, step * 15.f);
            pl.animT += step;
        };
        bool wasMoving = pl.moving;
        if (world.dialogue().active) { pl.moving = false; pl.path.clear(); haveDest = false; }
        stepPlayer(dt);
        if (wasMoving != pl.moving) pl.animT = 0;
        // ---- the world: scripts, NPC actions, triggers
        bool scripted = world.playerScripted();
        if (scripted) { pl.path.clear(); haveDest = false; pendingUse = 0; }
        else world.setPlayer(pl.pos.x, pl.pos.y, pl.pos.z, pl.yaw);
        profMark(11); world.update(dt); profMark(1);
        for (auto it = propBlock.begin(); it != propBlock.end();) {            // a destroyed / removed prop no longer blocks
            vb::Entity* pe = world.find(it->first);
            if (!pe || pe->destroyed || !pe->active) { nav.openCells(it->second); it = propBlock.erase(it); } else ++it;
        }
        if (!world.playerScripted() && !world.dialogue().active) {          // people and creatures are solid: the protagonist is pushed out of them (they steer by their scripts)
            for (size_t id = 2; id <= world.count(); id++) {
                const vb::Entity* ce = world.find((int)id); if (!ce || ce->kind != vb::EntKind::Critter || ce->dead || !ce->active || ce->destroyed) continue;
                float dx = pl.pos.x - ce->x, dz = pl.pos.z - ce->z, d = std::hypot(dx, dz), minD = 1.5f;
                if (d >= minD || std::fabs(pl.pos.y - ce->y) > 6.f) continue;
                if (d < 0.01f) { dx = 1.f; dz = 0.f; d = 1.f; }
                float nx = ce->x + dx / d * minD, nz = ce->z + dz / d * minD;
                if (!nav.valid() || nav.walkableAt(nx, nz)) { pl.pos.x = nx; pl.pos.z = nz; }
            }
        }
        if (scripted || world.playerScripted()) {                  // a script walks the protagonist (it follows the world's entity)
            vb::Entity* p = world.player(); pl.pos = {p->x, p->y, p->z}; pl.yaw = p->yaw;
            if (std::getenv("VB_DEBUG_ENTS")) { float g1 = -99, g2 = -99; bool a1 = groundHeight(L.tre, pl.pos.x, pl.pos.z, pl.pos.y, 2.f, g1), a2 = groundHeight(L.tre, pl.pos.x, pl.pos.z, 20.f, 40.f, g2); std::fprintf(stderr, "scripted player %.1f %.2f %.1f moving=%d ground(hint)=%d:%.2f ground(any)=%d:%.2f\n", pl.pos.x, pl.pos.y, pl.pos.z, (int)p->moving, a1, g1, a2, g2); }
            cam.target.x += (pl.pos.x - cam.target.x) * std::min(1.f, dt * 4.f); cam.target.z += (pl.pos.z - cam.target.z) * std::min(1.f, dt * 4.f);   // the camera follows a scripted walk
            bool mv = p->moving; if (mv != pl.moving) { pl.moving = mv; pl.animT = 0; }
        }
        if (world.playerDead()) { pl.moving = false; pl.path.clear(); haveDest = false; attackOrder = 0; attackHand = -1; pendingUse = 0; deathT += dt; }
        if (attackOrder && !world.playerScripted()) {                      // attacking: walk into range (in combat that costs action points), then strike once
            vb::Entity* ct = world.find(attackOrder);
            if (!ct || ct->dead || ct->destroyed || !ct->active || !entVisible(*ct)) { attackOrder = 0; attackHand = -1; }
            else {
                vb::WeaponStats ws = world.weaponStats(world.playerWeapon());
                float dd = std::hypot(ct->x - pl.pos.x, ct->z - pl.pos.z);
                bool los = ws.melee || ws.fists || !nav.valid() || nav.lineClear(pl.pos.x, pl.pos.z, ct->x, ct->z, nav.walk);
                if (dd <= ws.range * 0.9f && los) {
                    if (pl.moving) { pl.moving = false; pl.path.clear(); haveDest = false; }
                    vb::AttackResult ar = world.tryAttack(1, attackOrder);
                    if (ar == vb::AttackResult::Fired) { pl.yaw = world.player()->yaw; attackOrder = 0; attackHand = -1; }
                    else if (ar == vb::AttackResult::NotYourTurn) { note("It is not your turn."); attackOrder = 0; attackHand = -1; }
                    else if (ar == vb::AttackResult::NoAp) { note("Not enough action points."); attackOrder = 0; attackHand = -1; }
                    else if (ar == vb::AttackResult::NoAmmo || ar == vb::AttackResult::Invalid) { attackOrder = 0; attackHand = -1; }
                } else if (!pl.moving) {
                    std::vector<std::pair<float, float>> cp;
                    if (nav.valid() ? nav.findPathWithin(pl.pos.x, pl.pos.z, ct->x, ct->z, std::max(1.2f, ws.range * 0.7f), cp) : (cp.push_back({ct->x, ct->z}), true)) { if (!cp.empty()) { pl.path = cp; pl.next = 0; pl.moving = true; destX = cp.back().first; destZ = cp.back().second; haveDest = true; destY = pl.pos.y; } }
                    else { note("You can't reach that."); attackOrder = 0; attackHand = -1; }
                }
            }
        }
        if (outOfAp) { outOfAp = false; attackOrder = 0; attackHand = -1; pendingUse = 0; note("Not enough action points."); }
        if (world.playerTurn() && world.playerAp() < 1.f && !pl.moving && world.player()->attackT <= 0.f && !attackOrder) world.endTurn();      // nothing left to spend: the turn passes
        if (!world.inCombat()) { /* combat over: nothing armed carries on */ }
        static int ticks = 0; ticks++;
        if (const char* dc = std::getenv("VB_DEBUG_CELLS")) if (ticks == 3 && nav.valid()) { float cx0 = 0, cz0 = 0; std::sscanf(dc, "%f,%f", &cx0, &cz0); int ci, cj; nav.cellOf(cx0, cz0, ci, cj);       // ascii map of the cells around a point: # blocks light, + blocked prop (no light block), . free
            for (int j = cj + 16; j >= cj - 16; j--) { for (int i = ci - 24; i <= ci + 24; i++) { size_t k = (size_t)j * nav.w + i; char ch = '.'; if (i >= 0 && j >= 0 && i < nav.w && j < nav.h) { if (nav.blocksLight(k)) ch = nav.raw[k] == 47 ? 'W' : 'D'; else if (!nav.walk[k]) ch = nav.raw[k] == 47 ? 'o' : '+'; } std::fputc(ch, stderr); } std::fputc('\n', stderr); } }
        if (std::getenv("VB_LIST_ENTS") && ticks == 3) for (size_t id = 2; id <= world.count(); id++) { vb::Entity* e = world.find((int)id); if (e) std::fprintf(stderr, "ent %zu %s file=%s kind=%d at %.1f %.1f %.1f\n", id, e->instance.c_str(), e->file.c_str(), (int)e->kind, e->x, e->y, e->z); }
        if (std::getenv("VB_LIST_SKILLS") && ticks == 3) for (size_t id = 2; id <= world.count(); id++) { auto so = world.usableSkills((int)id); if (so.empty()) continue; vb::Entity* e = world.find((int)id); std::fprintf(stderr, "skills %s (%s) locked=%d at %.1f %.1f:", e->instance.c_str(), e->scriptName.c_str(), e->locked ? 1 : 0, e->x, e->z); for (auto& x : so) std::fprintf(stderr, " [%d] %s;", x.id, x.label.c_str()); std::fprintf(stderr, "\n"); }
        if (!useName.empty() && ticks >= 3) { for (size_t id = 2; id <= world.count(); id++) { vb::Entity* e = world.find((int)id); if (e && e->instance == useName) { note(world.use((int)id)); if (std::getenv("VB_DEBUG_ENTS")) { std::vector<std::pair<float, float>> pa; float fx = std::sin(e->yaw), fz = std::cos(e->yaw); bool r1 = nav.findPath(e->x - fx * 5, e->z - fz * 5, e->x + fx * 5, e->z + fz * 5, pa); std::fprintf(stderr, "crossing %s after use (open=%d): found=%d points=%zu\n", useName.c_str(), (int)e->open, (int)r1, pa.size()); }
                if (std::getenv("VB_DEBUG_ENTS")) for (auto& en : L.ents) if (en.mapIdx == e->mapIdx) std::fprintf(stderr, "use %s: skel=%p openAnim=%p dur=%.2f models=%zu skinned=%d\n", useName.c_str(), (const void*)en.skel, (const void*)en.open, en.open ? en.open->duration : 0.f, en.models.size(), en.models.empty() ? -1 : (int)en.models[0]->skinned); break; } } useName.clear(); }
        if (!talkDone) { for (size_t id = 2; id <= world.count(); id++) { vb::Entity* e = world.find((int)id); if (e && (e->instance == talkTo || e->display == talkTo)) { world.startDialogue((int)id); break; } } talkDone = true; }
        if (useT > 0.f) {                                          // playing the use animation: standing still until it is over
            pl.moving = false; pl.path.clear(); haveDest = false; useT += dt;
            if (!useFired && useT >= useCue) { useFired = true; if (useAct) useAct(); }
            if (useT >= useDur) useT = 0.f;
        }
        if (pendingUse) {                                         // walking up to something the player clicked
            vb::Entity* t = world.find(pendingUse);
            float reach = reachOf(t);
            if (!t || t->destroyed) pendingUse = 0;
            else if (std::hypot(t->x - pl.pos.x, t->z - pl.pos.z) < reach) {
                pl.moving = false; pl.path.clear(); haveDest = false;
                pl.yaw = std::atan2(t->x - pl.pos.x, t->z - pl.pos.z);
                int tid = pendingUse; bool isSk = pendingIsSkill; vb::SkillOption sk = pendingSkill;
                useAct = [&, tid, isSk, sk]() { note(isSk ? world.useSkill(tid, sk) : world.use(tid)); };
                bool person = t->kind == vb::EntKind::Critter && !t->dead && !isSk;                 // talking to someone is not "using" them
                const vb::GrAnimation* ua = person ? nullptr : t->kind == vb::EntKind::Item ? (world.inCombat() && pl.pickCom ? pl.pickCom : pl.pickDef) : pl.useA;
                if (ua && ua->duration > 0.f) {
                    useAnimP = ua; useDur = ua->duration; if (std::getenv("VB_DEBUG_SKILL")) std::fprintf(stderr, "use animation %.2f s, action at %.2f s\n", ua->duration, t->kind == vb::EntKind::Item ? pickupCue : reachCue(ua)); useT = 1e-4f; useFired = false;
                    useCue = t->kind == vb::EntKind::Item ? std::min(useDur, pickupCue) : reachCue(ua);            // the pickup's cue "PU" is in its .ini; the use animation has none: the action comes when the right hand reaches farthest from the body
                } else useAct();
                pendingUse = 0; pendingIsSkill = false;
            } else if (!pl.moving) { if (!world.dialogue().active) note("You can't reach that."); pendingUse = 0; }
        }
        if (world.hasMapChange) {
            world.hasMapChange = false;
            std::string target = world.mapChange.map; int entry = world.mapChange.entry;
            int found = -1;
            for (size_t i = 0; i < maps.size() && found < 0; i++) if (maps[i] == target) found = (int)i;
            if (found < 0 && target.size() > 3) { std::string t2 = target.substr(3); for (size_t i = 0; i < maps.size() && found < 0; i++) if (maps[i] == t2) found = (int)i; }   // "00_04_Tutorial_Vault" -> "04_Tutorial_Vault"
            if (found >= 0) openMap(found, entry); else note("The way to '" + target + "' is not available.");
        }
        if (std::getenv("VB_HANDS") && ticks == 2) { std::string hs = std::getenv("VB_HANDS"); size_t pos = 0; int slot = vb::SlotHand1; while (pos <= hs.size() && slot <= vb::SlotHand1 + 1) { size_t c = hs.find(',', pos); std::string nm = hs.substr(pos, c == std::string::npos ? std::string::npos : c - pos); for (size_t bi = 0; bi < world.bag.size(); bi++) if (world.bag[bi].file == nm) { world.equip((int)bi, slot); break; } slot++; if (c == std::string::npos) break; pos = c + 1; } if (argSelect >= 0) iw.selectEquip(argSelect); }      // (test hook)
        if (world.openContainer && argTakeAll) { world.takeAll(world.openContainer); world.openContainer = 0; if (argEquipAll) for (int tries = 0; tries < 8; tries++) for (size_t bi = 0; bi < world.bag.size(); bi++) { int sl = world.defaultSlot(world.bag[bi].file); if (sl >= 0 && world.equipped[sl].empty() && world.equip((int)bi, sl)) break; } if (argSelect >= 0) iw.selectEquip(argSelect); }
        if (world.openContainer) { iw.containerId = world.openContainer; world.openContainer = 0; if (iw.containerId && world.cargoOf(iw.containerId).empty()) { note("It is empty."); iw.containerId = 0; } }
        for (const std::string& l : world.log) note(l);
        world.log.clear();

        float P[16], Vm[16], VP[16];
        perspective(P, cam.fov, (float)dw / dh, std::getenv("VB_NEAR") ? (float)std::atof(std::getenv("VB_NEAR")) : 150.f, 900.f)       /* tight depth range: the camera is a fixed 350 away, and thin decals (back number, stubble) z-fight with a wide one */; lookAt(Vm, cam.eye(), cam.forward());
        for (MapEmit& em : mapEmits) {       // map emitters: groups a script switches (the vault's vents after life support is restored) follow it, the others run from the start
            auto it = world.mapEffects.find(em.group); bool want = it != world.mapEffects.end() ? it->second : em.group != "Life_SupportEffects";
            if (want && !em.gid) { VfxRuntime::Anchor a; std::memcpy(a.M, em.M, 64); em.gid = vfx.start(em.file, a, nullptr, true); if (em.file.rfind("FX_AirVentMist", 0) == 0) vfx.tune(em.gid, 0.5f, 0.7f); }       // the mist: slower and dimmer than its file (user, 2026-10-25)
            else if (!want && em.gid) { vfx.stop(em.gid); em.gid = 0; }
        }
        {   // effects scripts attached to entities (AttachVEG / RemoveVEG): start new ones where the entity stands, stop removed ones
            for (const vb::Veg& g : world.vegs) if (!vegGroup.count(g.id)) if (vb::Entity* ve = world.find(g.ent)) { vegGroup[g.id] = vfx.start(g.name, vegAnchor(ve)); vfx.useSockets(vegGroup[g.id]);
                if (g.name.rfind("FX_State_FloatingEyeBot_Broken", 0) == 0) vfx.tuneLook(vegGroup[g.id], 0.55f, {1.f, 0.85f, 0.25f}, {0.55f, 0.75f, 1.f}, 0.55f); /* the broken eye bot: bluer lightning, smaller yellower sparks (user, 2026-10-25) */ if (std::getenv("VB_LIST_FX")) std::fprintf(stderr, "attached effect %s on %s: group %d\n", g.name.c_str(), ve->instance.c_str(), vegGroup[g.id]); }
            for (const vb::Veg& g : world.vegs) { auto vg = vegGroup.find(g.id); if (vg != vegGroup.end() && vg->second) if (vb::Entity* ve = world.find(g.ent)) vfx.setAnchor(vg->second, vegAnchor(ve)); }
            for (auto it = vegGroup.begin(); it != vegGroup.end();) { bool alive = false; for (const vb::Veg& g : world.vegs) if (g.id == it->first) alive = true; if (!alive) { vfx.stop(it->second); it = vegGroup.erase(it); } else ++it; }
        }
        { V3 f = cam.forward(); float fl = std::sqrt(f.x * f.x + f.z * f.z); if (fl > 1e-4f) { f.x /= fl; f.z /= fl; } audio.listener(pl.pos.x, pl.pos.y + 3.f, pl.pos.z, f.x, 0.f, f.z, 0.f, 1.f, 0.f); audio.update(dt); }
        profMark(11); std::memcpy(Vm_, Vm, 64); vfx.update(dt, cam.forward()); profMark(2);
        if (std::getenv("VB_DEBUG_FX") && (frame % 30) == 0) std::fprintf(stderr, "fx: %zu groups (%zu live), %zu particles\n", vfx.groups(), vfx.liveGroups(), vfx.particles()); if (std::getenv("VB_DEBUG_FX") && vfx.particles() > 300) vfx.dumpBig();
        if (menuMode) { perspective(P, 68.f, (float)dw / dh, 1.f, 10000.f); lookAt(Vm, {0.f, 5.5f, 0.f}, {0.f, 0.f, 1.f}); }       // [Mainmenu] target (0, 5.5, 0), azimuth 0, elevation 0, distance 0, fov 68
        mat4mul(VP, P, Vm); std::memcpy(lastVP, VP, 64); lastW = dw; lastH = dh;

        {   // mouse position (window -> drawable pixels)
            if (haveMouseArg) { mouseX = argMouseX; mouseY = argMouseY; mouseIn = true; }
            else { int mx, my; SDL_GetMouseState(&mx, &my); mouseX = mx * (float)dw / ww; mouseY = my * (float)dh / wh; mouseIn = (SDL_GetWindowFlags(win) & SDL_WINDOW_MOUSE_FOCUS) != 0; }
        }
        int hoverId = 0;       // the person / object under the mouse: it gets the pulsing glow, the ground ring and the name label
        if (mouseIn && attackOrder == 0) {
            bool overUi2 = world.dialogue().active || journalOpen || panel > 1 || (panel == 1 && iw.invOver(mouseX, mouseY, dw, dh)) || iw.containerId || bar.over(mouseX, mouseY, dw, dh); for (auto& r : uiRects) if (mouseX >= r[0] && mouseX < r[2] && mouseY >= r[1] && mouseY < r[3]) overUi2 = true;
            V3 ho, hd; if (!overUi2 && !menuMode && !world.playerScripted() && pickRay(lastVP, mouseX, mouseY, lastW, lastH, ho, hd)) hoverId = pickEntity(ho, hd);
        }
        const float hoverPulse = 0.5f + 0.5f * std::sin((float)SDL_GetTicks() * 0.006f);
        bool dying = world.playerDead(); if (dying) deathFx.bind(dw, dh);       // the scene goes through the gray / blur pass while dead
        glViewport(0, 0, dw, dh);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        {   // entering a building fades its roof / upper floor out (the map's building triggers, see level.h)
            int inside = L.bldInside(pl.pos.x, pl.pos.y, pl.pos.z); { static const char* fb = std::getenv("VB_FORCEBLD"); if (fb) inside = std::atoi(fb); }   // VB_FORCEBLD=n: debug, pretend the player is inside building n
            for (int bn = 1; bn < 8; bn++) { float goal = bn == inside ? 0.f : 1.f; float& f = L.bldFade[bn]; f += std::max(-dt / 0.45f, std::min(dt / 0.45f, goal - f)); }
        }
        profMark(11); R.fog.on = fogMode != 0 && !menuMode; R.fog.cx = pl.pos.x; R.fog.cz = pl.pos.z;
        R.fog.floorBright = fogMode == 1 ? 0.4f : 0.f; R.fog.hideActors = true;
        if (nav.valid()) {                                                    // the light's field of visibility: recomputed when the player changes cell or a door opens / closes
            unsigned sig = 0; for (size_t id = 2; id <= world.count(); id++) { vb::Entity* de = world.find((int)id); if (de && de->kind == vb::EntKind::Door && de->open) sig = sig * 31u + (unsigned)id; }
            { auto t0 = std::chrono::steady_clock::now(); bool ch = lightField.update(pl.pos.x, pl.pos.z, R.fog.r1, sig, [&](int idx) { return nav.blocksLight((size_t)idx); }); lightField.upload();
              if (ch && std::getenv("VB_DEBUG_LIGHT")) std::fprintf(stderr, "light field update %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
            R.fog.litTex = fogMode != 0 ? lightField.tex : 0; R.fog.lox = lightField.ox; R.fog.loz = lightField.oz; R.fog.lex = lightField.w * 0.5f; R.fog.lez = lightField.h * 0.5f;
        } else R.fog.litTex = 0;
        R.fog.cy = pl.pos.y + 4.f;
        fogMask.reveal(pl.pos.x, pl.pos.z, R.fog.r0, R.fog.r1, [&](float x, float z) { return litAt(x, z, pl.pos.y + 1.5f); }); fogMask.upload();      // exploration is remembered even while the fog is toggled off
        R.fog.maskTex = fogMode == 2 ? fogMask.tex : 0; R.fog.ox = fogMask.ox; R.fog.oz = fogMask.oz; R.fog.ex = fogMask.w * 0.5f; R.fog.ez = fogMask.h * 0.5f;
        LevelRenderer::Opts ro; ro.useTex = useTex; ro.useLm = useLm; ro.wire = wire;
        profMark(3); R.drawWorld(L, VP, ro); profMark(4);
        glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
        {   // placed entities: driven by the world (position, facing, walk/idle/death, door opening); everything else keeps its idle pose
            R.beginModels(VP);
            float nowS = (float)gameClock;                                          // idle animations stop while the game is paused
            if (const char* dbi = std::getenv("VB_DEBUG_ENTBONES")) { static bool once = false; if (!once) for (auto& en : L.ents) { vb::Entity* we = world.byMapIdx(en.mapIdx); if (we && we->instance == dbi && en.skel) { once = true; for (size_t bi = 0; bi < en.skel->bones.size(); bi++) std::fprintf(stderr, "bone %zu %s parent %d\n", bi, en.skel->bones[bi].name.c_str(), en.skel->bones[bi].parent); } } }
            for (auto& en : L.ents) {
                vb::Entity* we = world.byMapIdx(en.mapIdx);
                if (we && (!we->active || we->destroyed)) continue;
                const vb::GrAnimation* an = en.anim; float t = nowS + en.phase;
                if (we && en.skel && (we->kind == vb::EntKind::Critter)) {
                    LevelRenderer::modelMatrix(en.M, we->yaw, 0, 0, we->x, we->y, we->z);
                    int state = we->dead ? 3 : we->attackT > 0 ? 4 : we->hitT > 0 ? 5 : we->anim == "run" ? 2 : we->anim == "walk" ? 1 : we->anim == "laydown" && en.lay ? 6 : we->anim == "lying" && (en.lying || en.lay) ? 7 : we->anim == "getup" && en.getup ? 8 : we->anim == "twitch" && en.twitch ? 9 : we->anim == "maint" && en.maint ? 10 : 0;
                    if (state != en.animState) { en.animState = state; en.animT = 0; }
                    en.animT += dt; t = en.animT;
                    an = state == 3 ? (en.death ? en.death : en.anim) : state == 2 ? (en.run ? en.run : en.walk) : state == 1 ? en.walk : en.anim;
                    if (state == 4) an = we->attackKind == 1 ? (en.atkGun ? en.atkGun : en.atkUna) : we->attackKind == 2 ? (en.atkMelee ? en.atkMelee : en.atkUna) : en.atkUna;
                    else if (state == 5) an = en.hit;
                    else if (state == 6) an = en.lay; else if (state == 7) an = en.lying ? en.lying : en.lay; else if (state == 8) an = en.getup; else if (state == 9) an = en.twitch; else if (state == 10) an = en.maint;
                    if (!an) an = en.anim;
                    if (!we->dead && weaponClass(we->weapon) == 2) {            // two-handed weapons use the two-handed stance
                        if (state == 0 && (world.inCombat() ? en.idle2 : en.idle2d ? en.idle2d : en.idle2)) an = world.inCombat() ? en.idle2 : en.idle2d ? en.idle2d : en.idle2; else if ((state == 1 || state == 2) && en.walk2) an = en.walk2; else if (state == 4 && en.atk2) an = en.atk2; else if (state == 5 && en.hit2) an = en.hit2;
                    }
                    if ((state == 4 || state == 5 || state == 6 || state == 8 || state == 9 || state == 10) && an && an->duration > 0) t = std::min(t, an->duration - 0.001f);
                    if (state == 7 && an && an->duration > 0) t = (en.lying ? std::fmod(t, an->duration) : an->duration - 0.001f);
                    if (state == 3 && an && an->duration > 0) t = std::min(t, an->duration - 0.001f);
                    if (state == 0) t = nowS + en.phase;
                } else if (menuMode && we && en.skel && en.walk && we->instance.rfind("BoxcarIT_MainMenu", 0) == 0) {      // the menu's train rolls in with its Walk animation, then stands in its idle pose
                    static bool pd = std::getenv("VB_DEBUG_MENU") && std::fprintf(stderr, "menu train walk %.2f s\n", en.walk->duration); (void)pd; if (menuT < en.walk->duration) { an = en.walk; t = menuT; } else { an = en.anim; t = 0.f; }
                } else if (we && we->kind == vb::EntKind::Door && en.skel && en.open) {      // the door's own opening animation, at its real speed (the vault entrance takes 11.7 s)
                    float dur = en.open->duration; en.animT = std::max(0.f, std::min(dur, en.animT + (we->open ? dt : -dt))); an = en.open; t = std::min(en.animT, dur * 0.999f);
                }
                R.setActorFog(fogHides(we));
                R.drawActor(en.models, en.M, en.skel, ro.animate ? an : nullptr, t, useTex); actorBones = R.boneWorld;
                if (we && en.skel) { bool hasVeg = false; for (const vb::Veg& g : world.vegs) if (g.ent == we->id) hasVeg = true;
                    if (hasVeg) { HeldPose& vp = vegPose[we->id]; vp.valid = true; std::memcpy(vp.W2, en.M, 64); vp.bone.clear(); for (size_t bi = 0; bi < en.skel->bones.size() && (bi + 1) * 16 <= R.boneWorld.size(); bi++) { float T[16]; mat4mul(T, en.M, &R.boneWorld[bi * 16]); std::array<float, 16> arr; std::memcpy(arr.data(), T, 64); vp.bone[en.skel->bones[bi].name] = arr; } } }
                if (we && we->id == hoverId) {                 // the "highlight" model effect of FX_Selection_Critter_Medium_01: the model recoloured, colour pulsing between 46fffb and 00c1bc (sine, 1 Hz), unlit
                    GLint amb = glGetUniformLocation(R.modelProg, "ambient"), mul = glGetUniformLocation(R.modelProg, "fxMul");
                    float f = 0.5f + 0.5f * std::sin(nowS * 6.2831853f);
                    GLint lum = glGetUniformLocation(R.modelProg, "lumaOnly"); glDepthFunc(GL_LEQUAL); glUniform1f(amb, 1.f); glUniform1i(lum, 1);
                    glUniform4f(mul, (0x46 + (0x00 - 0x46) * f) / 255.f, (0xff + (0xc1 - 0xff) * f) / 255.f, (0xfb + (0xbc - 0xfb) * f) / 255.f * 1.0f, 1.f);
                    R.drawActor(en.models, en.M, en.skel, ro.animate ? an : nullptr, t, useTex);
                    glUniform1i(lum, 0); glUniform1f(amb, 0.45f); glUniform4f(mul, 1.f, 1.f, 1.f, 1.f); glDepthFunc(GL_LESS);
                }
                if (we && world.inCombat() && we->id == world.currentActor() && !we->dead && world.hostile(we->id) && entVisible(*we)) {      // the enemy whose turn it is: a thin red outline (the model again, pushed out along its normals, only its back faces, flat red)
                    GLint ol = glGetUniformLocation(R.modelProg, "outline"), fc = glGetUniformLocation(R.modelProg, "flatCol");
                    glUniform1f(ol, 0.05f); glUniform4f(fc, 0.9f, 0.05f, 0.05f, 1.f); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
                    R.drawActor(en.models, en.M, en.skel, ro.animate ? an : nullptr, t, useTex);
                    glDisable(GL_CULL_FACE); glUniform1f(ol, 0.f); glUniform4f(fc, 0.f, 0.f, 0.f, 0.f);
                }
                if (we && we->kind == vb::EntKind::Critter) {
                    drawHeld(we->dead ? std::string() : we->weapon, en.M, en.skel, we);
                    static std::map<const vb::GrSkeleton*, bool> isHuman; auto ih = isHuman.find(en.skel); if (ih == isHuman.end()) { bool h = false; if (en.skel) for (auto& b : en.skel->bones) if (b.name == "R Handle_1") h = true; ih = isHuman.emplace(en.skel, h).first; }
                    if (!we->dead && ih->second && (we->anim == "walk" || we->anim == "run") && we->attackT <= 0.f) footsteps(we, an, t, we->anim == "run", en.skel, en.M, actorBones);
                }
            }
        }
        profMark(5);
        {   // the player
            if (world.equipped[vb::SlotBody] != pmBody || world.equipped[vb::SlotHead] != pmHead) buildPlayerModel();
            float M[16]; LevelRenderer::modelMatrix(M, pl.yaw, 0, 0, pl.pos.x, pl.pos.y, pl.pos.z);
            R.beginModels(VP); R.setActorFog(true);
            vb::Entity* pw = world.player(); const vb::GrAnimation* pa = pl.moving && pl.walk ? pl.walk : pl.idle; float pt = pl.animT;
            if (pw->dead) { pa = pl.death ? pl.death : pl.idle; pt = deathT; if (pa && pa->duration > 0) pt = std::min(pt, pa->duration - 0.001f); }
            else if (pw->attackT > 0) { pa = pw->attackKind == 1 ? (pl.atkGun ? pl.atkGun : pl.atkUna) : pw->attackKind == 2 ? (pl.atkMelee ? pl.atkMelee : pl.atkUna) : pl.atkUna; if (!pa) pa = pl.idle; pt = std::max(0.f, 0.6f - pw->attackT); if (pa->duration > 0) pt = std::min(pt, pa->duration - 0.001f); }
            else if (pw->hitT > 0 && pl.hit) { pa = pl.hit; pt = std::max(0.f, 0.5f - pw->hitT); if (pa->duration > 0) pt = std::min(pt, pa->duration - 0.001f); }
            if (!pw->dead && weaponClass(world.playerWeapon()) == 2) {
                if (pw->attackT > 0 && pl.atk2) pa = pl.atk2; else if (pw->hitT > 0 && pl.hit2) pa = pl.hit2; else if (pl.moving && pl.walk2) pa = pl.walk2; else if (!world.inCombat() && pl.idle2d) pa = pl.idle2d; else if (pl.idle2) pa = pl.idle2;
                if (pa->duration > 0 && (pw->attackT > 0 || pw->hitT > 0)) pt = std::min(pt, pa->duration - 0.001f);
            }
            if (useT > 0.f && useAnimP && !pw->dead && pw->attackT <= 0) { pa = useAnimP; pt = std::min(useT, useAnimP->duration - 0.001f); }
            R.drawActor(pl.models, M, pl.skel, pa, pt, useTex); actorBones = R.boneWorld;
            drawHeld(pw->dead ? std::string() : world.playerWeapon(), M, pl.skel, pw);
            if (!pw->dead && pl.moving && pl.walk && pw->attackT <= 0.f) footsteps(pw, pl.walk, pl.animT, false, pl.skel, M, actorBones);
        }
        profMark(6);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        vfx.draw(VP, V3{Vm_[0], Vm_[4], Vm_[8]}, V3{Vm_[1], Vm_[5], Vm_[9]}, cam.forward());
        if (hoverId) if (vb::Entity* he = world.find(hoverId)) {      // the selection effect FX_Selection_Critter_Medium_01: two spinning discs on the ground (Disc_select_01 / 02)
            if (!selLoaded) {
                selLoaded = true;
                try { selFx = vb::loadVfx(assets + "/Effects/FX_Selection_Critter_Medium_01.vfx"); } catch (std::exception&) {}
                for (const vb::VfxEffect& e : selFx.effects) {
                    std::string nm = e.name(); if (nm != "select1" && nm != "select2") continue; SelDisc d; d.name = nm;
                    std::string mdl = e.res("Model"); size_t dot = mdl.rfind('.'); if (dot != std::string::npos) mdl = mdl.substr(0, dot); d.scale = e.f("ScaleX", 0.3f); d.col = e.rgb("Color"); d.alphaMin = (float)e.i("FinalAlpha", 100) / 255.f; d.alphaFreq = e.f("AlphaFreq", 1.f);
                    vb::ModelPart mp; mp.g3d_path = assets + "/Effects/" + mdl + ".g3d"; d.model = R.getModel(mp);
                    { vb::vfs::Stream f(mp.g3d_path); std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());     // the material's optional trailer: bit 4 = texture rotation (centre u, centre v, speed)
                      size_t k = b.find("SILENT"); if (k != std::string::npos && k + 10 <= b.size()) { uint32_t mask; std::memcpy(&mask, &b[k + 6], 4); size_t o = k + 10; static const int cnt[6] = {1, 1, 2, 2, 3, 4};
                        for (int bit = 0; bit < 6; bit++) if (mask >> bit & 1) { if (bit == 4 && o + 24 <= b.size()) { double v[3]; std::memcpy(v, &b[o], 24); d.cu = (float)v[0]; d.cv = (float)v[1]; d.spin = (float)v[2]; } o += 8 * cnt[bit]; } } }
                    if (d.model) selDiscs.push_back(d);
                }
                if (std::getenv("VB_DEBUG_FX")) for (auto& d : selDiscs) std::fprintf(stderr, "selection disc %s scale %.2f size %.2f x %.2f spin %.3f centre %.2f %.2f\n", d.name.c_str(), d.scale, d.model->size[0], d.model->size[2], d.spin, d.cu, d.cv);
            }
            float kind = he->kind == vb::EntKind::Critter ? 1.f : he->kind == vb::EntKind::Door ? 1.8f : he->kind == vb::EntKind::Container ? 1.3f : he->kind == vb::EntKind::Usable ? 1.5f : 0.7f, gy = he->y; groundHeight(L.tre, he->x, he->z, he->y, 3.f, gy);
            float tsec = (float)SDL_GetTicks() * 0.001f;
            R.beginModels(VP); R.setActorFog(false); GLint amb = glGetUniformLocation(R.modelProg, "ambient"), mul = glGetUniformLocation(R.modelProg, "fxMul"), uvr = glGetUniformLocation(R.modelProg, "uvRot");
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-2.f, -2.f); glUniform1f(amb, 1.f);      // depth-tested: the model hides the rings behind it
            for (const SelDisc& d : selDiscs) {
                float sc = d.scale * kind, M[16] = {sc, 0, 0, 0, 0, 1, 0, 0, 0, 0, sc, 0, he->x, gy + 0.35f, he->z, 1};
                float a = d.alphaMin + (1.f - d.alphaMin) * (0.5f + 0.5f * std::sin(tsec * 6.2831853f * d.alphaFreq));
                glUniform4f(mul, d.col[0], d.col[1], d.col[2], a); glUniform3f(uvr, d.cu, d.cv, d.spin * 6.2831853f * tsec);
                R.drawActor({d.model}, M, nullptr, nullptr, 0.f, true);
            }
            glUniform3f(uvr, 0.f, 0.f, 0.f); glUniform1f(amb, 0.45f); glUniform4f(mul, 1.f, 1.f, 1.f, 1.f); glDisable(GL_POLYGON_OFFSET_FILL); glDepthFunc(GL_LESS); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
        }
        profMark(7);
        if (haveDest && pl.moving && curTex[CurTarget].id) {      // destination marker: the yellow crosshair of the target cursor lying on the ground, depth tested (the player hides it where they stand in front), gone when they stop
            static GLuint mq = 0, mqVao = 0, mqVbo = 0;
            if (!mq) {
                mq = program("#version 330 core\nlayout(location=0) in vec3 p; layout(location=1) in vec2 uv; uniform mat4 VP; out vec2 v; void main(){ v = uv; gl_Position = VP * vec4(p, 1.0); }",
                             "#version 330 core\nin vec2 v; uniform sampler2D tex; out vec4 o; void main(){ o = texture(tex, v); if (o.a < 0.02) discard; }");
                glGenVertexArrays(1, &mqVao); glBindVertexArray(mqVao); glGenBuffers(1, &mqVbo); glBindBuffer(GL_ARRAY_BUFFER, mqVbo);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12); glEnableVertexAttribArray(1);
            }
            const float h = 1.6f, y = destY + 0.15f;                           // 3.2 units across
            float q[30] = {destX - h, y, destZ - h, 0, 0,  destX + h, y, destZ - h, 1, 0,  destX + h, y, destZ + h, 1, 1,   destX - h, y, destZ - h, 0, 0,  destX + h, y, destZ + h, 1, 1,  destX - h, y, destZ + h, 0, 1};
            glUseProgram(mq); glUniformMatrix4fv(glGetUniformLocation(mq, "VP"), 1, GL_FALSE, VP); glUniform1i(glGetUniformLocation(mq, "tex"), 0);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, curTex[CurTarget].id);
            glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-2.f, -2.f);
            glBindVertexArray(mqVao); glBindBuffer(GL_ARRAY_BUFFER, mqVbo); glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_POLYGON_OFFSET_FILL); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
        }
        if (colOverlay) R.drawCollisionOverlay(L, VP);
        if (markers) R.drawMarkers(L, VP);

        if (std::getenv("VB_DEBUG_ENTS")) { float sx, sy; bool ok = projectToScreen(VP, pl.pos.x, pl.pos.y + 3, pl.pos.z, dw, dh, sx, sy); std::fprintf(stderr, "player screen %d %.0f %.0f cam target %.1f %.1f %.1f\n", (int)ok, sx, sy, cam.target.x, cam.target.y, cam.target.z); }
        if (dying) deathFx.resolve(std::max(0.f, std::min(1.f, deathT / 3.f)));          // 3 second fade to gray and blur
        if (menuMode) {   // the main menu: its buttons over the 3D scene
            if (mbOpen) {     // Map Browser: a dark backdrop and the chosen map seen by a free camera, right of the list (no player, no sound, no fog of war)
                MbBox mb = mbBox(dw, dh); text.begin(dw, dh); text.rect(0, 0, (float)dw, (float)dh, 0.06f, 0.07f, 0.09f, 1.f); text.end();
                int vx = (int)mb.vx, vw = dw - vx;
                if (mvOk && vw > 40) {
                    if (shot.empty()) {       // the same keys as in play: WASD pan on the ground plane (Shift: twice as fast), Q / E rotate
                        const Uint8* k = SDL_GetKeyboardState(nullptr); V3 f{std::sin(mvGc.yaw), 0, std::cos(mvGc.yaw)}, r = norm(cross({0, 1, 0}, f));
                        float sp = GameCamera::kPanSpeed * (mvGc.fov / 8.f) * ((k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? 2.f : 1.f) * dt;       // (scaled with the zoom: the same screen speed)
                        if (k[SDL_SCANCODE_W]) mvGc.target = mvGc.target + f * sp; if (k[SDL_SCANCODE_S]) mvGc.target = mvGc.target - f * sp;
                        if (k[SDL_SCANCODE_D]) mvGc.target = mvGc.target + r * sp; if (k[SDL_SCANCODE_A]) mvGc.target = mvGc.target - r * sp;
                        mvGc.target.x = std::max(MV.cmin.x, std::min(MV.cmax.x, mvGc.target.x)); mvGc.target.z = std::max(MV.cmin.z, std::min(MV.cmax.z, mvGc.target.z));
                        { float gy; if (groundHeight(MV.tre, mvGc.target.x, mvGc.target.z, mvGc.target.y, 3.f, gy)) mvGc.target.y += (gy - mvGc.target.y) * std::min(1.f, dt * 8.f); }
                        mvGc.goalYaw += ((k[SDL_SCANCODE_Q] ? 1.f : 0.f) - (k[SDL_SCANCODE_E] ? 1.f : 0.f)) * GameCamera::kRotSpeed * dt; mvGc.yaw = mvGc.goalYaw;
                    }
                    float Pm[16], Vo[16], VPo[16];
                    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClear(GL_DEPTH_BUFFER_BIT); glViewport(vx, 0, vw, dh);
                    perspective(Pm, mvGc.fov, (float)vw / (float)dh, 40.f, 2000.f); lookAt(Vo, mvGc.eye(), mvGc.forward()); mat4mul(VPo, Pm, Vo);
                    R.fog.on = false; LevelRenderer::Opts ro; ro.showEnts = mvShowEnts; R.drawWorld(MV, VPo, ro); R.drawEntities(MV, VPo, ro, (float)SDL_GetTicks() * 0.001f);
                    glViewport(0, 0, dw, dh);
                }
            }
            if (objOpen) {    // Object Browser: a dark backdrop and the chosen model (orbit camera, normalised to a unit sphere), right of the list
                ObjBox ob = objBox(dw, dh); text.begin(dw, dh); text.rect(0, 0, (float)dw, (float)dh, 0.06f, 0.07f, 0.09f, 1.f); text.end();
                if (objSpin && !objDrag) objYaw += 25.f * dt;
                int vx = (int)ob.vx, vw = dw - vx;
                if (objModel && objModel->vao && vw > 40) {
                    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClear(GL_DEPTH_BUFFER_BIT); glViewport(vx, 0, vw, dh);
                    float yr = objYaw * kPi / 180.f, pr = objPitch * kPi / 180.f; V3 dir{std::sin(yr) * std::cos(pr), std::sin(pr), std::cos(yr) * std::cos(pr)};
                    float sc = 1.f / objModel->radius; float Pm[16], Vo[16], VPo[16], Mm[16];
                    perspective(Pm, 45.f, (float)vw / (float)dh, 0.05f, 100.f); lookAt(Vo, {dir.x * objDist, dir.y * objDist, dir.z * objDist}, {-dir.x, -dir.y, -dir.z}); mat4mul(VPo, Pm, Vo);
                    std::memset(Mm, 0, sizeof Mm); Mm[0] = Mm[5] = Mm[10] = sc; Mm[15] = 1.f; Mm[12] = -objModel->center[0] * sc; Mm[13] = -objModel->center[1] * sc; Mm[14] = -objModel->center[2] * sc;
                    R.beginModels(VPo); R.setActorFog(false); glUniform3f(glGetUniformLocation(R.modelProg, "light"), -dir.x, -dir.y, -dir.z);
                    const vb::GrAnimation* an = objSkel && objAnim >= 0 && objAnim < (int)objAnims.size() ? R.alib.animation(objAnims[(size_t)objAnim]) : nullptr; objAnimT += dt;
                    R.drawActor({objModel}, Mm, objSkel, an, objAnimT, true);
                    glViewport(0, 0, dw, dh);
                }
            }
            if (argMenuClick >= 0 && ++menuFrames == 4) menuPress = argMenuClick;
            if (const char* mp = std::getenv("VB_MAPPICK")) if (menuFrames == 6) for (size_t k = 0; k < mbMaps.size(); k++) if (maps[(size_t)mbMaps[k]] == mp) { mbSelect((int)k); mbVisible(dw, dh); }       // test hook: pick this map in the Map Browser
            if (std::getenv("VB_MAPPICK") && menuFrames >= 4 && menuFrames < 7) menuFrames++;
            if (objOpen && menuFrames >= 5) { static bool objHook = false; if (!objHook) { objHook = true;       // test hooks: VB_OBJFILTER=text, VB_OBJPICK=n (index in the filtered list), VB_OBJYAW / VB_OBJPITCH / VB_OBJDIST / VB_OBJANIM
                if (const char* f = std::getenv("VB_OBJFILTER")) { objFilter = f; objRefilter(-1); }
                if (const char* pk = std::getenv("VB_OBJPICK")) { objSelect(std::atoi(pk)); objVisible(dw, dh); }
                if (const char* v = std::getenv("VB_OBJYAW")) { objYaw = (float)std::atof(v); objSpin = false; } if (const char* v = std::getenv("VB_OBJPITCH")) objPitch = (float)std::atof(v); if (const char* v = std::getenv("VB_OBJDIST")) objDist = (float)std::atof(v);
                if (const char* v = std::getenv("VB_OBJANIM")) { objAnim = std::min(std::atoi(v), (int)objAnims.size() - 1); objAnimT = 0.f; objVisible(dw, dh); } } }
            float S, x0, y0; menuScale(dw, dh, S, x0, y0); gm::TexInfo bt = R.mtex.get("vb_menu_buttons", nullptr, true), lg = R.mtex.get("f3_logo", nullptr, true);
            ui.begin(dw, dh);
            int over = mouseIn ? menuAt(mouseX, mouseY, dw, dh) : -1;
            for (int i = 0; i < 4 && bt.id && !objOpen && !mbOpen; i++) {
                float bx = x0 + kMenuX * S, by = y0 + (kMenuY + kMenuRowY[i]) * S, br = i == menuDown ? 0.7f : i == over ? 1.25f : 1.f; if (i == menuDown) by += S;
                ui.image(bt.id, bx, by, kMenuBtnW * S, kMenuRowH * S, {br, br, br, 1.f}, 0.f, kMenuRowY[i] / (float)bt.h, 1.f, (kMenuRowY[i] + kMenuRowH) / (float)bt.h);
            }
            if (lg.id && !objOpen && !mbOpen) ui.image(lg.id, x0 + 828.f * S, y0 + 575.f * S, 176.f * S, 177.f * S);
            ui.end();
            if (objOpen) {
                ObjBox b = objBox(dw, dh); objClamp(dw, dh); const float sc = 0.9f; size_t maxc = (size_t)((b.w - 24.f) / text.width("M", sc)); int hov = mouseIn ? objAt(mouseX, mouseY, dw, dh) : -1, ahov = mouseIn ? objAnimAt(mouseX, mouseY, dw, dh) : -1;
                auto shorten = [&](const std::string& t) { return t.size() > maxc && maxc > 4 ? "..." + t.substr(t.size() - (maxc - 3)) : t; };
                text.begin(dw, dh); text.rect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, 0.55f, 0.45f, 0.15f, 0.92f); text.rect(b.x, b.y, b.w, b.h, 0.04f, 0.05f, 0.04f, 0.93f);
                text.draw(b.x + 14, b.y + 8, 1.1f, 1.f, 0.85f, 0.35f, "OBJECT BROWSER");
                text.draw(b.x + 14, b.y + 34, sc, 0.75f, 0.9f, 1.f, "Filter: " + objFilter + "_");
                for (int r = 0; r < b.mrows; r++) {
                    int k = objTop + r; if (k >= (int)objShown.size()) break; float ry = b.my + (float)r * b.lh; const std::string& lab = objLabels[(size_t)objShown[(size_t)k]];
                    if (k == objSel || k == hov) text.rect(b.x + 6, ry - 1, b.w - 12, b.lh, k == objSel ? 0.25f : 0.2f, k == objSel ? 0.4f : 0.3f, k == objSel ? 0.7f : 0.15f, 0.65f);
                    text.draw(b.x + 12, ry, sc, k == objSel ? 1.f : 0.7f, 1.f, k == objSel ? 1.f : 0.7f, shorten(lab));
                }
                if (objShown.empty()) text.draw(b.x + 12, b.my, sc, 1.f, 0.5f, 0.5f, "no models match");
                text.rect(b.x + 6, b.ayt - 5.f, b.w - 12, 1.f, 0.55f, 0.45f, 0.15f, 0.8f);
                text.draw(b.x + 14, b.ayt, 1.f, 1.f, 0.85f, 0.35f, "ANIMATIONS");
                if (objAnims.empty()) text.draw(b.x + 12, b.ay, sc, 0.6f, 0.6f, 0.6f, objSel < 0 ? "" : objModel && objModel->skinned ? "no animations found" : "this model has no animations");
                for (int r = 0; r < b.arows && !objAnims.empty(); r++) {
                    int k = objAnimTop + r; if (k > (int)objAnims.size()) break; float ry = b.ay + (float)r * b.lh; bool cur = k == objAnim + 1;
                    if (cur || k == ahov) text.rect(b.x + 6, ry - 1, b.w - 12, b.lh, cur ? 0.25f : 0.2f, cur ? 0.4f : 0.3f, cur ? 0.7f : 0.15f, 0.65f);
                    text.draw(b.x + 12, ry, sc, cur ? 1.f : 0.85f, cur ? 1.f : 0.8f, cur ? 1.f : 1.f, shorten(k == 0 ? std::string("(rest pose)") : objAnims[(size_t)(k - 1)]));
                }
                char cnt[64]; std::snprintf(cnt, sizeof cnt, "%zu / %zu models", objShown.size(), objLabels.size()); text.draw(b.x + b.w - text.width(cnt, 0.8f) - 12, b.y + b.h - 18, 0.8f, 0.6f, 0.6f, 0.6f, cnt);
                float ix = b.vx + 16.f;
                if (objSel >= 0) {
                    text.draw(ix, 16, 1.1f, 1.f, 1.f, 1.f, objLabels[(size_t)objShown[(size_t)objSel]]);
                    if (!objModel) text.draw(ix, 44, 1.f, 1.f, 0.5f, 0.5f, "this model cannot be loaded");
                    else {
                        char ib[160]; std::snprintf(ib, sizeof ib, "%zu vertices  %zu triangles   size %.2f x %.2f x %.2f", objModel->mesh.vertices.size(), objModel->mesh.faces.size(), objModel->size[0], objModel->size[1], objModel->size[2]);
                        text.draw(ix, 44, 0.85f, 0.7f, 0.85f, 0.7f, ib);
                    }
                } else text.draw(ix, 16, 1.f, 0.7f, 0.7f, 0.7f, "pick a model from the list");
                text.draw(ix, (float)dh - 46.f, 0.8f, 0.8f, 0.8f, 0.8f, "type: filter   Up/Down: choose model   Left / Right: animation"); text.draw(ix, (float)dh - 26.f, 0.8f, 0.8f, 0.8f, 0.8f, "drag: orbit   wheel: zoom   Space: spin   Esc: main menu");
                text.end();
            }
            if (mbOpen) {
                MbBox b = mbBox(dw, dh); mbClamp(dw, dh); const float sc = 0.9f; size_t maxc = (size_t)((b.w - 24.f) / text.width("M", sc)); int hov = mouseIn ? mbAt(mouseX, mouseY, dw, dh) : -1;
                text.begin(dw, dh); text.rect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, 0.55f, 0.45f, 0.15f, 0.92f); text.rect(b.x, b.y, b.w, b.h, 0.04f, 0.05f, 0.04f, 0.93f);
                text.draw(b.x + 14, b.y + 8, 1.1f, 1.f, 0.85f, 0.35f, "MAP BROWSER");
                for (int r = 0; r < b.rows; r++) {
                    int k = mbTop + r; if (k >= (int)mbMaps.size()) break; float ry = b.my + (float)r * b.lh; std::string lab = maps[(size_t)mbMaps[(size_t)k]];
                    if (k == mbSel || k == hov) text.rect(b.x + 6, ry - 1, b.w - 12, b.lh, k == mbSel ? 0.25f : 0.2f, k == mbSel ? 0.4f : 0.3f, k == mbSel ? 0.7f : 0.15f, 0.65f);
                    text.draw(b.x + 12, ry, sc, k == mbSel ? 1.f : 0.7f, 1.f, k == mbSel ? 1.f : 0.7f, lab.size() > maxc && maxc > 4 ? lab.substr(0, maxc - 3) + "..." : lab);
                }
                char cnt[64]; std::snprintf(cnt, sizeof cnt, "%zu maps", mbMaps.size()); text.draw(b.x + b.w - text.width(cnt, 0.8f) - 12, b.y + b.h - 18, 0.8f, 0.6f, 0.6f, 0.6f, cnt);
                { bool th = mouseIn && mbToggleAt(mouseX, mouseY, dw, dh); text.rect(b.x + 8, b.y + b.h - 26, b.w * 0.62f - 8, 22, th ? 0.2f : 0.12f, th ? 0.3f : 0.18f, 0.12f, 0.8f); text.draw(b.x + 14, b.y + b.h - 22, 0.85f, 0.9f, 1.f, 0.9f, std::string(mvShowEnts ? "[x]" : "[ ]") + " Objects (O)"); }
                float ix = b.vx + 16.f;
                if (mbWant >= 0) { std::string t = "Loading " + maps[(size_t)mbWant] + " ..."; text.draw(b.vx + ((float)dw - b.vx - text.width(t, 1.6f)) * 0.5f, (float)dh * 0.45f, 1.6f, 1.f, 1.f, 0.6f, t); mbWantDrawn = true; }
                else if (mvOk) { text.draw(ix, 16, 1.1f, 1.f, 1.f, 1.f, mvName); char ib[96]; std::snprintf(ib, sizeof ib, "%zu triangles   %zu entities   fov %.1f", MV.tris, MV.ents.size(), mvGc.fov); text.draw(ix, 44, 0.85f, 0.7f, 0.85f, 0.7f, ib); }
                else if (!mvErr.empty()) text.draw(ix, 16, 1.f, 1.f, 0.5f, 0.5f, mvName + ": " + mvErr);
                else text.draw(ix, 16, 1.f, 0.7f, 0.7f, 0.7f, "pick a map from the list");
                text.draw(ix, (float)dh - 46.f, 0.8f, 0.8f, 0.8f, 0.8f, "WASD: pan   Q / E: rotate   wheel: zoom   O: objects on / off   Up/Down: choose map"); text.draw(ix, (float)dh - 26.f, 0.8f, 0.8f, 0.8f, 0.8f, "left drag: turn / tilt   right drag: pan   Esc: main menu");
                text.end();
            }
        }
        if (bar.ok() && !menuMode) {   // the bottom bar (above the map, below the dialogue / journal windows)
            vb::Entity* pc = world.player(); BarState bs; bs.hp = pc->hp; bs.maxHp = pc->maxHp; bs.maxFatigue = 5 * (pc->special[0] + pc->special[2]); bs.fatigue = bs.maxFatigue;
            bs.log = &msgLog; bs.pressed = barPressed;
            for (int h = 0; h < 2; h++) {
                const std::string& wf = world.equipped[vb::SlotHand1 + h]; bool isW = !wf.empty() && world.itemInfo(wf).kind == "weapon";
                vb::WeaponStats ws = world.weaponStats(isW ? wf : std::string());
                if (isW) { vb::ItemInfo wi = world.itemInfo(wf); bs.weaponIcon[h] = wi.actIcon.empty() ? wi.icon : wi.actIcon; }
                if (!wf.empty() && !world.itemInfo(wf).skill.empty()) {                          // a kit: its action icon (lockpick / wrenches), "USE ON", no AP cost
                    vb::ItemInfo ki = world.itemInfo(wf); bs.weaponIcon[h] = ki.actIcon.empty() ? ki.icon : ki.actIcon; bs.weaponMode[h] = world.stringRef(378); bs.weaponApText[h] = "AP3"; continue;
                }
                bool empty = isW && !ws.melee && world.clipOf(vb::SlotHand1 + h) == 0;            // an empty gun's button becomes RELOAD
                bs.weaponMode[h] = world.stringRef(empty ? 389 : ws.fists ? 332 : ws.melee ? 350 : 330);
                bs.weaponApText[h] = "AP" + std::to_string(empty ? vb::World::kReloadAp : ws.ap);
            }
            bs.endTurnEnabled = world.playerTurn() && !world.playerDead();
            bs.armed = skillArm == 2 ? "btnUseSkill" : skillArm >= 0 ? (skillArm == 0 ? "btnWeapon01" : "btnWeapon02") : attackHand == 0 ? "btnWeapon01" : attackHand == 1 ? "btnWeapon02" : "";
            bs.apMax = world.maxApOf(*pc); bs.apFloat = world.inCombat() ? world.playerAp() : (float)bs.apMax; bs.ap = (int)std::floor(bs.apFloat + 1e-3f);
            ui.begin(dw, dh); bar.draw(ui, bs, dw, dh); if (panel == 1) iw.drawInv(ui, dw, dh, mouseX, mouseY); iw.drawContainer(ui, dw, dh, mouseX, mouseY); ui.end();
        }
        profMark(8);
        {   // world overlay: speech / damage text above heads (not fogged unless Z), transient messages, dialogue window, journal
            text.begin(dw, dh);
            for (const vb::FloatText& f : world.floats) {
                if (!speechInFog && fogMode != 0) {            // speech (and numbers) of anyone in the fog stay hidden, except in combat for hostiles and the player's side
                    vb::Entity* se = f.ent ? world.find(f.ent) : nullptr; float sx0 = se ? se->x : f.x, sz0 = se ? se->z : f.z;
                    bool combatSide = se && world.inCombat() && (se->team == world.player()->team || world.enemies(*se, *world.player()));
                    if (litAt(sx0, sz0, (se ? se->y : pl.pos.y) + 3.f) < kLitMin && !combatSide) continue;
                }
                float sx, sy; if (!projectToScreen(VP, f.x, f.y + f.dy + (f.speech ? 0.f : f.t * 2.5f), f.z, dw, dh, sx, sy)) continue;
                float a = std::min(1.f, std::max(0.f, (f.ttl - f.t) / 0.6f));
                float sc = f.speech ? 1.15f : 1.4f; float cw = text.width("M", sc);
                std::vector<std::string> lines = wrapText(f.text, f.speech ? 38 : 12);
                float lp = 19.f * sc, bh = (float)lines.size() * lp, y0 = sy - bh;
                float r = ((f.rgb >> 16) & 255) / 255.f, g = ((f.rgb >> 8) & 255) / 255.f, b = (f.rgb & 255) / 255.f;
                if (f.speech) { r += (1.f - r) * 0.7f; g += (1.f - g) * 0.7f; b += (1.f - b) * 0.7f; }   // the scripts ask for mid-grey (128): lighten it so it reads on any ground
                for (size_t i = 0; i < lines.size(); i++) {
                    float w = (float)lines[i].size() * cw, x0 = sx - w * 0.5f, yy = y0 + (float)i * lp;
                    if (f.speech) text.rect(x0 - 4, yy - 3, w + 8, lp + 3, 0, 0, 0, 0.6f * a);
                    text.draw(x0, yy, sc, r, g, b, lines[i], a);
                }
            }
            replyRects.clear(); uiRects.clear();
            if (world.dialogue().active) {
                const vb::Dialogue& dlg = world.dialogue();
                const float sc = 1.35f, cw = text.width("M", sc), lh = 20.f * sc;
                float pw = std::min((float)dw - 40.f, 1180.f), px0 = ((float)dw - pw) * 0.5f; size_t cols = (size_t)((pw - 40.f) / cw);
                std::vector<std::pair<std::string, int>> rows;           // text, reply index (-1 = NPC line, -2 = spacing)
                for (const std::string& t : dlg.npcText) { for (auto& l : wrapText(t, cols)) rows.push_back({l, -1}); rows.push_back({"", -2}); }
                for (size_t i = 0; i < dlg.replies.size(); i++) {
                    std::string t = std::to_string(i + 1) + ". " + (dlg.replies[i].text.empty() ? "[Continue]" : dlg.replies[i].text);
                    auto ls = wrapText(t, cols - 3); for (size_t k = 0; k < ls.size(); k++) rows.push_back({(k ? "   " : "") + ls[k], (int)i});
                }
                if (dlg.replies.empty()) rows.push_back({"1. [Goodbye]", 0});
                float ph = (float)rows.size() * lh + 36.f, py0 = (bar.ok() ? (float)dh - bar.height(dw, dh) : (float)dh) - ph - 14.f;
                uiRects.push_back({px0, py0, px0 + pw, py0 + ph});
                text.rect(px0 - 2, py0 - 2, pw + 4, ph + 4, 0.55f, 0.45f, 0.15f, 0.9f); text.rect(px0, py0, pw, ph, 0.04f, 0.05f, 0.04f, 0.88f);
                vb::Entity* npc = world.find(dlg.npc);
                std::string who = npc ? npc->display : ""; size_t sp = who.find(' ');            // dialogue scripts are named "00 Corporal Armstrong": drop the map-number prefix
                if (sp != std::string::npos && sp > 0 && sp <= 3 && std::all_of(who.begin(), who.begin() + (long)sp, [](unsigned char c) { return std::isdigit(c); })) who = who.substr(sp + 1);
                text.draw(px0 + 16, py0 + 6, 1.1f, 1.f, 0.85f, 0.35f, who);
                float y = py0 + 28.f;
                for (size_t i = 0; i < rows.size(); i++, y += lh) {
                    if (rows[i].second >= 0) { float r1 = y + lh; text.rect(px0 + 8, y - 2, pw - 16, lh, 0.15f, 0.2f, 0.12f, 0.6f); text.draw(px0 + 16, y, sc, 0.55f, 1.f, 0.55f, rows[i].first); if (i + 1 >= rows.size() || rows[i + 1].second != rows[i].second) replyRects.push_back({y - 2 - (float)0, r1, rows[i].second}); }
                    else text.draw(px0 + 16, y, sc, 0.95f, 0.95f, 0.85f, rows[i].first);
                }
                // reply hit areas: make each cover its own rows
                replyRects.clear(); { float yy = py0 + 28.f; int cur = -99; float start = 0; for (size_t i = 0; i <= rows.size(); i++, yy += lh) { int ri = i < rows.size() ? rows[i].second : -99; if (ri != cur) { if (cur >= 0) replyRects.push_back({start, yy, cur}); cur = ri; start = yy; } } }
            }
            if (journalOpen) {
                float pw = std::min((float)dw - 40.f, 900.f), px0 = ((float)dw - pw) * 0.5f; const float sc = 1.2f, cw = text.width("M", sc), lh = 20.f * sc; size_t cols = (size_t)((pw - 40.f) / cw);
                std::vector<std::pair<std::string, int>> rows;
                rows.push_back({"JOURNAL   (J to close)", 0}); rows.push_back({"", 2});
                if (world.journal.empty()) rows.push_back({"No tasks yet.", 2});
                for (const auto& t : world.journal) {
                    std::string title = world.stringRef(t.id); if (title.empty()) title = "Task " + std::to_string(t.id);
                    rows.push_back({std::string(t.state ? "[done] " : "[open] ") + title, 1});
                    for (int en : t.entries) { for (auto& l : wrapText(world.stringRef(en), cols - 2)) rows.push_back({"  " + l, 2}); }
                    rows.push_back({"", 2});
                }
                float ph = (float)rows.size() * lh + 24.f, py0 = ((float)dh - ph) * 0.4f;
                text.rect(px0 - 2, py0 - 2, pw + 4, ph + 4, 0.55f, 0.45f, 0.15f, 0.9f); text.rect(px0, py0, pw, ph, 0.04f, 0.05f, 0.04f, 0.9f);
                for (size_t i = 0; i < rows.size(); i++) { int k = rows[i].second; text.draw(px0 + 16, py0 + 12 + (float)i * lh, sc, k == 1 ? 1.f : 0.9f, k == 1 ? 0.85f : 0.9f, k == 1 ? 0.35f : 0.8f, rows[i].first); }
            }
            if (panel > 1) {      // character (2) / options (3); the inventory (1) is drawn by ItemWindows
                std::vector<std::string> rows; std::string title; vb::Entity* pc = world.player();
                if (panel == 2) {
                    title = "CHARACTER   (K to close)"; static const char* kS[7] = {"Strength", "Perception", "Endurance", "Charisma", "Intelligence", "Agility", "Luck"};
                    rows.push_back("Hit points  " + std::to_string(pc->hp) + " / " + std::to_string(pc->maxHp)); rows.push_back("");
                    for (int k = 0; k < 7; k++) { char b[64]; std::snprintf(b, sizeof b, "%-14s %d", kS[k], pc->special[k]); rows.push_back(b); }
                } else { title = "OPTIONS"; rows.push_back("Resume game"); rows.push_back("Quit to main menu"); rows.push_back("Quit game"); }
                const float sc = 1.3f, lh = 22.f * sc; float pw = 420.f, ph = (float)(rows.size() + 2) * lh + 20.f, px0 = ((float)dw - pw) * 0.5f, py0 = ((float)dh - ph) * 0.35f;
                uiRects.push_back({px0, py0, px0 + pw, py0 + ph});
                text.rect(px0 - 2, py0 - 2, pw + 4, ph + 4, 0.55f, 0.45f, 0.15f, 0.92f); text.rect(px0, py0, pw, ph, 0.04f, 0.05f, 0.04f, 0.93f);
                text.draw(px0 + 16, py0 + 10, sc, 1.f, 0.85f, 0.35f, title);
                if (panel == 3) replyRects.clear();
                for (size_t i = 0; i < rows.size(); i++) {
                    float ry = py0 + 10 + (float)(i + 2) * lh;
                    if (panel == 3) { bool hov = mouseY >= ry - 3 && mouseY < ry + lh - 3 && mouseX >= px0 && mouseX < px0 + pw; text.rect(px0 + 8, ry - 3, pw - 16, lh, hov ? 0.25f : 0.12f, hov ? 0.32f : 0.18f, 0.12f, 0.8f); replyRects.push_back({ry - 3, ry - 3 + lh, (int)i}); }
                    text.draw(px0 + 18, ry, sc, 0.7f, 1.f, 0.7f, rows[i]);
                }
            }
            text.end();
        }
        if (world.playerDead() && deathT > 1.f) {
            text.begin(dw, dh); float sc = 3.f; const char* m1 = "YOU ARE DEAD"; const char* m2 = "Press Enter to start over";
            text.rect(0, dh * 0.5f - 80, (float)dw, 150, 0, 0, 0, 0.6f);
            text.draw(dw * 0.5f - text.width(m1, sc) * 0.5f, dh * 0.5f - 60, sc, 0.9f, 0.2f, 0.2f, m1);
            text.draw(dw * 0.5f - text.width(m2, 1.4f) * 0.5f, dh * 0.5f + 20, 1.4f, 0.9f, 0.9f, 0.9f, m2);
            text.end();
        }
        if (hud || help) {
            text.begin(dw, dh);
            if (hud) {
                char buf[256];
                std::snprintf(buf, sizeof buf, "%s   %.0f fps   player %.1f %.1f %.1f   fov %.1f", maps[curMap].c_str(), fps, pl.pos.x, pl.pos.y, pl.pos.z, cam.fov);
                text.rect(0, 0, text.width(buf, 1.f) + 16, 26, 0, 0, 0, 0.55f); text.draw(8, 3, 1.f, 1, 1, 1, buf);
                std::string combatTxt = world.inCombat() && !world.playerDead() ? std::string("COMBAT round ") + std::to_string(world.combatRound()) + (world.playerTurn() ? ": YOUR TURN, " + std::to_string((int)std::floor(world.playerAp())) + " AP left (weapon buttons: attack / reload, Enter: end turn)   " : ": enemy turn   ") : std::string();
                float ly = 30.f; if (!combatTxt.empty()) { text.draw(8, ly, 1.f, 0.85f, 0.85f, 0.85f, combatTxt, 0.9f); ly += 22.f; }
                text.draw(8, ly, 1.f, 0.85f, 0.85f, 0.85f, "WASD pan   Q/E rotate   wheel zoom   click: walk / use / talk", 0.9f);
                text.draw(8, ly + 22.f, 1.f, 0.85f, 0.85f, 0.85f, "Space: center   F: fog of war   J: journal   F1: help   H: hide HUD", 0.9f);
            }
            if (help) {
                static const std::vector<std::string> help_ = {
                    "Controls", "",
                    "W A S D          pan the camera (Shift: faster)",
                    "Q / E            rotate the camera (hold)",
                    "Mouse wheel      zoom",
                    "Space            center the camera on the player",
                    "Left click       walk there; click a person, door, container or item to use it",
                    "Weapon buttons   combat: attack (then click a target) or reload",
                    "Kit / Use Skill  a kit in a hand (USE ON) or Use Skill, then click a target: pick the skill",
                    "Enter            end your turn in combat (or the End Turn button)",
                    "I  inventory    K  character    J  journal    Z  speech text in the fog on / off",
                    "1-9 or click     answer in a dialogue (Esc ends it)",
                    "Esc              close a window, or open the menu",
                    "Any open window pauses the game and freezes the camera", "",
                    "Debug:  C collision grid   M markers   T textures   L lightmaps   X wireframe",
                    "        F fog of war   [ and ] previous / next map",
                    "H  HUD on / off      F1  this help"};
                float tw = 0.f; for (const std::string& l : help_) tw = std::max(tw, text.width(l, 1.f));
                float bw = std::min(tw + 40.f, (float)dw - 20.f), bh = 22.f * (float)help_.size() + 20.f;
                text.rect(dw * 0.5f - bw * 0.5f, dh * 0.5f - bh * 0.5f, bw, bh, 0, 0, 0, 0.8f);
                for (size_t i = 0; i < help_.size(); i++) text.draw(dw * 0.5f - bw * 0.5f + 20, dh * 0.5f - bh * 0.5f + 10 + 22.f * (float)i, 1.f, i ? 0.9f : 1.f, 0.9f, i ? 0.9f : 0.4f, help_[i]);
            }
            if (hoverId) if (vb::Entity* he = world.find(hoverId)) if (he->kind != vb::EntKind::Door) {     // the name label next to the cursor (every object except doors)
                std::string nm = world.nameOf(*he); if (he->kind == vb::EntKind::Critter && he->dead) nm += " (dead)";
                if (!nm.empty()) { float sc = 1.f, w = text.width(nm, sc), x0 = std::min(mouseX + 24.f, (float)dw - w - 12.f), y0 = mouseY - 40.f;
                    text.rect(x0 - 6, y0 - 3, w + 12, 24, 0.f, 0.f, 0.f, 0.72f); text.draw(x0, y0, sc, 0.9f, 0.9f, 0.9f, nm); }
            }
            skRects.clear();
            if (skm.open) {                                                       // the small skill menu: the entries the target offers plus Cancel
                float sc = 1.1f, lh = 24.f, w = text.width(world.stringRef(158), sc); for (const auto& so : skm.opts) w = std::max(w, text.width(so.label, sc));
                w += 28; float h = lh * (float)(skm.opts.size() + 1) + 8, x0 = std::min(skm.x, dw - w - 4), y0 = std::min(skm.y, dh - h - 4);
                text.rect(x0 - 2, y0 - 2, w + 4, h + 4, 0.55f, 0.5f, 0.2f, 0.95f); text.rect(x0, y0, w, h, 0.05f, 0.05f, 0.05f, 0.92f);
                for (size_t i = 0; i <= skm.opts.size(); i++) {
                    float yy = y0 + 4 + (float)i * lh; bool over = mouseX >= x0 && mouseX < x0 + w && mouseY >= yy && mouseY < yy + lh;
                    if (over) text.rect(x0 + 2, yy, w - 4, lh, 0.3f, 0.28f, 0.1f, 0.9f);
                    text.draw(x0 + 14, yy + 2, sc, over ? 1.f : 0.85f, over ? 1.f : 0.8f, over ? 0.6f : 0.35f, i < skm.opts.size() ? skm.opts[i].label : world.stringRef(158));
                    skRects.push_back({x0, yy, x0 + w, yy + lh, (float)i});
                }
                uiRects.push_back({x0, y0, x0 + w, y0 + h});
            }
            text.end();
        }
        if (mouseIn) {   // the cursor: depends on what is under it
            int cur = CurDefault;
            bool overUi = world.dialogue().active || journalOpen || panel > 1 || (panel == 1 && iw.invOver(mouseX, mouseY, dw, dh)) || iw.containerId || bar.over(mouseX, mouseY, dw, dh); for (auto& r : uiRects) if (mouseX >= r[0] && mouseX < r[2] && mouseY >= r[1] && mouseY < r[3]) overUi = true;
            if (overUi) cur = CurGui;
            else if (skillArm >= 0) cur = CurTarget;                      // a kit / Use Skill is armed: the yellow crosshair
            else if (attackHand >= 0) cur = CurAttack;                    // a weapon button is armed: pick a target
            else if (world.playerScripted()) cur = CurWait;
            else { V3 o, d; if (pickRay(lastVP, mouseX, mouseY, lastW, lastH, o, d)) if (int h = pickEntity(o, d)) {
                vb::Entity* he = world.find(h);
                if (world.hostile(h)) cur = CurAttack;
                else if (he->kind == vb::EntKind::Critter) cur = he->dead ? CurUse : world.wantsDialogue(h) ? CurTalk : CurDefault;
                else if (he->kind == vb::EntKind::Item) cur = CurPickup;
                else if (he->kind == vb::EntKind::Door || he->kind == vb::EntKind::Container || he->kind == vb::EntKind::Usable) cur = CurUse;
            } }
            const gm::TexInfo& ti = curTex[cur];
            if (ti.id) {
                float w = (float)ti.w, h = (float)ti.h, x = std::round(mouseX - (kCurCentre[cur] ? w * 0.5f : 0.f)), y = std::round(mouseY - (kCurCentre[cur] ? h * 0.5f : 0.f));
                float v[24] = {x, y, 0, 0, x + w, y, 1, 0, x + w, y + h, 1, 1, x, y, 0, 0, x + w, y + h, 1, 1, x, y + h, 0, 1};
                glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUseProgram(curProg); glUniform2f(glGetUniformLocation(curProg, "screen"), (float)dw, (float)dh); glUniform1i(glGetUniformLocation(curProg, "tex"), 0);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ti.id); glBindVertexArray(curVao); glBindBuffer(GL_ARRAY_BUFFER, curVbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
            }
        }
        profMark(9);
        SDL_GL_SwapWindow(win); profMark(10);
        static int pf = 0; if (gProf) { double tot = 0; for (int i = 0; i < 12; i++) tot += gProfCur[i]; if (tot - gProfCur[10] > 12.0) { std::fprintf(stderr, "SPIKE frame %d: %.1f ms excl swap:", pf, tot - gProfCur[10]); for (int i = 0; i < 12; i++) if (i != 10 && gProfCur[i] > 3.0) std::fprintf(stderr, " %s=%.1f", gProfName[i], gProfCur[i]); std::fprintf(stderr, "\n"); } for (int i = 0; i < 12; i++) gProfCur[i] = 0; }
        if (gProf && ++pf % 120 == 0) { std::fprintf(stderr, "prof ms/frame:"); for (int i = 0; i < 12; i++) { std::fprintf(stderr, " %s=%.2f", gProfName[i], gProfAcc[i] / 120); gProfAcc[i] = 0; } std::fprintf(stderr, "\n"); }

        if (!shot.empty()) {                                    // scripted run: click once the first frame has set up the matrices, then simulate, then capture
            frame++;
            if (frame == 2 && !clickEnt.empty() && !haveClick) { for (size_t id = 2; id <= world.count(); id++) { vb::Entity* ce = world.find((int)id); if (ce && ce->instance == clickEnt) { float sx, sy; vb::Entity& r = *ce; if (projectToScreen(lastVP, r.x, r.y + 4.f, r.z, lastW, lastH, sx, sy)) { clickX = sx; clickY = sy; haveClick = true; } } } }
            if (frame == 4 && argPickSkill >= 0 && skm.open) chooseSkill(argPickSkill);       // (test hook: -pickskill N chooses that menu entry)
            if (frame == 2 && haveClick && !clickDone) { if (argArm == 2) skillArm = 2; else if (argArm >= 0) { world.selectHand(argArm); if (!world.equipped[vb::SlotHand1 + argArm].empty() && !world.itemInfo(world.equipped[vb::SlotHand1 + argArm]).skill.empty()) skillArm = argArm; else attackHand = argArm; } handleClick(clickX, clickY); clickDone = true; }
            if (frame >= frames && clickDone == haveClick) {
                if (simLeft > 0) { simLeft -= dt; }
                else {
                    std::vector<uint8_t> px((size_t)dw * dh * 3), flip((size_t)dw * dh * 3);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                    for (int y = 0; y < dh; y++) std::memcpy(&flip[(size_t)y * dw * 3], &px[(size_t)(dh - 1 - y) * dw * 3], (size_t)dw * 3);
                    SDL_Surface* s = SDL_CreateRGBSurfaceFrom(flip.data(), dw, dh, 24, dw * 3, 0x0000FF, 0x00FF00, 0xFF0000, 0);
                    if (s) { SDL_SaveBMP(s, shot.c_str()); SDL_FreeSurface(s); std::printf("screenshot: %s (player %.1f %.1f %.1f)\n", shot.c_str(), pl.pos.x, pl.pos.y, pl.pos.z); }
                    done = true;
                }
            }
        }
    }
    if (fpsCount) std::printf("average fps %.0f\n", fpsSum / fpsCount);
    L.destroy();
    audio.shutdown();
    SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

#pragma once
// Runtime for the game's effect files (Effects/*.vfx, see VFX_FORMAT_FINDINGS.md and src/assets/vfx.h): particle systems (with trails and force fields), models, sprites (billboards and
// flat decals), lightning, swing trails. Sounds, lights, weather and model effects are read but not played yet.
//
// An effect group is started with an anchor: the world matrix of whatever it is attached to. `Anchor::bone(name, M)` can resolve named bones live (a held weapon's FX_Muzzle / FX_Brass /
// Weapon_1) so the effect follows them. Units: lengths, velocities and accelerations in an effect file are in the skeleton's unit (x10 world units, like the bones); model scales are not.
#include "assets/vfs.h"
#include "assets/vfx.h"
#include "common/level.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>

class VfxRuntime {
public:
    static constexpr float kU = 10.f;                                    // effect-file length unit -> world units

    struct Anchor {
        float M[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // world matrix of the anchor (rotation + position)
        std::function<bool(const std::string&, float*)> bone;            // optional live lookup of a named bone's world matrix ("" = the anchor itself)
    };

    void init(LevelRenderer* renderer, gm::TexCache* tc, const std::string& assetsDir) {
        R_ = renderer; tc_ = tc; dir_ = assetsDir + "/Effects/";
        prog_ = program("#version 330 core\nlayout(location=0) in vec3 p; layout(location=1) in vec2 uv; layout(location=2) in vec4 c; uniform mat4 VP; out vec2 v; out vec4 col; void main(){ v = uv; col = c; gl_Position = VP * vec4(p, 1.0); }",
                        "#version 330 core\nin vec2 v; in vec4 col; uniform sampler2D tex; out vec4 o; void main(){ vec4 t = texture(tex, v); o = vec4(col.rgb * t.rgb, col.a * t.a); if (o.a < 0.003) discard; }");
        glGenVertexArrays(1, &vao_); glBindVertexArray(vao_); glGenBuffers(1, &vbo_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, (void*)12); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, (void*)20); glEnableVertexAttribArray(2);
        const int N = 64; std::vector<uint8_t> g((size_t)N * N * 4);                       // a soft glow, used for texture-less ribbons and sprites
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            float dx = ((float)x + 0.5f) / N * 2.f - 1.f, dy = ((float)y + 0.5f) / N * 2.f - 1.f, r = std::sqrt(dx * dx + dy * dy), a = std::max(0.f, 1.f - r); a = a * a * (3.f - 2.f * a);
            size_t i = ((size_t)y * N + x) * 4; g[i] = g[i + 1] = g[i + 2] = 255; g[i + 3] = (uint8_t)(a * 255.f);
        }
        glGenTextures(1, &glow_); glBindTexture(GL_TEXTURE_2D, glow_); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, g.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        flipV_ = std::getenv("VB_FXFLIPV") != nullptr;
    }

    std::function<bool(float, float, float&)> groundY;                  // ground height under (x, z)
    std::function<bool(float, float)> nearCamera;                       // false: too far from the view to bother (map emitters there are frozen and skipped)
    std::function<float(float, float)> fogAlpha;                         // 0..1: how much of the lit disc's soft edge lets a combat effect show (map emitters ignore it)
    // sound effects (EffectType SOUND): start(preset .psf name, wav name, looping, position) -> voice id; move / stop follow the group; null = silent
    std::function<int(const std::string&, const std::string&, bool, V3)> soundStart; std::function<void(int, V3)> soundMove; std::function<void(int)> soundStop;
    std::function<bool(float, float)> visibleAt;                        // false: the fog hides this place (the group is not drawn)

    // Starts the effect file `name` ("FX_FireSmall_01.veg" / ".vfx" / bare). Returns a group id, 0 if the file is unknown.
    int start(const std::string& name, const Anchor& self, const Anchor* target = nullptr, bool ambient = false) {
        const vb::VfxFile* f = file(name); if (!f) return 0;
        Group g; g.id = ++nextId_; g.f = f; g.self = self; g.ambient = ambient; if (target) { g.target = *target; g.hasTarget = true; }
        for (const vb::VfxEffect& e : f->effects) { Inst in; in.e = &e; in.triggered = e.b("TriggeredEffect"); in.trigT = -1.f; g.inst.push_back(std::move(in)); }
        groups_.push_back(std::move(g)); return nextId_;
    }
    void tune(int id, float timeScale, float alphaMul) { for (Group& g : groups_) if (g.id == id) { g.timeScale = timeScale; g.alphaMul = alphaMul; } }       // slower / dimmer playback of one group
    void setAnchor(int id, const Anchor& a) { for (Group& g : groups_) if (g.id == id) g.self = a; }       // an effect attached to something that moves
    // look adjustments of one group: particle size factor and colour tint, lightning inner colour and outer-glow opacity (the port's own tuning on top of the file values)
    void tuneLook(int id, float sizeMul, std::array<float, 3> tint, std::array<float, 3> boltInner, float boltOuterA) { for (Group& g : groups_) if (g.id == id) { g.sizeMul = sizeMul; g.tint = tint; g.boltInner = boltInner; g.boltOuterA = boltOuterA; g.boltLook = true; } }
    void useSockets(int id) { for (Group& g : groups_) if (g.id == id) g.sockets = true; }               // read "Socket = BONE_WEAPON_n" as the bone "Weapon_n" (effects attached to a creature, not to a held weapon)
    void stop(int id) { for (Group& g : groups_) if (g.id == id) g.stopped = true; }
    void clear() { if (soundStop) for (Group& g : groups_) for (Inst& in : g.inst) if (in.snd) soundStop(in.snd); groups_.clear(); }
    size_t groups() const { return groups_.size(); }
    size_t particles() const { size_t n = 0; for (const Group& g : groups_) for (const Inst& i : g.inst) n += i.ps.size(); return n; }
    void dumpBig() const { for (const Group& g : groups_) for (const Inst& i : g.inst) if (i.ps.size() > 40) std::fprintf(stderr, "  big: %s n=%zu life=%.1f..%.1f maxP=%d use=%d rate=%.1f..%.1f p0=(%.1f %.1f %.1f) v0=(%.1f %.1f %.1f)\n", g.f ? g.f->path.c_str() : "?", i.ps.size(), i.e->f("MinParticleLifetime", 1.f), i.e->f("MaxParticleLifetime", 1.f), i.e->i("MaxParticles", 50), (int)i.e->b("UseMaxParticles", true), i.e->f("MinAddRate", 10.f), i.e->f("MaxAddRate", 10.f), i.ps[0].p.x, i.ps[0].p.y, i.ps[0].p.z, i.ps[0].v.x, i.ps[0].v.y, i.ps[0].v.z); }
    size_t liveGroups() const { size_t n = 0; for (const Group& g : groups_) if (g.visible && !g.dead) n++; return n; }
    bool alive(int id) const { for (const Group& g : groups_) if (g.id == id && !g.stopped) return true; return false; }

    void update(float dt, V3 viewDir) {
        view_ = viewDir;
        for (Group& g : groups_) updateGroup(g, dt);
        groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [](const Group& g) { return g.dead; }), groups_.end());
    }

    // Draws everything. `right` / `up`: the camera's axes (billboards).
    void draw(const float* VP, V3 right, V3 up, V3 view) {
        view_ = view; right_ = right; up_ = up;
        for (auto& b : batches_) b.second.clear();
        modelDraws_.clear();
        for (Group& g : groups_) if (!g.dead && g.visible) for (Inst& in : g.inst) emitGeometry(g, in);
        glUseProgram(prog_); glUniformMatrix4fv(glGetUniformLocation(prog_, "VP"), 1, GL_FALSE, VP); glUniform1i(glGetUniformLocation(prog_, "tex"), 0);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glActiveTexture(GL_TEXTURE0); glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        for (int phase : {(int)Opaque, (int)RevSub, (int)Blend, (int)Add})                // decals and opaque first, then smoke / alpha, glowing additive effects (flames) last so smoke does not hide them
            for (auto& b : batches_) {
                if (b.second.empty() || b.first.second != phase) continue;
                setPhase(phase); glBindTexture(GL_TEXTURE_2D, b.first.first);
                glBufferData(GL_ARRAY_BUFFER, b.second.size() * 4, b.second.data(), GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(b.second.size() / 9));
            }
        glBlendEquation(GL_FUNC_ADD);
        // models (flash cones, blood cones ...)
        if (!modelDraws_.empty()) {
            R_->beginModels(VP); R_->setActorFog(false); GLint amb = glGetUniformLocation(R_->modelProg, "ambient"), mul = glGetUniformLocation(R_->modelProg, "fxMul");
            glEnable(GL_BLEND); glDepthMask(GL_FALSE);
            for (const ModelDraw& m : modelDraws_) {
                setPhase(m.phase); glUniform1f(amb, 1.f); glUniform4f(mul, m.r, m.g, m.b, m.a);
                R_->drawActor({m.model}, m.M, nullptr, nullptr, 0.f, true);
            }
            glUniform1f(amb, 0.45f); glUniform4f(mul, 1.f, 1.f, 1.f, 1.f); glBlendEquation(GL_FUNC_ADD);
        }
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    }

private:
    struct TrailPt { V3 p; float age; };
    struct Particle { V3 p, v; float age = 0, life = 1, size = 1, rot = 0, spin = 0, alpha = 255; bool collided = false; float trailAcc = 0; std::vector<TrailPt> trail; };
    struct RibbonPt { V3 a, b; float age; };
    struct Inst {
        const vb::VfxEffect* e = nullptr; bool triggered = false; float trigT = -1.f, acc = 0.f, boltT = 0.f; std::vector<Particle> ps; std::vector<RibbonPt> ribbon; V3 bolt[48]; int boltN = 0; float boltRegen = 0.f; int snd = 0; bool sndDone = false;
        V3 trigPos{0, 0, 0}; float trigM[16]; bool trigSet = false;
    };
    struct Group { int id = 0; const vb::VfxFile* f = nullptr; Anchor self, target; bool hasTarget = false; float t = 0.f; std::vector<Inst> inst; bool dead = false, stopped = false, visible = true, ambient = false; float timeScale = 1.f, alphaMul = 1.f; bool sockets = false; float sizeMul = 1.f; std::array<float, 3> tint{1.f, 1.f, 1.f}; bool boltLook = false; std::array<float, 3> boltInner{1.f, 1.f, 1.f}; float boltOuterA = -1.f; };
    struct ModelDraw { gm::GpuModel* model; float M[16]; int phase; float r, g, b, a; };

    LevelRenderer* R_ = nullptr; gm::TexCache* tc_ = nullptr; std::string dir_; GLuint prog_ = 0, vao_ = 0, vbo_ = 0, glow_ = 0; bool flipV_ = false;
    std::map<std::string, std::unique_ptr<vb::VfxFile>> files_; std::vector<Group> groups_; int nextId_ = 0; V3 view_{0, 0, 1}, right_{1, 0, 0}, up_{0, 1, 0};
    std::map<std::pair<GLuint, int>, std::vector<float>> batches_; std::vector<ModelDraw> modelDraws_;
    enum Phase { Add = 0, Blend = 1, Opaque = 2, RevSub = 3 };

    static float frand() { return (float)(std::rand() % 10000) / 10000.f; }
    static float lerp(float a, float b, float t) { return a + (b - a) * t; }
    static float rr(float a, float b) { return lerp(a, b, frand()); }
    static V3 xf(const float* M, V3 p) { return {M[0] * p.x + M[4] * p.y + M[8] * p.z + M[12], M[1] * p.x + M[5] * p.y + M[9] * p.z + M[13], M[2] * p.x + M[6] * p.y + M[10] * p.z + M[14]}; }
    static V3 rot(const float* M, V3 v) { return {M[0] * v.x + M[4] * v.y + M[8] * v.z, M[1] * v.x + M[5] * v.y + M[9] * v.z, M[2] * v.x + M[6] * v.y + M[10] * v.z}; }
    static int phaseOf(const std::string& s) { return s == "RP_BLEND_ADD" ? Add : s == "RP_OPAQUE" ? Opaque : s == "RP_BLEND_REVSUBTRACT" ? RevSub : Blend; }
    static void setPhase(int p) {
        glBlendEquation(p == RevSub ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        if (p == Add || p == RevSub) glBlendFunc(GL_SRC_ALPHA, GL_ONE); else if (p == Opaque) glBlendFunc(GL_ONE, GL_ZERO); else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    // waveform of a normalised time (guess: LINEAR = x, SINE = half a sine period per unit, SAW = fractional part, SQUARE = on / off)
    static float wave(const std::string& w, float x) {
        if (w == "VFXWAVE_SINE") return 0.5f - 0.5f * std::cos(x * 6.2831853f);
        if (w == "VFXWAVE_SAW") return x - std::floor(x);
        if (w == "VFXWAVE_SQUARE") return (x - std::floor(x)) < 0.5f ? 0.f : 1.f;
        return std::max(0.f, std::min(1.f, x));
    }

    const vb::VfxFile* file(const std::string& nameIn) {
        std::string n = nameIn; for (auto& c : n) c = (char)std::tolower((unsigned char)c);
        size_t dot = n.rfind('.'); if (dot != std::string::npos) n = n.substr(0, dot);
        auto it = files_.find(n); if (it != files_.end()) return it->second.get();
        std::unique_ptr<vb::VfxFile> f;
        for (const std::string& fn : vb::vfs::list(dir_)) {                      // case-insensitive lookup of the stem
            std::filesystem::path fp(fn); std::string s = fp.stem().string(); for (auto& c : s) c = (char)std::tolower((unsigned char)c);
            if (s == n && fp.extension() == ".vfx") { try { f = std::make_unique<vb::VfxFile>(vb::loadVfx(dir_ + fn)); } catch (std::exception& ex) { std::fprintf(stderr, "effect %s: %s\n", n.c_str(), ex.what()); } break; }
        }
        return (files_[n] = std::move(f)).get();
    }

    // the world matrix an effect is attached to: the anchor (or one of its bones), then the effect's Offset / Rotation
    static std::string snapOf(const Group& g, const vb::VfxEffect& e, const char* snapKey) {
        std::string s = e.s(snapKey); if (!s.empty() || !g.sockets) return s;
        std::string sock = e.s(std::string(snapKey) == "SnapBone" ? "Socket" : "EndPointSocket"); if (sock.rfind("BONE_WEAPON_", 0) == 0) return "Weapon_" + sock.substr(12);
        return s;
    }
    bool attach(const Group& g, const vb::VfxEffect& e, const char* tgtKey, const char* snapKey, const char* offKey, const char* rotKey, float* M) const {
        std::string tgt = e.s(tgtKey), snap = snapOf(g, e, snapKey); const Anchor& a = (tgt == "VFXTARGET_TARGET" && g.hasTarget) ? g.target : g.self;
        float B[16]; if (!(a.bone && a.bone(snap, B))) std::memcpy(B, a.M, 64);
        std::array<float, 3> off = e.v3(offKey), ro = e.v3(rotKey); float L[16], Rm[16];
        LevelRenderer::modelMatrix(Rm, ro[1] * 3.14159265f / 180.f, ro[0] * 3.14159265f / 180.f, ro[2] * 3.14159265f / 180.f, off[0] * kU, off[1] * kU, off[2] * kU);
        mat4mul(L, B, Rm); std::memcpy(M, L, 64); return true;
    }

    void trigger(Group& g, int idx, V3 pos) {
        if (idx < 0 || (size_t)idx >= g.inst.size()) return; Inst& in = g.inst[(size_t)idx]; if (!in.triggered) return;
        in.trigT = g.t; in.trigPos = pos; in.trigSet = true;
    }

    void updateGroup(Group& g, float dt) {
        dt *= g.timeScale;
        float M0[16]; if (!(g.self.bone && g.self.bone("", M0))) std::memcpy(M0, g.self.M, 64);
        if (g.ambient && nearCamera && !nearCamera(M0[12], M0[14])) { g.visible = false; return; }
        g.t += dt;
        g.visible = g.ambient || !visibleAt || visibleAt(M0[12], M0[14]);       // map emitters (fires, smoke) are drawn everywhere; combat effects only where the fog lets us see
        bool anyActive = false;
        for (size_t k = 0; k < g.inst.size(); k++) {
            Inst& in = g.inst[k]; const vb::VfxEffect& e = *in.e; float dur = e.f("Duration", 0.f), start = e.f("Delay", 0.f), now = g.t;
            if (in.triggered) { if (in.trigT < 0.f) continue; now = g.t - in.trigT; start = 0.f; }
            bool within = now >= start && (dur < 0.f || now < start + dur) && !g.stopped;
            if (within && dur < 0.f) anyActive = true; if (within && dur >= 0.f) anyActive = true;
            if (e.type == "particlesystem") updateParticles(g, in, k, now - start, within, dt);
            else if (e.type == "trail") updateTrail(g, in, within, dt);
            else if (e.type == "sound" && soundStart) {
                float SM[16]; attach(g, e, "Target", "SnapBone", "Offset", "Rotation", SM); V3 sp{SM[12], SM[13], SM[14]};
                if (within && !in.sndDone) { in.sndDone = true; std::string ps = e.res("PresetFile"), wv = e.res("WAVFile"); in.snd = soundStart(ps.empty() ? wv : ps, wv, e.b("Looping"), sp); }
                else if (in.snd && soundMove && e.b("Looping")) soundMove(in.snd, sp);
                if (!within && in.snd && e.b("Looping") && soundStop) { soundStop(in.snd); in.snd = 0; }
            }
            else if (e.type == "lightning" && within) { in.boltRegen -= dt; if (in.boltRegen <= 0.f) { in.boltRegen = 0.05f; regenBolt(g, in); } }
            if (e.type == "particlesystem" && !in.ps.empty()) anyActive = true;
            if (e.type == "trail" && !in.ribbon.empty()) anyActive = true;
            if (!within && now < start && !g.stopped) anyActive = true;                      // not started yet
        }
        if ((g.stopped || !anyActive) && soundStop) for (Inst& in : g.inst) if (in.snd && in.e->type == "sound" && in.e->b("Looping")) { soundStop(in.snd); in.snd = 0; }
        if (!anyActive || (g.stopped && std::all_of(g.inst.begin(), g.inst.end(), [](const Inst& i) { return i.ps.empty() && i.ribbon.empty(); }))) {
            if (g.f->loop && !g.stopped) { g.t = 0.f; for (Inst& in : g.inst) { in.ps.clear(); in.ribbon.clear(); in.acc = 0; } } else g.dead = true;
        }
    }

    void updateParticles(Group& g, Inst& in, size_t self, float local, bool within, float dt) {
        const vb::VfxEffect& e = *in.e; float M[16];
        attach(g, e, "Target", "SnapBone", "Offset", "Rotation", M);
        bool emitting = within && (!e.b("UseEmitterLifetime") || local < e.f("EmitterLifetime", 0.f));
        if (emitting && e.b("AutoAdd", true)) {
            in.acc += rr(e.f("MinAddRate", 10.f), e.f("MaxAddRate", 10.f)) * dt;
            while (in.acc >= 1.f) {
                in.acc -= 1.f; int n = e.i("MinParticlesPerAdd", 1) + (int)(frand() * (float)(e.i("MaxParticlesPerAdd", 1) - e.i("MinParticlesPerAdd", 1) + 0.999f));
                for (int c = 0; c < n; c++) { if (e.b("UseMaxParticles", true) && (int)in.ps.size() >= e.i("MaxParticles", 50)) break; spawn(g, in, M); }
            }
        }
        // forces from the group's collision fields
        std::array<float, 3> a1 = e.v3("Acceleration1"), a2 = e.v3("Acceleration2"); bool r1 = e.b("RotateAccel1WithTarget"), r2 = e.b("RotateAccel2WithTarget");
        V3 acc = (r1 ? rot(M, {a1[0], a1[1], a1[2]}) : V3{a1[0], a1[1], a1[2]}) * kU + (r2 ? rot(M, {a2[0], a2[1], a2[2]}) : V3{a2[0], a2[1], a2[2]}) * kU;
        fields_.clear(); for (const Inst& fi : g.inst) if (fi.e->type == "collision") { Field f; if (makeField(g, fi, f)) fields_.push_back(f); }
        const std::string trailType = e.s("ParticleTrailType"); float tint = e.f("TrailInterval", 0.02f), tlife = e.f("TrailLifetime", 0.1f);
        bool collide = e.b("PerformCollision"); int collEff = e.i("CollisionEffect", -1), deathEff = e.i("ParticleDeathEffect", -1);
        for (size_t i = 0; i < in.ps.size();) {
            Particle& p = in.ps[i]; p.age += dt;
            if (p.age >= p.life) { if (deathEff >= 0) trigger(g, deathEff, p.p); in.ps[i] = std::move(in.ps.back()); in.ps.pop_back(); continue; }
            V3 a = acc; for (const Field& fl : fields_) a = a + fieldForce(fl, p.p);
            p.v = p.v + a * dt; p.p = p.p + p.v * dt; p.rot += p.spin * dt;
            if (collide && groundY) { float gy; if (groundY(p.p.x, p.p.z, gy) && p.p.y < gy + 0.03f && p.v.y < 0.f) { p.p.y = gy + 0.03f; p.v.y = -p.v.y * 0.3f; p.v.x *= 0.5f; p.v.z *= 0.5f; if (!p.collided && collEff >= 0) trigger(g, collEff, p.p); p.collided = true; } }
            if (trailType == "VFXPARTICLETRAILTYPE_LINE") {
                p.trailAcc += dt; if (p.trailAcc >= tint || p.trail.empty()) { p.trailAcc = 0.f; p.trail.push_back({p.p, 0.f}); }
                for (auto& t : p.trail) t.age += dt; while (!p.trail.empty() && p.trail.front().age > tlife) p.trail.erase(p.trail.begin());
            }
            i++;
        }
        (void)self;
    }

    struct Field { V3 c; float R, power; };                                                  // a collision field (force field) of the group: attract (16) / repel (5) inside a sphere / box
    std::vector<Field> fields_;
    bool makeField(const Group& g, const Inst& fi, Field& f) const {
        const vb::VfxEffect& c = *fi.e; float start = c.f("Delay", 0.f), dur = c.f("Duration", -1.f); if (g.t < start || (dur >= 0.f && g.t > start + dur)) return false;
        float M[16]; attach(g, c, "Target", "SnapBone", "Offset", "Rotation", M);
        f.c = {M[12], M[13], M[14]}; f.R = (c.s("CollisionShape") == "VFXCOLLISIONSHAPE_BOX" ? std::max(c.v3("Dimensions")[0], c.v3("Dimensions")[2]) : c.f("Radius", 1.f)) * kU;
        f.power = c.f("Power", 1.f) * kU * (c.s("CollisionType") == "VFXCOLLISIONTYPE_REPEL" ? -1.f : 1.f); return f.R > 0.f;
    }
    static V3 fieldForce(const Field& f, V3 pos) {
        V3 d = f.c - pos; float r = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); if (r > f.R || r < 1e-3f) return {0, 0, 0};
        return d * (f.power * (1.f - r / f.R) / r);
    }

    void spawn(Group& g, Inst& in, const float* M) {
        const vb::VfxEffect& e = *in.e; Particle p; std::array<float, 3> mn = e.v3("MinDimensions"), mx = e.v3("MaxDimensions");
        V3 half{rr(mn[0], mx[0]) * kU, rr(mn[1], mx[1]) * kU, rr(mn[2], mx[2]) * kU}, loc{0, 0, 0};
        if (e.s("EmitterShape") == "VFXEMITTERSHAPE_SPHERE") { for (int t = 0; t < 8; t++) { V3 q{frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1}; if (q.x * q.x + q.y * q.y + q.z * q.z <= 1.f) { loc = {q.x * half.x, q.y * half.y, q.z * half.z}; break; } } }
        else loc = {(frand() * 2 - 1) * half.x, (frand() * 2 - 1) * half.y, (frand() * 2 - 1) * half.z};
        bool rotWith = e.b("RotateWithTarget", true);
        p.p = xf(M, loc);
        std::array<float, 3> v0 = e.v3("MinInitialVelocity"), v1 = e.v3("MaxInitialVelocity"); float sc = rr(e.f("MinInitialVelocityScale", 1.f), e.f("MaxInitialVelocityScale", 1.f)) * kU;
        V3 vl{rr(v0[0], v1[0]) * sc, rr(v0[1], v1[1]) * sc, rr(v0[2], v1[2]) * sc}; p.v = rotWith ? rot(M, vl) : vl;
        float ex = rr(e.f("MinExplosion", 0.f), e.f("MaxExplosion", 0.f)) * kU; if (ex != 0.f) { V3 d{frand() * 2 - 1, frand() * 2 - 1, frand() * 2 - 1}; float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) + 1e-4f; p.v = p.v + d * (ex / l); }
        p.life = std::max(0.02f, rr(e.f("MinParticleLifetime", 1.f), e.f("MaxParticleLifetime", 1.f))); p.size = rr(e.f("MinParticleSize", 0.1f), e.f("MaxParticleSize", 0.1f)) * kU;
        p.alpha = rr((float)e.i("MinParticleAlpha", 255), (float)e.i("MaxParticleAlpha", 255));
        if (e.b("RotatableParticles")) { p.rot = rr(e.v3("MinParticleRotation")[2], e.v3("MaxParticleRotation")[2]) * 3.14159265f / 180.f; p.spin = rr(e.v3("MinParticleSpin")[2], e.v3("MaxParticleSpin")[2]) * 3.14159265f / 180.f * 0.25f; }
        in.ps.push_back(std::move(p)); (void)g;
    }

    void updateTrail(Group& g, Inst& in, bool within, float dt) {                            // a swept ribbon between the effect point and its end point
        const vb::VfxEffect& e = *in.e; float life = e.f("SegmentLifetime", 0.2f);
        for (auto& r : in.ribbon) r.age += dt;
        while (!in.ribbon.empty() && in.ribbon.front().age > life) in.ribbon.erase(in.ribbon.begin());
        if (!within) return;
        float A[16], Bm[16]; attach(g, e, "Target", "SnapBone", "Offset", "Rotation", A);
        const Anchor& a = (e.s("EndPointTarget") == "VFXTARGET_TARGET" && g.hasTarget) ? g.target : g.self;
        if (!(a.bone && a.bone(snapOf(g, e, "EndPointSnapBone"), Bm))) std::memcpy(Bm, a.M, 64);
        std::array<float, 3> off = e.v3("EndPointOffset");
        in.ribbon.push_back({V3{A[12], A[13], A[14]}, xf(Bm, {off[0] * kU, off[1] * kU, off[2] * kU}), 0.f});
    }

    void regenBolt(Group& g, Inst& in) {
        const vb::VfxEffect& e = *in.e; float A[16]; attach(g, e, "Target", "SnapBone", "Offset", "Rotation", A);
        const Anchor& a = (e.s("EndPointTarget") == "VFXTARGET_TARGET" && g.hasTarget) ? g.target : g.self; float Bm[16]; std::string snap = snapOf(g, e, "EndPointSnapBone");
        if (!(a.bone && a.bone(snap, Bm))) std::memcpy(Bm, a.M, 64); std::array<float, 3> off = e.v3("EndPointOffset");
        V3 p0{A[12], A[13], A[14]}, p1 = xf(Bm, {off[0] * kU, off[1] * kU, off[2] * kU}); V3 d = p1 - p0; float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < 1e-3f) { p1 = p0 + V3{0, 2.f, 0}; d = p1 - p0; len = 2.f; }
        int n = std::max(3, std::min(46, (int)(len * rr(e.f("MinSegmentsPerFoot", 1.f), e.f("MaxSegmentsPerFoot", 1.f)) * 0.3f + rr(e.f("MinAdditionalSegments", 0.f), e.f("MaxAdditionalSegments", 0.f)) + 3)));
        V3 side = norm(cross(d, view_)); V3 up = norm(cross(d, side)); float jag = rr(e.f("MinJaggedness", 10.f), e.f("MaxJaggedness", 20.f)) * 0.01f * len * 0.4f;       // Jaggedness is a percentage (4 .. 100 in the files) of the bolt length
        in.boltN = n + 1;
        for (int i = 0; i <= n; i++) { float t = (float)i / (float)n; V3 p = p0 + d * t; if (i != 0 && i != n) p = p + side * ((frand() * 2 - 1) * jag) + up * ((frand() * 2 - 1) * jag * 0.5f); in.bolt[i] = p; }
    }

    // ---- geometry
    std::vector<float>& batch(GLuint tex, int phase) { return batches_[{tex, phase}]; }
    GLuint texOf(const std::string& file) { if (file.empty()) return glow_; gm::TexInfo t = tc_->get(file, nullptr, true); return t.id ? t.id : glow_; }
    // a flat decal whose corners each follow the ground height (on a slope a single-height quad dips into the terrain and is cut off in jagged pieces)
    void decal(std::vector<float>& out, V3 c, V3 ax, V3 ay, float r, float g, float b, float a, const float* uv) {
        const int N = 3; float v0 = flipV_ ? uv[3] : uv[1], v1 = flipV_ ? uv[1] : uv[3];
        auto pt = [&](float s, float t) { V3 p = c + ax * s + ay * t; float gy; if (groundY && groundY(p.x, p.z, gy)) p.y = gy + 0.25f; return p; };
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
            float s0 = -1.f + 2.f * i / N, s1 = -1.f + 2.f * (i + 1) / N, t0 = -1.f + 2.f * j / N, t1 = -1.f + 2.f * (j + 1) / N;
            auto tcOf = [&](float s, float t, float* o) { o[0] = uv[0] + (uv[2] - uv[0]) * (s + 1.f) * 0.5f; o[1] = v1 + (v0 - v1) * (t + 1.f) * 0.5f; };
            V3 p[4] = {pt(s0, t0), pt(s1, t0), pt(s1, t1), pt(s0, t1)}; float tc[4][2]; tcOf(s0, t0, tc[0]); tcOf(s1, t0, tc[1]); tcOf(s1, t1, tc[2]); tcOf(s0, t1, tc[3]); const int idx[6] = {0, 1, 2, 0, 2, 3};
            for (int k : idx) out.insert(out.end(), {p[k].x, p[k].y, p[k].z, tc[k][0], tc[k][1], r, g, b, a * fa(p[k].x, p[k].z)});
        }
    }
    void quad(std::vector<float>& out, V3 c, V3 ax, V3 ay, float r, float g, float b, float a, const float* uv) {
        float v0 = flipV_ ? uv[3] : uv[1], v1 = flipV_ ? uv[1] : uv[3];
        V3 p[4] = {c - ax - ay, c + ax - ay, c + ax + ay, c - ax + ay}; const float tc[4][2] = {{uv[0], v1}, {uv[2], v1}, {uv[2], v0}, {uv[0], v0}}; const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k : idx) out.insert(out.end(), {p[k].x, p[k].y, p[k].z, tc[k][0], tc[k][1], r, g, b, a});
    }
    void ribbon(std::vector<float>& out, const std::vector<V3>& pts, const std::vector<float>& width, const std::vector<std::array<float, 4>>& col) {
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            V3 d = pts[i + 1] - pts[i]; V3 s = cross(d, view_); float l = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z); if (l < 1e-6f) continue; s = s * (1.f / l);
            V3 a0 = pts[i] - s * (width[i] * 0.5f), a1 = pts[i] + s * (width[i] * 0.5f), b0 = pts[i + 1] - s * (width[i + 1] * 0.5f), b1 = pts[i + 1] + s * (width[i + 1] * 0.5f);
            const auto &c0 = col[i], &c1 = col[i + 1];
            auto vtx = [&](V3 p, float u, const std::array<float, 4>& c) { out.insert(out.end(), {p.x, p.y, p.z, u, 0.5f, c[0], c[1], c[2], c[3]}); };
            vtx(a0, 0, c0); vtx(a1, 1, c0); vtx(b1, 1, c1); vtx(a0, 0, c0); vtx(b1, 1, c1); vtx(b0, 0, c1);
        }
    }

    bool fogFade_ = false;
    float fa(float x, float z) const { return fogFade_ && fogAlpha ? fogAlpha(x, z) : 1.f; }
    void emitGeometry(Group& g, Inst& in) {
        fogFade_ = !g.ambient;
        const vb::VfxEffect& e = *in.e; const std::string& ty = e.type; float dur = e.f("Duration", 0.f), start = e.f("Delay", 0.f), now = g.t;
        if (in.triggered) { if (in.trigT < 0.f) return; now = g.t - in.trigT; start = 0.f; }
        bool within = now >= start && (dur < 0.f || now < start + dur) && !g.stopped; float u = dur > 0.f ? std::max(0.f, std::min(1.f, (now - start) / dur)) : 0.f;
        if (ty == "particlesystem") emitParticles(g, in, now - start);
        else if (ty == "sprite" && within) emitSprite(g, in, u);
        else if (ty == "model" && within) emitModel(g, in, u);
        else if (ty == "lightning" && within && in.boltN > 1) emitBolt(g, in, now - start);
        else if (ty == "trail") emitTrail(g, in);
    }

    void emitParticles(Group& g, Inst& in, float local) {
        const vb::VfxEffect& e = *in.e; int phase = phaseOf(e.s("RenderPhase")); bool mod2 = e.b("Modulate2x"); GLuint tex = texOf(e.res("Texture")); std::array<float, 4> uv = e.uv("TextureUVs");
        if (phase == Blend && std::getenv("VB_NOTRANS")) return;
        float uvArr[4] = {uv[0], uv[1], uv[2], uv[3]}; std::vector<float>& out = batch(tex, phase);
        int ncol = e.i("NumColorsUsed", 0); float ct[8], cc[8][3]; for (int k = 0; k < 8 && k < std::max(ncol, 0); k++) { ct[k] = e.f("ColorTime" + std::to_string(k + 1)); auto c = e.rgb("Color" + std::to_string(k + 1)); cc[k][0] = c[0]; cc[k][1] = c[1]; cc[k][2] = c[2]; }
        bool adjA = e.b("AdjustParticleAlpha"), adjS = e.b("AdjustParticleScale"); std::string wA = e.s("ParticleAlphaWaveform"), wS = e.s("ParticleScaleWaveform");
        float fA = e.f("ParticleAlphaFreq", 1.f), pA = e.f("ParticleAlphaPhase", 0.f), fS = e.f("ParticleScaleFreq", 1.f), pS = e.f("ParticleScalePhase", 0.f);
        float sa0 = (float)e.i("StartParticleAlpha", 255), sa1 = (float)e.i("EndParticleAlpha", 0), ss0 = e.f("StartParticleScale", 1.f), ss1 = e.f("EndParticleScale", 1.f);
        bool sysA = e.b("AdjustSystemAlpha"), sysS = e.b("AdjustSystemScale"); float dur = e.f("Duration", 0.f), su = dur > 0.f ? std::max(0.f, std::min(1.f, local / dur)) : 0.f;
        float sysAlpha = sysA ? lerp((float)e.i("StartSystemAlpha", 255), (float)e.i("EndSystemAlpha", 255), wave(e.s("SystemAlphaWaveform"), su * e.f("SystemAlphaFreq", 1.f) + e.f("SystemAlphaPhase", 0.f))) / 255.f : 1.f;
        float sysScale = sysS ? lerp(e.f("StartSystemScale", 1.f), e.f("EndSystemScale", 1.f), wave(e.s("SystemScaleWaveform"), su * e.f("SystemScaleFreq", 1.f) + e.f("SystemScalePhase", 0.f))) : 1.f;
        bool faceVel = e.b("FaceVelocityVector"), isTrail = e.s("ParticleTrailType") == "VFXPARTICLETRAILTYPE_LINE";
        if (std::getenv("VB_DEBUG_SPARKS") && e.name() == "sparks2" && !in.ps.empty()) { const Particle& q = in.ps.front(); std::fprintf(stderr, "sparks2: %zu particles, first at %.1f %.1f %.1f size %.2f age %.2f/%.2f\n", in.ps.size(), q.p.x, q.p.y, q.p.z, q.size, q.age, q.life); }
        for (const Particle& p : in.ps) {
            float t = p.age / p.life; float alpha = adjA ? lerp(sa0, sa1, wave(wA, t * fA + pA)) : p.alpha; alpha = alpha / 255.f * sysAlpha * g.alphaMul * fa(p.p.x, p.p.z);
            float scale = (adjS ? lerp(ss0, ss1, wave(wS, t * fS + pS)) : 1.f) * sysScale; float size = p.size * scale * g.sizeMul; { static float k = std::getenv("VB_PSIZE") ? (float)std::atof(std::getenv("VB_PSIZE")) : 1.f; size *= k; }
            float r = 1, gC = 1, b = 1;
            if (ncol > 0) { int k = 0; while (k + 1 < ncol && t >= ct[k + 1]) k++; if (k + 1 < ncol) { float f = (t - ct[k]) / std::max(1e-4f, ct[k + 1] - ct[k]); f = std::max(0.f, std::min(1.f, f)); r = lerp(cc[k][0], cc[k + 1][0], f); gC = lerp(cc[k][1], cc[k + 1][1], f); b = lerp(cc[k][2], cc[k + 1][2], f); } else { r = cc[k][0]; gC = cc[k][1]; b = cc[k][2]; } }
            if (mod2) { r *= 2.f; gC *= 2.f; b *= 2.f; }
            if (g.boltLook) { r = g.tint[0]; gC = g.tint[1]; b = g.tint[2]; }       // tuned groups: the particle colour is replaced (the file's sparks are pure blue)
            if (size > 0.f && alpha > 0.f) {
                V3 ax = right_ * (size * 0.5f), ay = up_ * (size * 0.5f); float ang = p.rot;
                if (faceVel) { float sp2 = p.v.x * p.v.x + p.v.y * p.v.y + p.v.z * p.v.z; if (sp2 > 1.f) ang = std::atan2(dot(p.v, up_), dot(p.v, right_)) - 1.5707963f; }
                if (ang != 0.f) { float c = std::cos(ang), s = std::sin(ang); V3 rx = right_ * (c * size) + up_ * (s * size), ry = right_ * (-s * size) + up_ * (c * size); ax = rx; ay = ry; }
                quad(out, p.p, ax * 0.5f, ay * 0.5f, r, gC, b, alpha, uvArr);
            }
            if (isTrail && p.trail.size() >= 1) {
                float tl = e.f("TrailLifetime", 0.1f); auto c0 = e.rgb("BeginTrailColor"), c1 = e.rgb("EndTrailColor"); float a0 = (float)e.i("BeginTrailAlpha", 255) / 255.f, a1 = (float)e.i("EndTrailAlpha", 0) / 255.f, s0 = e.f("BeginTrailScale", 1.f), s1 = e.f("EndTrailScale", 1.f);
                auto o = e.v3("RibbonOrigin"), en = e.v3("RibbonEndPoint"); float wid = std::sqrt((en[0] - o[0]) * (en[0] - o[0]) + (en[1] - o[1]) * (en[1] - o[1]) + (en[2] - o[2]) * (en[2] - o[2])) * kU * 0.35f * g.sizeMul;
                std::vector<V3> pts; std::vector<float> w; std::vector<std::array<float, 4>> col;
                for (const TrailPt& tp : p.trail) { float f = std::min(1.f, tp.age / std::max(1e-3f, tl)); pts.push_back(tp.p); w.push_back(wid * lerp(s0, s1, f)); col.push_back({g.boltLook ? g.tint[0] : lerp(c0[0], c1[0], f), g.boltLook ? g.tint[1] : lerp(c0[1], c1[1], f), g.boltLook ? g.tint[2] : lerp(c0[2], c1[2], f), lerp(a0, a1, f) * sysAlpha}); }
                pts.push_back(p.p); w.push_back(wid * s0); col.push_back({g.boltLook ? g.tint[0] : c0[0], g.boltLook ? g.tint[1] : c0[1], g.boltLook ? g.tint[2] : c0[2], a0 * sysAlpha});
                ribbon(batch(glow_, phaseOf(e.s("TrailRenderPhase"))), pts, w, col);
            }
        }
        (void)g;
    }

    void emitSprite(Group& g, Inst& in, float u) {
        const vb::VfxEffect& e = *in.e; float M[16]; attach(g, e, "Target", "SnapBone", "Offset", "Rotation", M);
        int phase = phaseOf(e.s("RenderPhase")); GLuint tex = texOf(e.res("Texture")); std::array<float, 4> uv = e.uv("TextureUVs"); float uvArr[4] = {uv[0], uv[1], uv[2], uv[3]};
        float w = e.f("Width", 0.1f) * kU, h = e.f("Height", 0.1f) * kU;
        if (e.b("AdjustScale")) { float f = wave(e.s("ScaleWaveform"), u * e.f("ScaleFreq", 1.f) + e.f("ScalePhase", 0.f)); w *= lerp(e.f("InitialScaleX", 1.f), e.f("FinalScaleX", 1.f), f); h *= lerp(e.f("InitialScaleY", 1.f), e.f("FinalScaleY", 1.f), f); }
        auto c0 = e.rgb("Color"), c1 = e.rgb("FinalColor"); float f = e.b("AdjustColor") ? wave(e.s("ColorWaveform"), u * e.f("ColorFreq", 1.f) + e.f("ColorPhase", 0.f)) : 0.f;
        float a = (float)e.i("Alpha", 255); if (e.b("AdjustAlpha")) a = lerp(a, (float)e.i("FinalAlpha", 0), wave(e.s("AlphaWaveform"), u * e.f("AlphaFreq", 1.f) + e.f("AlphaPhase", 0.f)));
        float rr_ = lerp(c0[0], c1[0], f), gg = lerp(c0[1], c1[1], f), bb = lerp(c0[2], c1[2], f); if (e.b("Modulate2x")) { rr_ *= 2; gg *= 2; bb *= 2; }
        V3 c{M[12], M[13], M[14]}; std::vector<float>& out = batch(tex, phase);
        if (std::getenv("VB_DEBUG_FX")) std::fprintf(stderr, "sprite %s tex %u phase %d at %.1f %.1f %.1f size %.2f x %.2f alpha %.0f col %.2f %.2f %.2f rotatable %d\n", e.name().c_str(), tex, phase, c.x, c.y, c.z, w, h, a, rr_, gg, bb, (int)e.b("Rotatable"));
        if (e.b("Rotatable")) {                                                  // a flat decal on the ground, turned with its anchor
            V3 fx = rot(M, {1, 0, 0}), fz = rot(M, {0, 0, 1}); V3 ax{fx.x, 0, fx.z}, az{fz.x, 0, fz.z}; ax = norm(ax) * (w * 0.5f); az = norm(az) * (h * 0.5f);
            if (e.res("Texture").find("footprint") != std::string::npos && !g.self.bone) {             // a footprint: toes (the top of the picture) point where the walker faces; the Rotation in the file is for the engine's own quad layout
                V3 F = rot(g.self.M, {0, 0, 1}); F.y = 0; F = norm(F); az = F * (h * 0.5f); ax = V3{-F.z, 0, F.x} * (w * 0.5f);
            }
            float gy; if (groundY && groundY(c.x, c.z, gy)) c.y = gy + 0.25f;
            if (std::getenv("VB_FPDEBUG")) { rr_ = 1.f; gg = 0.f; bb = 1.f; a = 255.f; ax = ax * 4.f; az = az * 4.f; }
            decal(out, c, ax, az, rr_, gg, bb, a / 255.f, uvArr);
        } else quad(out, c, right_ * (w * 0.5f), up_ * (h * 0.5f), rr_, gg, bb, a / 255.f * fa(c.x, c.z), uvArr);
    }

    void emitModel(Group& g, Inst& in, float u) {
        const vb::VfxEffect& e = *in.e; std::string m = e.res("Model"); if (m.empty()) return;
        size_t dot = m.rfind('.'); if (dot != std::string::npos) m = m.substr(0, dot);
        std::string gp = R_->elib.g3dPath(m); if (gp.empty()) return; vb::ModelPart mp; mp.g3d_path = gp; gm::GpuModel* gm = R_->getModel(mp); if (!gm) return;
        float M[16], S[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, W[16]; attach(g, e, "Target", "SnapBone", "Offset", "Rotation", M);
        float sx = e.f("ScaleX", 1.f), sy = e.f("ScaleY", 1.f), sz = e.f("ScaleZ", 1.f);
        if (e.b("AdjustScale")) { float f = wave(e.s("ScaleWaveform"), u * e.f("ScaleFreq", 1.f) + e.f("ScalePhase", 0.f)); sx = lerp(sx, e.f("FinalScaleX", sx), f); sy = lerp(sy, e.f("FinalScaleY", sy), f); sz = lerp(sz, e.f("FinalScaleZ", sz), f); }
        S[0] = sx; S[5] = sy; S[10] = sz; mat4mul(W, M, S);
        ModelDraw d; d.model = gm; std::memcpy(d.M, W, 64); d.phase = phaseOf(e.s("RenderPhase")); d.r = d.g = d.b = d.a = 1.f;
        if (e.b("UseColor")) { auto c0 = e.rgb("Color"), c1 = e.rgb("FinalColor"); float f = e.b("AdjustColor") ? wave(e.s("ColorWaveform"), u * e.f("ColorFreq", 1.f) + e.f("ColorPhase", 0.f)) : 0.f; d.r = lerp(c0[0], c1[0], f); d.g = lerp(c0[1], c1[1], f); d.b = lerp(c0[2], c1[2], f); }
        if (e.b("UseAlpha")) { float a = (float)e.i("Alpha", 255); if (e.b("AdjustAlpha")) a = lerp(a, (float)e.i("FinalAlpha", 0), wave(e.s("AlphaWaveform"), u * e.f("AlphaFreq", 1.f) + e.f("AlphaPhase", 0.f))); d.a = a / 255.f; }
        modelDraws_.push_back(d);
    }

    void emitBolt(Group& g, Inst& in, float local) {
        const vb::VfxEffect& e = *in.e; float life = std::max(0.02f, rr(e.f("MinLifetime", 0.1f), e.f("MaxLifetime", 0.1f))), delay = rr(e.f("MinRepeatDelay", 0.f), e.f("MaxRepeatDelay", 0.f));
        if (e.i("RepeatCount", 0) != 0 && delay > 0.f) { float per = life + delay; if (std::fmod(local, per) > life) return; }
        int phase = phaseOf(e.s("RenderPhase")); auto oc = e.rgb("OuterColor"), ic = e.rgb("InnerColor"); float oa = (float)e.i("OuterAlpha", 255) / 255.f, ia = (float)e.i("InnerAlpha", 255) / 255.f;
        float wd = rr(e.f("MinWidth", 0.02f), e.f("MaxWidth", 0.04f)) * kU * 0.5f;
        std::vector<V3> pts(in.bolt, in.bolt + in.boltN); std::vector<float> w1(pts.size(), wd * 2.f), w2(pts.size(), wd * 0.8f);
        if (g.boltLook) { ic = {g.boltInner[0], g.boltInner[1], g.boltInner[2]}; if (g.boltOuterA >= 0.f) oa = g.boltOuterA; }
        std::vector<std::array<float, 4>> c1(pts.size(), {oc[0], oc[1], oc[2], oa}), c2(pts.size(), {ic[0], ic[1], ic[2], ia});
        ribbon(batch(glow_, phase), pts, w1, c1); ribbon(batch(glow_, phase), pts, w2, c2); (void)g;
    }

    void emitTrail(Group& g, Inst& in) {
        const vb::VfxEffect& e = *in.e; if (in.ribbon.size() < 2) return; float life = std::max(1e-3f, e.f("SegmentLifetime", 0.2f)); int phase = phaseOf(e.s("RenderPhase"));
        auto c0 = e.rgb("Color1"), c1 = e.rgb("Color2"); float a0 = (float)e.i("Alpha1", 255) / 255.f, a1 = (float)e.i("Alpha2", 0) / 255.f, w0 = e.f("WidthScale1", 1.f), w1 = e.f("WidthScale2", 0.f);
        std::vector<float>& out = batch(glow_, phase);
        for (size_t i = 0; i + 1 < in.ribbon.size(); i++) {                                  // a strip of quads between consecutive (point, end point) pairs
            const RibbonPt &p = in.ribbon[i], &q = in.ribbon[i + 1]; float fa = std::min(1.f, p.age / life), fb = std::min(1.f, q.age / life);
            auto col = [&](float f) { return std::array<float, 4>{lerp(c0[0], c1[0], f), lerp(c0[1], c1[1], f), lerp(c0[2], c1[2], f), lerp(a0, a1, f)}; };
            auto ca = col(fa), cb = col(fb); V3 pa0 = p.a, pa1 = p.a + (p.b - p.a) * lerp(w0, w1, fa), qa0 = q.a, qa1 = q.a + (q.b - q.a) * lerp(w0, w1, fb);
            auto vtx = [&](V3 v, float uu, const std::array<float, 4>& c) { out.insert(out.end(), {v.x, v.y, v.z, uu, 0.5f, c[0], c[1], c[2], c[3]}); };
            vtx(pa0, 0, ca); vtx(pa1, 1, ca); vtx(qa1, 1, cb); vtx(pa0, 0, ca); vtx(qa1, 1, cb); vtx(qa0, 0, cb);
        }
        (void)g;
    }
};

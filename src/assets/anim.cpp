#include "assets/vfs.h"
#include "anim.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

namespace vb {
namespace {
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
void matMul(const float* a, const float* b, float* out) {          // out = a * b (row-major, row-vector convention)
    float r[16];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float s = 0; for (int k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j]; r[i * 4 + j] = s; }
    memcpy(out, r, sizeof r);
}
}

void transformMatrix(const GrTransform& t, float m[16]) {
    float x = t.orient[0], y = t.orient[1], z = t.orient[2], w = t.orient[3];
    float n = x * x + y * y + z * z + w * w, s = n > 0 ? 2.f / n : 0.f;
    // rotation matrix for column vectors, then transposed for the row-vector convention
    float R[3][3] = {{1 - s * (y * y + z * z), s * (x * y - z * w), s * (x * z + y * w)},
                     {s * (x * y + z * w), 1 - s * (x * x + z * z), s * (y * z - x * w)},
                     {s * (x * z - y * w), s * (y * z + x * w), 1 - s * (x * x + y * y)}};
    float Rr[3][3]; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) Rr[i][j] = R[j][i];
    float L[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float a = 0; for (int k = 0; k < 3; k++) a += t.ss[i * 3 + k] * Rr[k][j]; L[i][j] = a; }
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) m[i * 4 + j] = L[i][j]; m[i * 4 + 3] = 0; }
    m[12] = t.pos[0]; m[13] = t.pos[1]; m[14] = t.pos[2]; m[15] = 1;
}

void sampleCurve(const GrCurve& c, float t, float* out) {
    int d = c.dim; size_t n = c.controls.size() / (size_t)d;
    if (n == 0) return;
    size_t i = 0;
    if (c.knots.size() >= n && n > 1) {
        i = (size_t)(std::upper_bound(c.knots.begin(), c.knots.begin() + n, t) - c.knots.begin());
        i = i ? i - 1 : 0;
    }
    const float* a = &c.controls[i * d];
    if (i + 1 < n && c.knots.size() >= n && c.knots[i + 1] > c.knots[i]) {
        float u = std::min(1.f, std::max(0.f, (t - c.knots[i]) / (c.knots[i + 1] - c.knots[i])));
        const float* b = &c.controls[(i + 1) * d];
        float sgn = 1.f;
        if (d == 4) { float dp = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; if (dp < 0) sgn = -1.f; }   // shortest arc
        for (int k = 0; k < d; k++) out[k] = a[k] + (sgn * b[k] - a[k]) * u;
    } else for (int k = 0; k < d; k++) out[k] = a[k];
    if (d == 4) { float l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]); if (l > 0) for (int k = 0; k < 4; k++) out[k] /= l; }
}

void poseSkeleton(const GrSkeleton& sk, const GrAnimation* anim, float time, std::vector<float>& skin, std::vector<float>* worldOut) {
    size_t nb = sk.bones.size();
    skin.assign(nb * 16, 0.f);
    std::vector<float> world(nb * 16, 0.f); std::vector<char> done(nb, 0);
    std::map<std::string, const GrTrack*> tracks;
    float t = time;
    if (anim) { for (const GrTrack& tr : anim->tracks) tracks[tr.name] = &tr; if (anim->duration > 0) t = std::fmod(std::max(0.f, time), anim->duration); }
    std::vector<int> order;                       // parents before children, whatever the file order is
    std::function<void(int)> visit = [&](int i) {
        if (done[(size_t)i]) return;
        int p = sk.bones[(size_t)i].parent;
        if (p >= 0 && p != i && (size_t)p < nb) visit(p);
        const GrBone& b = sk.bones[(size_t)i];
        GrTransform tr = b.local;
        auto it = tracks.find(b.name);
        if (it != tracks.end()) {
            float v[9];
            if (!it->second->position.empty()) { sampleCurve(it->second->position, t, v); memcpy(tr.pos, v, 12); }
            if (!it->second->orientation.empty()) { sampleCurve(it->second->orientation, t, v); memcpy(tr.orient, v, 16); }
            if (!it->second->scaleShear.empty()) { sampleCurve(it->second->scaleShear, t, v); memcpy(tr.ss, v, 36); }
        }
        for (int k = 0; k < 3; k++) tr.pos[k] *= kSkeletonUnitScale;
        float L[16]; transformMatrix(tr, L);
        if (p >= 0 && (size_t)p < nb && p != i) matMul(L, &world[(size_t)p * 16], &world[(size_t)i * 16]);
        else memcpy(&world[(size_t)i * 16], L, 64);
        float inv[16]; memcpy(inv, b.inverseWorld, 64); for (int k = 12; k < 15; k++) inv[k] *= kSkeletonUnitScale;
        matMul(inv, &world[(size_t)i * 16], &skin[(size_t)i * 16]);
        done[(size_t)i] = 1;
    };
    for (size_t i = 0; i < nb; i++) visit((int)i);
    if (worldOut) *worldOut = world;
}

// ---- library -------------------------------------------------------------------------------------------------------------------
AnimLibrary::AnimLibrary(const std::string& root) {
    for (const std::string& p : vfs::walk(root)) {
        fs::path fp(p); std::string ext = lower(fp.extension().string());
        if (ext == ".skel") skel_.emplace(lower(fp.stem().string()), p);
        else if (ext == ".anim") anim_.emplace(lower(fp.stem().string()), p);
    }
}

const Gr2* AnimLibrary::load(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) return it->second.get();
    std::unique_ptr<Gr2> g;
    try { g = std::make_unique<Gr2>(loadGr2(path)); } catch (...) {}
    return (cache_[path] = std::move(g)).get();
}

std::string AnimLibrary::skelKey(const std::string& g3dStem) const {
    std::string s = lower(g3dStem);
    if (s.size() >= 8 && s.compare(0, 3, "hum") == 0 && (s[3] == 'm' || s[3] == 'f') && s[7] == '_') s = s.substr(0, 7);   // HumMNor_BodVaultSuit -> HumMNor
    return s;
}

const GrSkeleton* AnimLibrary::skeletonFor(const std::string& g3dStem) {
    auto it = skel_.find(skelKey(g3dStem));
    if (it == skel_.end()) return nullptr;
    const Gr2* g = load(it->second);
    if (!g || g->skeletons.empty()) return nullptr;
    // some files list helper skeletons first (a lone "Light" bone in the door files): take the one with the most bones
    const GrSkeleton* best = &g->skeletons[0];
    for (const GrSkeleton& k : g->skeletons) if (k.bones.size() > best->bones.size()) best = &k;
    return best;
}

std::vector<std::string> AnimLibrary::animationsFor(const std::string& g3dStem) const {
    std::string key = skelKey(g3dStem), prefix = key + "_";
    if (key.size() == 7 && key.compare(0, 3, "hum") == 0) prefix = key.substr(0, 4) + "_";      // HumMNor -> "humm_"
    std::vector<std::string> out;
    for (auto& a : anim_) if (a.first.compare(0, prefix.size(), prefix) == 0) out.push_back(fs::path(a.second).stem().string());
    return out;
}

std::string AnimLibrary::idleFor(const std::string& g3dStem) const {
    std::vector<std::string> names = animationsFor(g3dStem);
    for (const char* want : {"_def_una_idle", "_com_una_idle", "_def_1h_idle", "_com_1h_idle", "idle"})
        for (const std::string& n : names) { std::string l = lower(n); if (l.find(want) != std::string::npos && l.find("prone") == std::string::npos) return n; }
    return "";
}

const GrAnimation* AnimLibrary::animation(const std::string& animStem) {
    auto it = anim_.find(lower(animStem));
    if (it == anim_.end()) return nullptr;
    const Gr2* g = load(it->second);
    return g && !g->animations.empty() ? &g->animations[0] : nullptr;
}

} // namespace vb

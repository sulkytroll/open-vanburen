// Loads every Granny .skel/.anim under the assets dir with the native reader and prints corpus statistics.
#include "assets/gr2.h"
#include "assets/anim.h"
#include "assets/g3d.h"
#include <cstdio>
#include <filesystem>
#include <cmath>
#include <map>
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "../assets";
    int ok = 0, bad = 0, skels = 0, anims = 0; size_t bones = 0, tracks = 0;
    std::map<std::string, int> curveStats;
    for (auto& e : fs::recursive_directory_iterator(root)) {
        std::string ext = e.path().extension().string();
        if (ext != ".skel" && ext != ".anim") continue;
        try {
            vb::Gr2 g = vb::loadGr2(e.path().string()); ok++;
            skels += (int)g.skeletons.size(); anims += (int)g.animations.size();
            for (auto& s : g.skeletons) bones += s.bones.size();
            for (auto& a : g.animations) for (auto& t : a.tracks) {
                tracks++;
                for (const vb::GrCurve* c : {&t.position, &t.orientation, &t.scaleShear}) {
                    if (c->empty()) { curveStats["empty"]++; continue; }
                    size_t n = c->controls.size() / c->dim;
                    char buf[96]; std::snprintf(buf, sizeof buf, "dim%d deg%d knots-ctrl=%d %s", c->dim, c->degree, (int)c->knots.size() - (int)n, n > 1 ? "multi" : "single");
                    curveStats[buf]++;
                }
            }
        } catch (std::exception& ex) { bad++; std::printf("FAIL %s: %s\n", e.path().c_str(), ex.what()); }
    }
    // mesh <-> skeleton binding by bone name, and rest-pose sanity (skin matrices ~ identity)
    {
        vb::AnimLibrary lib(root);
        int skinned = 0, noskel = 0, bound = 0, missingNames = 0, badIdx = 0, badWeights = 0; double worstRest = 0;
        std::map<std::string, int> noskelBy;
        for (auto& e : fs::recursive_directory_iterator(root)) {
            if (e.path().extension() != ".g3d") continue;
            vb::Mesh m; try { m = vb::parseG3D(e.path().string()); } catch (...) { continue; }
            if (!m.skinned()) continue;
            skinned++;
            std::string stem = e.path().stem().string();
            const vb::GrSkeleton* sk = lib.skeletonFor(stem);
            if (!sk) { noskel++; noskelBy[e.path().parent_path().filename().string()]++; continue; }
            bool allFound = true;
            for (const std::string& bn : m.bones) { bool f = false; for (auto& b : sk->bones) if (b.name == bn) { f = true; break; } if (!f) { allFound = false; } }
            if (!allFound) { missingNames++; std::printf("  name mismatch: %s (skeleton has %zu bones; mesh uses:", stem.c_str(), sk->bones.size()); for (const std::string& bn : m.bones) { bool f = false; for (auto& b : sk->bones) if (b.name == bn) f = true; if (!f) std::printf(" '%s'", bn.c_str()); } std::printf(")\n"); } else bound++;
            for (const vb::Vertex& v : m.vertices) {
                float sum = 0; for (int k = 0; k < 4; k++) { if (v.bone[k] >= (int)m.bones.size()) badIdx++; if (v.bone[k] >= 0) sum += v.weight[k]; }
                if (sum > 0 && std::fabs(sum - 1.f) > 0.02f) badWeights++;
            }
            std::vector<float> skin; vb::poseSkeleton(*sk, nullptr, 0, skin);
            for (size_t i = 0; i < sk->bones.size(); i++) for (int k = 0; k < 16; k++) worstRest = std::max(worstRest, (double)std::fabs(skin[i * 16 + k] - ((k % 5 == 0) ? 1.f : 0.f)));
        }
        std::printf("skinned meshes=%d  bound=%d  name-mismatch=%d  no-skeleton=%d  bad-bone-index verts=%d  weight-sum!=1 verts=%d  worst rest-pose error=%.2g\n", skinned, bound, missingNames, noskel, badIdx, badWeights, worstRest);
        for (auto& k : noskelBy) std::printf("  no skeleton in %s: %d\n", k.first.c_str(), k.second);
    }
    std::printf("ok=%d fail=%d skeletons=%d bones=%zu animations=%d tracks=%zu\n", ok, bad, skels, bones, anims, tracks);
    for (auto& k : curveStats) std::printf("  %-40s %d\n", k.first.c_str(), k.second);
}

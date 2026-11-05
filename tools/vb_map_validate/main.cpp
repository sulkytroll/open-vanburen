// vb_map_validate <assets_dir> — load every .map/.tre/.col under assets/Maps and assets/Engine.
#include "common/gamedir.h"
#include "assets/map.h"
#include "assets/tre.h"
#include "assets/col.h"
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
int main(int argc, char** argv) {
    // usage: vb_map_validate [game folder | extracted assets folder]   (default: the tech demo folder, see common/gamedir.h)
    std::string root = argc > 1 ? argv[1] : "";
    if (root.empty()) { if (!findAndMountGame("", root)) return 1; }
    else if (vb::vfs::isGameDir(root)) { std::string err; if (!vb::vfs::mountGame(root, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; } root = fs::absolute(root).lexically_normal().generic_string(); }
    int ok = 0, bad = 0; size_t tris = 0, ents = 0, lights = 0, lcells = 0;
    for (const char* sub : {"Maps", "Engine"}) {
        std::string dir = root + "/" + sub;
        for (const std::string& fn : vb::vfs::list(dir)) {
            std::string ext = fs::path(fn).extension().string(), p = dir + "/" + fn;
            try {
                if (ext == ".map") { auto m = vb::loadMap(p); ents += m.entities.size(); ok++; }
                else if (ext == ".tre") {
                    auto t = vb::loadTre(p);
                    lights += t.lights.size();
                    if (t.has_grid)       // decode every cell (also dumps `-lights` data for lgt cross-check)
                        for (uint32_t row = 0; row < t.grid.h; row++) for (uint32_t col = 0; col < t.grid.w; col++)
                            if (!t.grid.visibleLights(t.grid.min_x + col * t.grid.sx, t.grid.max_z - row * t.grid.sy).empty()) lcells++;
                    for (auto& l : t.leaves) for (auto& b : l.batches) tris += b.indices.size() / 3;
                    ok++;
                } else if (ext == ".col") { vb::loadCol(p); ok++; }
                else continue;
            } catch (const std::exception& ex) { bad++; std::printf("FAIL %s: %s\n", p.c_str(), ex.what()); }
        }
    }
    std::printf("ok=%d fail=%d  (entities=%zu, octree triangles=%zu, lights=%zu, cells with lights=%zu)\n", ok, bad, ents, tris, lights, lcells);
    return bad ? 1 : 0;
}

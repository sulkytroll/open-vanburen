// vb_vfs_test <game_dir> <extracted_assets_dir>
// Mounts the packaged game folder and checks that every file of an extracted copy (vb_extract output) reads back identically through the VFS,
// that the Stream, exists, list and walk calls agree, and that the file counts match.
#include "assets/vfs.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <game_dir> <extracted_assets_dir>\n", argv[0]); return 2; }
    std::string game = fs::absolute(argv[1]).lexically_normal().generic_string(), ext = argv[2], err;
    auto t0 = std::chrono::steady_clock::now();
    if (!vb::vfs::mountGame(game, err)) { std::fprintf(stderr, "mount: %s\n", err.c_str()); return 1; }
    std::printf("mounted in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    long ok = 0, bad = 0, missing = 0, extra = 0; std::set<std::string> seen;
    auto t1 = std::chrono::steady_clock::now();
    for (auto& e : fs::recursive_directory_iterator(ext)) {
        if (!e.is_regular_file()) continue;
        std::string rel = fs::relative(e.path(), ext).generic_string(), vp = game + "/" + rel;
        std::vector<uint8_t> a, b; { std::ifstream f(e.path(), std::ios::binary); a.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
        if (!vb::vfs::exists(vp)) { if (missing++ < 10) std::printf("missing: %s\n", rel.c_str()); continue; }
        vb::vfs::readBytes(vp, b);
        vb::vfs::Stream s(vp); std::vector<uint8_t> c((size_t)s.size()); if (s.size()) s.read((char*)c.data(), (std::streamsize)s.size());
        if (a == b && a == c) ok++; else if (bad++ < 10) std::printf("differs: %s (%zu vs %zu)\n", rel.c_str(), a.size(), b.size());
        seen.insert(rel);
    }
    size_t walked = vb::vfs::walk(game).size();
    for (auto& p : vb::vfs::walk(game)) { std::string rel = p.substr(game.size() + 1); if (!seen.count(rel)) { if (extra++ < 10) std::printf("not in the extracted copy: %s\n", rel.c_str()); } }
    std::printf("compared in %.1f s: %ld identical, %ld differ, %ld missing from the VFS, %ld only in the VFS; walk() lists %zu files\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count(), ok, bad, missing, extra, walked);
    std::printf("list(root) groups: %zu, list(Maps): %zu\n", vb::vfs::list(game).size(), vb::vfs::list(game + "/Maps").size());
    return (bad || missing) ? 1 : 0;
}

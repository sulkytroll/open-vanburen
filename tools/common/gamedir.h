#pragma once
// Finding and mounting the tech demo folder: shared by every tool that reads game files.
#include "assets/vfs.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// Looks for the game folder (the one holding resource.rht and data/) in this order: the -game argument (gameArg), $VB_GAME_DIR, the folder of the AppImage file
// ($APPIMAGE: inside an AppImage the executable itself sits in a temporary mount), the folder of the running executable, the current folder. The first one found is mounted; `dir` then holds its absolute path, which is also the root of the virtual file system
// ("<dir>/<Group>/<name>.<ext>" is a packaged resource). Prints what is wrong and returns false when there is none.
inline bool findAndMountGame(const std::string& gameArg, std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> cand;
    if (!gameArg.empty()) cand.push_back(gameArg);
    else {
        if (const char* g = std::getenv("VB_GAME_DIR")) cand.push_back(g);
        if (const char* ai = std::getenv("APPIMAGE")) cand.push_back(fs::path(ai).parent_path().string());
        std::error_code ec; fs::path exe = fs::read_symlink("/proc/self/exe", ec); if (!ec) cand.push_back(exe.parent_path().string());
        cand.push_back(fs::current_path().string());
    }
    for (const std::string& c : cand) {
        if (!vb::vfs::isGameDir(c)) continue;
        std::string err;
        if (vb::vfs::mountGame(c, err)) { dir = fs::absolute(c).lexically_normal().generic_string(); return true; }
        std::fprintf(stderr, "%s\n", err.c_str());
    }
    std::fprintf(stderr, "Van Buren tech demo folder not found (it holds resource.rht and data/). Run this next to F3.exe, or use -game DIR (or set VB_GAME_DIR).\n");
    return false;
}

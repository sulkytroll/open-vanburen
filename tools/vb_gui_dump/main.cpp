// Parses every Interface/*.gui (or the given files) and prints the widget tree of one of them.   vb_gui_dump [-tree FILE] [DIR]
#include "common/gamedir.h"
#include "assets/gui.h"
#include <cstdio>
#include <filesystem>
namespace fs = std::filesystem;
static void dump(const vb::GuiWidget& w, int ind) {
    static const char* t[] = {"win", "btn", "pic", "lbl", "lb4"};
    std::printf("%*s%s %-24s (%d,%d,%d,%d) col %08x", ind * 2, "", t[(int)w.type], w.name.c_str(), w.rect.x0, w.rect.y0, w.rect.x1, w.rect.y1, w.color);
    if (w.type == vb::GuiType::Label) std::printf(" text#%u font %s %u align %u", w.textId, w.font.c_str(), w.fontSize, w.align);
    if (!w.image().empty() && w.type == vb::GuiType::Picture) std::printf(" img %s", w.image().c_str());
    std::printf("\n"); for (auto& c : w.children) dump(*c, ind + 1);
}
int main(int argc, char** argv) {
    // usage: vb_gui_dump [-game DIR | an extracted Interface folder] [-tree File.gui]
    std::string dir, gameArg, tree;
    for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "-tree" && i + 1 < argc) tree = argv[++i]; else if (a == "-game" && i + 1 < argc) gameArg = argv[++i]; else dir = a; }
    if (dir.empty()) { std::string g; if (!findAndMountGame(gameArg, g)) return 1; dir = g + "/Interface"; }
    int ok = 0, bad = 0;
    for (const std::string& fn : vb::vfs::list(dir)) {
        if (fs::path(fn).extension() != ".gui") continue;
        try { vb::GuiFile g = vb::loadGui(dir + "/" + fn); ok++; if (fn == tree) for (auto& w : g.windows) dump(*w, 0); }
        catch (std::exception& ex) { bad++; std::printf("FAIL %s: %s\n", fn.c_str(), ex.what()); }
    }
    std::printf("gui files ok=%d fail=%d\n", ok, bad);
}

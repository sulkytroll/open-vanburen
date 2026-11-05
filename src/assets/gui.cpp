#include "assets/vfs.h"
#include "assets/gui.h"
#include "assets/binreader.h"
#include <fstream>
#include <iterator>

namespace vb {
namespace {

struct Rd {
    BinReader r;
    explicit Rd(const std::vector<uint8_t>& d) : r(d.data(), d.size()) {}
    int32_t i32() { return (int32_t)r.u32(); }
    std::string str() { uint32_t n = r.u32(); if (n > 4096) r.fail("string too long"); std::string s((const char*)r.ptr(), n); r.skip(n); return s; }
    GuiRect rect() { GuiRect q; q.x0 = i32(); q.y0 = i32(); q.x1 = i32(); q.y1 = i32(); return q; }
};

void fragment(Rd& d, GuiFragment& f) {
    f.flags = d.r.u32(); f.w = d.r.u32(); f.h = d.r.u32(); f.u3 = d.r.u32(); f.u4 = d.r.u32(); f.u5 = d.r.u32();
    f.texture = d.str(); f.scale = d.r.f32(); f.color = d.r.u32(); f.rect = d.rect();
}

std::unique_ptr<GuiWidget> widget(Rd& d, int ver, uint32_t type) {
    auto w = std::make_unique<GuiWidget>(); w->type = (GuiType)type;
    if (type > 4) d.r.fail("unknown window type");
    if (type == 1) { w->buttonId = d.r.u32(); if (ver >= 4) w->checked = d.r.u8(); for (auto& s : w->sounds) s = d.str(); }
    d.rect();                                           // design rectangle (equal to the working rect in every shipped file)
    w->name = d.str(); w->flagA = d.r.u8(); w->flagB = d.r.u8(); w->flagC = d.r.u8(); w->ini = d.str(); w->color = d.r.u32(); w->rect = d.rect();
    if (ver >= 2) { d.r.u8(); d.r.u32(); }
    if (ver >= 3) { d.str(); d.r.u32(); d.str(); d.r.u32(); d.str(); }
    for (auto& f : w->frags) fragment(d, f);
    for (;;) { uint32_t id = d.r.u32(); if (id == 0xffffffffu) break; d.str(); }       // id-keyed strings (unused by the shipped layouts' widgets we render)
    uint32_t n = d.r.u32();
    for (uint32_t i = 0; i < n; i++) { uint32_t t = d.r.u32(); w->children.push_back(widget(d, ver, t)); }
    if (type == 3 || type == 4) {                        // label: text string id, font face, then size / colour / alignment words
        w->textId = d.r.u32(); w->font = d.str();
        uint32_t l[4]; for (auto& x : l) x = d.r.u32(); w->fontSize = l[1]; w->textColor = l[3];
        d.r.u8(); d.r.u8(); w->align = d.r.u32(); for (int i = 0; i < 4; i++) d.r.u32(); d.r.u32();
    }
    return w;
}

} // namespace

GuiFile loadGui(const std::string& path) {
    vfs::Stream f(path); if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::string((const char*)data.data(), 4) != "GUI ") throw std::runtime_error("not a GUI file");
    std::string v((const char*)data.data() + 4, 4); GuiFile g;
    g.version = v == "V1.0" ? 1 : v == "V1.1" ? 2 : v == "V1.2" ? 3 : v == "V1.3" ? 4 : 0;
    if (!g.version) throw std::runtime_error("unknown GUI version " + v);
    Rd d(data); d.r.skip(8); g.name = d.str(); d.r.u8();
    uint32_t n = d.r.u32();
    for (uint32_t i = 0; i < n; i++) { uint32_t t = d.r.u32(); g.windows.push_back(widget(d, g.version, t)); }
    if (!d.r.done()) throw std::runtime_error("GUI: trailing bytes");
    return g;
}

} // namespace vb

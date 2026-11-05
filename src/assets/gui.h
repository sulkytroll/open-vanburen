#pragma once
// Interface layout files (`Interface/*.gui`, "GUI V1.0".."GUI V1.3"): a tree of windows (window / button / picture / label) with rectangles, skin fragments and string ids.
// Grammar decoded from F3.exe (loader 0x495b50, Window::Load 0x4b10f0, button 0x4a6b60, label 0x4aa640, fragment 0x4b20a0) -- see GAME_DATA_FINDINGS.md "GUI layouts".
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vb {

enum class GuiType : uint32_t { Window = 0, Button = 1, Picture = 2, Label = 3, Label4 = 4 };

struct GuiRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; int w() const { return x1 - x0; } int h() const { return y1 - y0; } };

struct GuiFragment {                  // one of the 9 skin pieces (corners, edges, middle) of a window; for pictures the middle one carries the image
    uint32_t flags = 0, w = 0, h = 0, u3 = 0, u4 = 0, u5 = 0;   // flags: 0 fixed, 1 stretch horizontally, 2 vertically, 3 both (the .ini's `flags`)
    std::string texture; float scale = 0; uint32_t color = 0; GuiRect rect;
};

struct GuiWidget {
    GuiType type = GuiType::Window;
    std::string name, ini;
    GuiRect rect;                     // position in the parent's coordinate space (1024x768 design space for the top window's children)
    uint32_t color = 0;               // ARGB tint
    uint32_t buttonId = 0; uint8_t checked = 0; std::string sounds[3];   // buttons
    uint8_t flagA = 0, flagB = 0, flagC = 0;                             // visible / ... (a,b,c bytes of Window::Load)
    GuiFragment frags[9];
    // labels: string-table id of the text, font face name, size/colour/alignment
    uint32_t textId = 0; std::string font; uint32_t fontSize = 0, textColor = 0, align = 0;
    std::vector<std::unique_ptr<GuiWidget>> children;
    const GuiWidget* find(const std::string& n) const { if (name == n) return this; for (auto& c : children) if (const GuiWidget* f = c->find(n)) return f; return nullptr; }
    // the image of a picture (the texture of its middle fragment, else the first named one)
    std::string image() const { if (!frags[4].texture.empty()) return frags[4].texture; for (auto& f : frags) if (!f.texture.empty()) return f.texture; return ""; }
};

struct GuiFile { std::string name; int version = 0; std::vector<std::unique_ptr<GuiWidget>> windows; };

// Throws std::runtime_error on malformed data or a file that is not consumed exactly.
GuiFile loadGui(const std::string& path);

} // namespace vb

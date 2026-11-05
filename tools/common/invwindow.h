#pragma once
// The inventory and container windows, laid out from the game's own `Interface/Inventory.gui` (1024x604 design units, stands directly above the action bar) and
// `Interface/Container.gui` (256x176). The backpack is a 3 x 6 grid of 60-unit cells, four equipment frames (head, body, two hands) sit in the middle column, the
// description box is bottom right. Drag an item onto a frame to equip it (or double-click / right-click); drag an equipped item back to the backpack (or double-click) to put it away.
#include "common/actionbar.h"
#include "game/world.h"
#include <chrono>
#include <cstring>
#include <functional>

class ItemWindows {
public:
    std::function<void(const char*)> sfx;                                             // plays an interface sound (wav name)
    void load(gm::TexCache& tc, ActionBar* bar, vb::World* world) { tc_ = &tc; bar_ = bar; w_ = world; }

    // ------------------------------------------------------------------------------------------------------------------------------ inventory
    struct Layout { float S = 1, x0 = 0, y0 = 0; };
    Layout layout(int dw, int dh) const {
        Layout L; L.S = std::max(1.f, (float)dh / 768.f); if (1024.f * L.S > (float)dw) L.S = (float)dw / 1024.f;
        L.x0 = ((float)dw - 1024.f * L.S) * 0.5f; L.y0 = (float)dh - 768.f * L.S; return L;
    }
    bool invOver(float mx, float my, int dw, int dh) const { Layout L = layout(dw, dh); return mx >= L.x0 && mx < L.x0 + 1024.f * L.S && my >= L.y0 && my < L.y0 + 604.f * L.S; }

    void selectEquip(int slot) { sel_ = {Hit::Equip, slot}; }          // (test hook)
    void selectBag(int idx) { sel_ = {Hit::Bag, idx}; }
    void invReset() { sel_ = {}; drag_ = false; scroll_ = 0; }
    void invDown(float mx, float my, int dw, int dh) {
        Layout L = layout(dw, dh); Hit h = invHit(mx, my, L);
        if (h.kind == Hit::ScrollUp) { scroll_ = std::max(0, scroll_ - 1); return; }
        if (h.kind == Hit::ScrollDown) { scroll_ = std::min(maxScroll(), scroll_ + 1); return; }
        if (!h.hasItem(*w_)) { sel_ = {}; return; }
        double now = nowMs(); bool dbl = sel_.kind == h.kind && sel_.idx == h.idx && now - lastClick_ < 400.0; lastClick_ = now;
        sel_ = h; if (dbl) { quick(h); drag_ = false; return; }
        drag_ = true; dragFrom_ = h; dragX_ = mx; dragY_ = my; if (sfx) sfx("InventoryPickUp.wav");
    }
    void invUp(float mx, float my, int dw, int dh) {
        if (!drag_) return; drag_ = false;
        if (std::hypot(mx - dragX_, my - dragY_) < 6.f) return;                       // a plain click only selects
        Layout L = layout(dw, dh); Hit t = invHit(mx, my, L);
        if (dragFrom_.kind == Hit::Bag) {
            if (t.kind == Hit::Equip) { if (w_->equip(dragFrom_.idx, t.idx)) { sel_ = {Hit::Equip, t.idx}; if (sfx) sfx("InventoryPutDown.wav"); } }
        } else if (dragFrom_.kind == Hit::Equip) {
            if (t.kind == Hit::Bag || (t.kind == Hit::None && invOver(mx, my, dw, dh))) { if (w_->unequip(dragFrom_.idx)) { sel_ = {}; if (sfx) sfx("InventoryPutDown.wav"); } }
            else if (t.kind == Hit::Equip && t.idx != dragFrom_.idx && dragFrom_.idx >= vb::SlotHand1 && t.idx >= vb::SlotHand1) { w_->swapHands(); sel_ = {Hit::Equip, t.idx}; if (sfx) sfx("InventoryPutDown.wav"); }
        }
    }
    void invRight(float mx, float my, int dw, int dh) { Hit h = invHit(mx, my, layout(dw, dh)); if (h.hasItem(*w_)) { sel_ = h; quick(h); } }
    void invWheel(float mx, float my, int dw, int dh, int dir) { if (invOver(mx, my, dw, dh)) scroll_ = std::max(0, std::min(maxScroll(), scroll_ - dir)); }

    void drawInv(UiDraw& ui, int dw, int dh, float mx, float my) {
        if (!bar_ || !w_) return; Layout L = layout(dw, dh); const float S = L.S;
        gm::TexInfo bg = tc_->get("f3_inventory", nullptr, true);
        if (bg.id) ui.image(bg.id, L.x0, L.y0, 1024.f * S, 604.f * S); else ui.solid(L.x0, L.y0, 1024.f * S, 604.f * S, {0.1f, 0.1f, 0.08f, 1.f});
        auto X = [&](float v) { return L.x0 + v * S; }; auto Y = [&](float v) { return L.y0 + v * S; };
        const Rgba gold{0.92f, 0.78f, 0.35f, 1.f}, green{0.65f, 1.f, 0.65f, 1.f}, dim{0.55f, 0.75f, 0.55f, 1.f};
        // frame titles (HEAD / BODY / HAND 1 / HAND 2)
        const int labelIds[4] = {124, 125, 126, 127}; const float labelY[4] = {32, 170, 308, 448};
        for (int i = 0; i < 4; i++) text(ui, "Gothic", 22.f * S, X(312), Y(labelY[i]), w_->text(labelIds[i]), gold);
        // backpack grid
        Hit hover = invHit(mx, my, L);
        for (int r = 0; r < 6; r++) for (int c = 0; c < 3; c++) {
            float cx = X(68.f + 64.f * (float)c), cy = Y(96.f + 64.f * (float)r), cs = 60.f * S; int bi = (scroll_ + r) * 3 + c;
            frame(ui, cx, cy, cs, cs, {0.36f, 0.36f, 0.30f, 0.55f});
            if (hover.kind == Hit::Bag && hover.idx == bi) ui.solid(cx, cy, cs, cs, {1.f, 1.f, 1.f, 0.10f});
            if (bi < (int)w_->bag.size()) {
                const vb::InvItem& it = w_->bag[(size_t)bi]; icon(ui, it.file, cx + 4.f * S, cy + 4.f * S, 52.f * S, drag_ && dragFrom_.kind == Hit::Bag && dragFrom_.idx == bi ? 0.35f : 1.f);
                if (it.count > 1) text(ui, "Fallouty", 9.f * S, cx + 6.f * S, cy + 42.f * S, "x" + std::to_string(it.count), {1.f, 1.f, 0.6f, 1.f});
                if (sel_.kind == Hit::Bag && sel_.idx == bi) outline(ui, cx, cy, cs, cs, gold, 2.f);
            }
        }
        // scroll arrows when the backpack holds more than six rows
        if (maxScroll() > 0) {
            const char* up = scroll_ > 0 ? "f3_chargen_up_up" : "f3_chargen_up_disabled"; const char* dn = scroll_ < maxScroll() ? "f3_chargen_down_up" : "f3_chargen_down_disabled";
            gm::TexInfo a = tc_->get(up, nullptr, true), b = tc_->get(dn, nullptr, true);
            if (a.id) ui.image(a.id, X(260), Y(56), 24.f * S, 24.f * S); if (b.id) ui.image(b.id, X(260), Y(84), 24.f * S, 24.f * S);
        }
        // equipment frames
        const float slotY[4] = {72, 212, 348, 488};
        for (int i = 0; i < 4; i++) {
            float sx = X(336), sy = Y(slotY[i]), ss = 76.f * S; const std::string& f = w_->equipped[i];
            if (hover.kind == Hit::Equip && hover.idx == i) ui.solid(X(316), sy, 116.f * S, ss, {1.f, 1.f, 1.f, 0.08f});
            if (drag_ && dragFrom_.kind == Hit::Bag && dragFrom_.idx < (int)w_->bag.size() && hover.kind == Hit::Equip && hover.idx == i) {
                vb::ItemInfo di = w_->itemInfo(w_->bag[(size_t)dragFrom_.idx].file); bool fits = di.slot == i || (di.slot == vb::SlotHand1 && i == vb::SlotHand2);
                outline(ui, X(316), sy, 116.f * S, ss, fits ? Rgba{0.4f, 1.f, 0.4f, 1.f} : Rgba{1.f, 0.35f, 0.3f, 1.f}, 2.f);
            }
            if (!f.empty()) { icon(ui, f, sx, sy, ss, drag_ && dragFrom_.kind == Hit::Equip && dragFrom_.idx == i ? 0.35f : 1.f); if (sel_.kind == Hit::Equip && sel_.idx == i) outline(ui, X(316), sy, 116.f * S, ss, gold, 2.f); }
        }
        // the held weapons' names next to the frames
        for (int h = 0; h < 2; h++) {
            float wx = 468.f, wy = h == 0 ? 268.f : 320.f; const std::string& f = w_->equipped[vb::SlotHand1 + h];
            text(ui, "Fallouty", 11.f * S, X(wx), Y(wy + 4), w_->text(h == 0 ? 118 : 122), dim);
            text(ui, "Fallouty", 11.f * S, X(wx + 56), Y(wy + 4), f.empty() ? std::string("-") : w_->itemInfo(f).name, green);
        }
        // description of the selected item
        std::string file = selectedFile();
        if (!file.empty()) {
            vb::ItemInfo inf = w_->itemInfo(file); float tx = X(460), ty = Y(400);
            text(ui, "Gothic", 22.f * S, tx, ty, inf.name, gold);
            TtFont* f = bar_->font("Fallouty", 12.f * S * 1.25f);
            if (f) { float y = ty + 28.f * S; for (const std::string& line : wrap(f, inf.desc, 468.f * S)) { f->draw(ui, tx, y, line, green); y += f->lineH; if (y > Y(572)) break; } }
        } else text(ui, "Fallouty", 12.f * S, X(460), Y(402), "Drag an item onto a frame to equip it. Double-click to equip or put away.", dim);
        // the dragged item follows the cursor
        if (drag_ && std::hypot(mx - dragX_, my - dragY_) >= 6.f) {
            std::string df = dragFrom_.kind == Hit::Bag && dragFrom_.idx < (int)w_->bag.size() ? w_->bag[(size_t)dragFrom_.idx].file : dragFrom_.kind == Hit::Equip ? w_->equipped[dragFrom_.idx] : "";
            if (!df.empty()) icon(ui, df, mx - 28.f * S, my - 28.f * S, 56.f * S, 0.85f);
        }
    }

    // ------------------------------------------------------------------------------------------------------------------------------ container
    int containerId = 0;                                            // 0 = closed
    struct CLayout { float S = 1, x0 = 0, y0 = 0; };
    CLayout clayout(int dw, int dh) const {
        CLayout L; L.S = std::max(1.f, (float)dh / 768.f) * 1.6f; L.x0 = ((float)dw - 256.f * L.S) * 0.5f; L.y0 = ((float)dh - 176.f * L.S) * 0.4f; return L;
    }
    bool containerOver(float mx, float my, int dw, int dh) const { if (!containerId) return false; CLayout L = clayout(dw, dh); return mx >= L.x0 && mx < L.x0 + 256.f * L.S && my >= L.y0 && my < L.y0 + 176.f * L.S; }
    void containerDown(float mx, float my, int dw, int dh) {
        if (!containerId) return; CLayout L = clayout(dw, dh); float S = L.S; auto in = [&](float x, float y, float w, float h) { return mx >= L.x0 + x * S && mx < L.x0 + (x + w) * S && my >= L.y0 + y * S && my < L.y0 + (y + h) * S; };
        if (in(129, 143, 126, 32)) { w_->takeAll(containerId); containerId = 0; return; }
        if (in(1, 143, 126, 32)) { containerId = 0; return; }
        std::vector<vb::InvItem> items = w_->cargoOf(containerId);
        for (int i = 0; i < 10 && i < (int)items.size(); i++) if (in(1.f + 51.f * (float)(i % 5), 41.f + 51.f * (float)(i / 5), 50, 50)) { w_->takeFromCargo(containerId, items[(size_t)i].file, items[(size_t)i].count); break; }
        if (w_->cargoOf(containerId).empty()) containerId = 0;
    }
    void drawContainer(UiDraw& ui, int dw, int dh, float mx, float my) {
        if (!containerId || !bar_) return; CLayout L = clayout(dw, dh); const float S = L.S; vb::Entity* e = w_->find(containerId); if (!e) { containerId = 0; return; }
        auto X = [&](float v) { return L.x0 + v * S; }; auto Y = [&](float v) { return L.y0 + v * S; };
        const Rgba gold{0.92f, 0.78f, 0.35f, 1.f}, green{0.65f, 1.f, 0.65f, 1.f};
        ui.solid(X(-3), Y(-3), 262.f * S, 182.f * S, {0.55f, 0.45f, 0.15f, 0.95f}); ui.solid(X(0), Y(0), 256.f * S, 176.f * S, {0.05f, 0.06f, 0.05f, 0.96f});
        std::string title = e->kind == vb::EntKind::Critter ? w_->nameOf(*e) : w_->itemInfo(e->file).name; if (title.empty() || title == e->file || title.find('_') != std::string::npos) title = pretty(title.empty() ? e->display : title);
        { TtFont* f = bar_->font("Gothic", 20.f * S * 0.8f * 1.25f); if (f) f->draw(ui, X(128) - f->width(title) * 0.5f, Y(8), title, gold); }
        std::vector<vb::InvItem> items = w_->cargoOf(containerId);
        for (int i = 0; i < 10; i++) {
            float cx = X(1.f + 51.f * (float)(i % 5)), cy = Y(41.f + 51.f * (float)(i / 5)), cs = 50.f * S; frame(ui, cx, cy, cs, cs, {0.4f, 0.4f, 0.33f, 0.8f});
            if (i < (int)items.size()) {
                bool hov = mx >= cx && mx < cx + cs && my >= cy && my < cy + cs; if (hov) ui.solid(cx, cy, cs, cs, {1.f, 1.f, 1.f, 0.10f});
                icon(ui, items[(size_t)i].file, cx + 3.f * S, cy + 3.f * S, 44.f * S, 1.f);
                if (items[(size_t)i].count > 1) text(ui, "Fallouty", 9.f * S, cx + 4.f * S, cy + 34.f * S, "x" + std::to_string(items[(size_t)i].count), {1.f, 1.f, 0.6f, 1.f});
            }
        }
        // names: the hovered item
        for (int i = 0; i < 10 && i < (int)items.size(); i++) { float cx = X(1.f + 51.f * (float)(i % 5)), cy = Y(41.f + 51.f * (float)(i / 5)), cs = 50.f * S; if (mx >= cx && mx < cx + cs && my >= cy && my < cy + cs) text(ui, "Fallouty", 10.f * S, X(4), Y(24), w_->itemInfo(items[(size_t)i].file).name, green); }
        struct B { float x; int str; } btn[2] = {{129, 1713}, {1, 75}};
        for (auto& b : btn) {
            bool hov = mx >= X(b.x) && mx < X(b.x + 126) && my >= Y(143) && my < Y(175);
            ui.solid(X(b.x), Y(143), 126.f * S, 32.f * S, hov ? Rgba{0.30f, 0.22f, 0.06f, 1.f} : Rgba{0.18f, 0.13f, 0.04f, 1.f}); outline(ui, X(b.x), Y(143), 126.f * S, 32.f * S, gold, 1.f);
            TtFont* f = bar_->font("Gothic", 20.f * S * 0.8f * 1.25f); std::string t = w_->text(b.str); if (f) f->draw(ui, X(b.x + 63) - f->width(t) * 0.5f, Y(143) + (32.f * S - f->lineH) * 0.5f, t, gold);
        }
    }

private:
    gm::TexCache* tc_ = nullptr; ActionBar* bar_ = nullptr; vb::World* w_ = nullptr;
    struct Hit { enum Kind { None, Bag, Equip, ScrollUp, ScrollDown } kind = None; int idx = -1; bool hasItem(vb::World& w) const { return kind == Bag ? idx >= 0 && idx < (int)w.bag.size() : kind == Equip && !w.equipped[idx].empty(); } };
    Hit sel_, dragFrom_; bool drag_ = false; float dragX_ = 0, dragY_ = 0; int scroll_ = 0; double lastClick_ = 0;

    static double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    int maxScroll() const { int rows = ((int)w_->bag.size() + 2) / 3; return std::max(0, rows - 6); }
    std::string selectedFile() const {
        if (sel_.kind == Hit::Bag && sel_.idx >= 0 && sel_.idx < (int)w_->bag.size()) return w_->bag[(size_t)sel_.idx].file;
        if (sel_.kind == Hit::Equip) return w_->equipped[sel_.idx];
        return "";
    }
    Hit invHit(float mx, float my, const Layout& L) const {
        auto in = [&](float x, float y, float w, float h) { return mx >= L.x0 + x * L.S && mx < L.x0 + (x + w) * L.S && my >= L.y0 + y * L.S && my < L.y0 + (y + h) * L.S; };
        Hit h;
        if (in(260, 56, 24, 24)) { h.kind = Hit::ScrollUp; return h; }
        if (in(260, 84, 24, 24)) { h.kind = Hit::ScrollDown; return h; }
        for (int r = 0; r < 6; r++) for (int c = 0; c < 3; c++) if (in(68.f + 64.f * (float)c, 96.f + 64.f * (float)r, 60, 60)) { h.kind = Hit::Bag; h.idx = (scroll_ + r) * 3 + c; return h; }
        const float slotY[4] = {72, 212, 348, 488};
        for (int i = 0; i < 4; i++) if (in(316, slotY[i], 116, 76)) { h.kind = Hit::Equip; h.idx = i; return h; }
        return h;
    }
    void quick(const Hit& h) {        // double-click / right-click: equip from the backpack, put an equipped item away
        if (h.kind == Hit::Bag && h.idx < (int)w_->bag.size() && w_->isUsable(w_->itemInfo(w_->bag[(size_t)h.idx].file))) { w_->useItem(h.idx); sel_ = {}; }
        else if (h.kind == Hit::Bag && h.idx < (int)w_->bag.size()) { int sl = w_->defaultSlot(w_->bag[(size_t)h.idx].file); if (sl >= 0) { if (w_->equip(h.idx, sl)) { sel_ = {Hit::Equip, sl}; if (sfx) sfx("InventoryPutDown.wav"); } } else w_->log.push_back("You cannot equip the " + w_->itemInfo(w_->bag[(size_t)h.idx].file).name + "."); }
        else if (h.kind == Hit::Equip) { if (w_->unequip(h.idx)) { sel_ = {}; if (sfx) sfx("InventoryPutDown.wav"); } }
    }
    void icon(UiDraw& ui, const std::string& file, float x, float y, float size, float alpha) {
        vb::ItemInfo inf = w_->itemInfo(file); gm::TexInfo t = inf.icon.empty() ? gm::TexInfo{} : tc_->get(inf.icon, nullptr, true);
        if (t.id) ui.image(t.id, x, y, size, size, {1.f, 1.f, 1.f, alpha}); else { ui.solid(x, y, size, size, {0.3f, 0.3f, 0.3f, 0.6f * alpha}); }
    }
    void text(UiDraw& ui, const std::string& face, float px, float x, float y, const std::string& s, Rgba c) { if (s.empty()) return; TtFont* f = bar_->font(face, std::max(8.f, px * 1.25f)); if (f) f->draw(ui, std::round(x), std::round(y), s, c); }
    static std::string pretty(std::string s) {        // "DS_PropsTribalGeneric_Shelf" -> "Tribal Generic Shelf"
        for (const char* pre : {"DS_Props", "GD_", "DS_"}) if (s.rfind(pre, 0) == 0) { s = s.substr(std::strlen(pre)); break; }
        std::string o; for (size_t i = 0; i < s.size(); i++) { char c = s[i]; if (c == '_') c = ' '; if (i && std::isupper((unsigned char)c) && std::islower((unsigned char)s[i - 1])) o.push_back(' '); o.push_back(c); }
        return o;
    }
    static void frame(UiDraw& ui, float x, float y, float w, float h, Rgba c) { outline(ui, x, y, w, h, c, 1.f); }
    static void outline(UiDraw& ui, float x, float y, float w, float h, Rgba c, float t) { ui.solid(x, y, w, t, c); ui.solid(x, y + h - t, w, t, c); ui.solid(x, y, t, h, c); ui.solid(x + w - t, y, t, h, c); }
    static std::vector<std::string> wrap(const TtFont* f, const std::string& s, float width) {
        std::vector<std::string> lines; std::string line, word;
        for (size_t i = 0; i <= s.size(); i++) {
            if (i == s.size() || s[i] == ' ' || s[i] == '\n') {
                if (!word.empty()) { if (!line.empty() && f->width(line + " " + word) > width) { lines.push_back(line); line = word; } else line += (line.empty() ? "" : " ") + word; word.clear(); }
                if (i < s.size() && s[i] == '\n') { lines.push_back(line); line.clear(); }
            } else word.push_back(s[i]);
        }
        if (!line.empty()) lines.push_back(line); return lines;
    }
};

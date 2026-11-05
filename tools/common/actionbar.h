#pragma once
// The bottom interface bar, drawn straight from the game's own layout file `Interface/Actionbar.gui` (1024x164 design units, scaled to the window):
// background picture, the two weapon slots, the party strip (health / fatigue bars), the HEALTH / FATIGUE / EVASION counters, the action-point readout,
// the six red buttons with their captions, and the black message screen on the left.
#include "assets/gui.h"
#include "assets/stf.h"
#include "common/gmodel.h"
#include "common/uidraw.h"
#include <deque>
#include <functional>
#include <memory>

struct BarState {
    std::string playerName = "PLAYER";
    int hp = 100, maxHp = 100, fatigue = 100, maxFatigue = 100, evasion = 0, ap = 8;
    const std::deque<std::string>* log = nullptr;                 // message log (newest last)
    std::string pressed;                                          // button being held down ("" none)
    std::string hover;
    std::string weaponIcon[2], weaponMode[2], weaponApText[2];     // per hand: action icon stem, mode caption (SINGLE / MELEE / UNARMED / RELOAD), cost caption ("4AP")
    std::string armed;                                            // weapon button waiting for a target click (drawn pressed)
    bool endTurnEnabled = false;                                  // the End Turn button (the former Sneak button) is only live in combat
    int apMax = 8; float apFloat = 8;                             // action point pips                     // what is held in each hand (action icon stem / display name; "" = empty)
};

class ActionBar {
public:
    bool load(const std::string& assets, const std::string& gameDir, gm::TexCache& tc, const vb::StringTable* stf) {
        try { gui_ = vb::loadGui(assets + "/Interface/Actionbar.gui"); } catch (std::exception& e) { std::fprintf(stderr, "Actionbar.gui: %s\n", e.what()); return false; }
        tc_ = &tc; stf_ = stf; fontDir_ = gameDir + "/Fonts"; ok_ = !gui_.windows.empty(); return ok_;
    }
    bool ok() const { return ok_; }
    // scale and top-left of the bar in window pixels
    void layout(int dw, int dh, float& S, float& x0, float& y0) const {
        S = std::max(1.f, (float)dh / 768.f); if (1024.f * S > (float)dw) S = (float)dw / 1024.f;
        x0 = ((float)dw - 1024.f * S) * 0.5f; y0 = (float)dh - 164.f * S;
    }
    float height(int dw, int dh) const { float S, x, y; layout(dw, dh, S, x, y); return 164.f * S; }
    bool over(float mx, float my, int dw, int dh) const { float S, x0, y0; layout(dw, dh, S, x0, y0); return mx >= x0 && mx < x0 + 1024.f * S && my >= y0; }
    // the button under the mouse ("btnInventory" ...), "" if none
    std::string buttonAt(float mx, float my, int dw, int dh) const {
        if (!ok_) return ""; float S, x0, y0; layout(dw, dh, S, x0, y0);
        std::string r; hit(*bg(), x0, y0, S, mx, my, r); return r;
    }
    void draw(UiDraw& ui, const BarState& st, int dw, int dh) {
        if (!ok_) return; float S, x0, y0; layout(dw, dh, S, x0, y0);
        const vb::GuiWidget* b = bg(); if (!b) return;
        drawNode(ui, *b, x0, y0, S, st);
        drawLog(ui, st, x0, y0, S);
    }
    TtFont* font(const std::string& face, float px) {
        std::string file = face == "Fallouty" ? "r_fallouty.ttf" : "TT0807M_.TTF";            // "Arial" is not shipped: the condensed Gothic that the tech demo ships stands in
        int ipx = (int)std::round(px); std::string key = file + "@" + std::to_string(ipx);
        auto it = fonts_.find(key); if (it != fonts_.end()) return it->second.get();
        auto f = std::make_unique<TtFont>(); if (!f->load(fontDir_ + "/" + file, (float)ipx)) f.reset();
        return (fonts_[key] = std::move(f)).get();
    }
private:
    vb::GuiFile gui_; gm::TexCache* tc_ = nullptr; const vb::StringTable* stf_ = nullptr; std::string fontDir_; bool ok_ = false;
    std::map<std::string, std::unique_ptr<TtFont>> fonts_;
    const vb::GuiWidget& root() const { return *gui_.windows[0]; }
    const vb::GuiWidget* bg() const { return root().find("picBackground"); }

    static bool isSkin(const std::string& t) { return t.rfind("zzmain", 0) == 0; }
    void hit(const vb::GuiWidget& w, float ox, float oy, float S, float mx, float my, std::string& out) const {
        for (auto& c : w.children) {
            float x = ox + (float)c->rect.x0 * S, y = oy + (float)c->rect.y0 * S;
            if (c->type == vb::GuiType::Button && mx >= x && mx < x + c->rect.w() * S && my >= y && my < y + c->rect.h() * S) out = c->name;
            hit(*c, x, y, S, mx, my, out);
        }
    }
    void drawText(UiDraw& ui, const vb::GuiWidget& w, const std::string& text, float x, float y, float S, bool gray = false) {
        if (text.empty()) return;
        TtFont* f = font(w.font, std::max(8.f, (float)w.fontSize * S * 1.25f)); if (!f) return;
        float tw = f->width(text), bw = (float)w.rect.w() * S, bh = (float)w.rect.h() * S, tx = x;
        if (w.align == 1) tx = x + bw - tw; else if (w.align == 2) tx = x + (bw - tw) * 0.5f;
        float ty = y + (bh - f->lineH) * 0.5f;
        Rgba c = argb(w.textColor); c.a = 1.f; c.r = std::min(1.f, c.r * 1.7f); c.g = std::min(1.f, c.g * 1.7f); c.b = std::min(1.f, c.b * 1.7f);
        if (gray) c = {0.5f, 0.5f, 0.5f, 1.f};
        f->draw(ui, std::round(tx), std::round(ty), text, c);
    }
    void drawWidget(UiDraw& ui, const vb::GuiWidget& w, float ox, float oy, float S, const BarState& st) { for (auto& c : w.children) drawNode(ui, *c, ox, oy, S, st); }
    // draw one widget whose parent's origin is at (ox, oy) on screen
    void drawNode(UiDraw& ui, const vb::GuiWidget& c, float ox, float oy, float S, const BarState& st) {
        float x = ox + (float)c.rect.x0 * S, y = oy + (float)c.rect.y0 * S, cw = (float)c.rect.w() * S, ch = (float)c.rect.h() * S;
        const std::string& n = c.name;
        if (c.type == vb::GuiType::Picture) {
            if (n.rfind("picPlayer", 0) == 0 && n != "picPlayer00") return;                        // single-character party for now
            if (n.find("Mask") != std::string::npos || n.find("CalledShot") != std::string::npos || n.find("ReloadIcon") != std::string::npos) return;
            if (n == "picAPIcon") {                                                                   // one pip per action point: green while available, grey once spent
                for (int i = 0; i < st.apMax && i < 29; i++) { gm::TexInfo t = tc_->get(i < (int)std::floor(st.apFloat + 1e-3f) ? "f3_actionbar_APfine_green" : "f3_actionbar_APfine_grey", nullptr, true); if (t.id) ui.image(t.id, x + (float)i * 7.f * S, y, cw, ch); }
                return;
            }
            if (n.find("Digit") != std::string::npos) {                                              // winHealthDigit1 / 10 / 100 ...: one decimal digit of the counter value
                int value = n.rfind("winHealth", 0) == 0 ? st.hp : n.rfind("winFatigue", 0) == 0 ? st.fatigue : st.evasion;
                int place = n.size() > 3 && n.compare(n.size() - 3, 3, "100") == 0 ? 100 : n.compare(n.size() - 2, 2, "10") == 0 ? 10 : 1;
                gm::TexInfo t = tc_->get("f3_digit_" + std::to_string(std::max(0, std::min(999, value)) / place % 10), nullptr, true); if (std::getenv("VB_DEBUG_UI")) std::fprintf(stderr, "digit %s at %.0f,%.0f %.0fx%.0f tex %u\n", n.c_str(), x, y, cw, ch, t.id); if (t.id) ui.image(t.id, x, y, cw, ch);
                return;
            }
            std::string img = c.image();
            if (n.rfind("picWeapon0", 0) == 0 && n.find("Icon") != std::string::npos && n.size() > 11) {      // picWeapon01Icon_up / _down: the held weapon's picture
                int hand = n[10] - '1'; if (hand >= 0 && hand < 2 && !st.weaponIcon[hand].empty()) { gm::TexInfo t = tc_->get(st.weaponIcon[hand], nullptr, true); if (t.id) { float iw = std::min(cw, ch * 2.f), ih = iw * 0.5f; ui.image(t.id, x + (cw - iw) * 0.5f, y + (ch - ih) * 0.5f, iw, ih); } }
                drawWidget(ui, c, x, y, S, st); return;
            }
            if (!img.empty() && !isSkin(img)) {
                gm::TexInfo t = tc_->get(img, nullptr, true);
                if (t.id) {
                    float fw = cw;
                    if (n == "picHealth") fw = cw * std::max(0.f, std::min(1.f, (float)st.hp / (float)std::max(1, st.maxHp)));
                    else if (n == "picFatigue") fw = cw * std::max(0.f, std::min(1.f, (float)st.fatigue / (float)std::max(1, st.maxFatigue)));
                    ui.image(t.id, x, y, fw, ch);
                }
            }
            drawWidget(ui, c, x, y, S, st);
        } else if (c.type == vb::GuiType::Label) {
            if (n.rfind("lblWeapon0", 0) == 0 && n.find("Mode") != std::string::npos && n.size() > 11) { int hand = n[10] - '1'; if (hand >= 0 && hand < 2 && !st.weaponMode[hand].empty()) { drawText(ui, c, st.weaponMode[hand], x, y, S); return; } }
            if (n.rfind("lblWeapon0", 0) == 0 && n.find("AP") != std::string::npos && n.size() > 11) { int hand = n[10] - '1'; if (hand >= 0 && hand < 2 && !st.weaponApText[hand].empty()) { drawText(ui, c, st.weaponApText[hand], x, y, S); return; } }
            std::string text = n == "lblName" ? st.playerName : n == "lblActionPointsValue" ? std::to_string(st.ap) : stf_ ? stf_->text(c.textId) : "";
            drawText(ui, c, text, x, y, S);
        } else if (c.type == vb::GuiType::Button) {
            bool down = st.pressed == n || st.armed == n;                       // a button holds an up picture and a down picture (each with its own icon / labels) plus caption labels
            for (auto& k : c.children) if (k->type == vb::GuiType::Picture && (k->name == "btnUp" || k->name == "btnDown") && ((k->name == "btnDown") == down)) drawNode(ui, *k, x, y, S, st);
            if (n == "btnSneak") {                                                    // End Turn: grayed out unless in combat
                for (auto& k : c.children) if (k->type == vb::GuiType::Label) drawText(ui, *k, "End Turn", x + (float)k->rect.x0 * S, y + (float)k->rect.y0 * S, S, !st.endTurnEnabled);
                if (!st.endTurnEnabled) ui.solid(x, y, cw, ch, {0.f, 0.f, 0.f, 0.5f});
            } else
            for (auto& k : c.children) if (k->type == vb::GuiType::Label) drawNode(ui, *k, x, y, S, st);
        } else drawWidget(ui, c, x, y, S, st);
    }
    void drawLog(UiDraw& ui, const BarState& st, float x0, float y0, float S) {
        if (!st.log) return;
        TtFont* f = font("Arial", 13.f * S * 1.25f); if (!f) return;
        float lx = x0 + 14.f * S, ly = y0 + 18.f * S, lw = 274.f * S, lh = 120.f * S, lineH = f->lineH;
        std::vector<std::string> lines;                                                          // newest at the bottom, word-wrapped to the screen width
        for (const std::string& m : *st.log) {
            std::string line, word;
            for (size_t i = 0; i <= m.size(); i++) {
                if (i == m.size() || m[i] == ' ') { if (!word.empty()) { if (!line.empty() && f->width(line + " " + word) > lw) { lines.push_back(line); line = word; } else line += (line.empty() ? "" : " ") + word; word.clear(); } }
                else word.push_back(m[i]);
            }
            if (!line.empty()) lines.push_back(line);
        }
        int maxLines = (int)(lh / lineH); int first = std::max(0, (int)lines.size() - maxLines);
        for (int i = first; i < (int)lines.size(); i++) f->draw(ui, lx, ly + (float)(i - first) * lineH, lines[(size_t)i], {0.45f, 0.95f, 0.45f, 1.f});
    }
};

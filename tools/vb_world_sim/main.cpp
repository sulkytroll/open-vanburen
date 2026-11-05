// Headless test of the game world runtime: loads a map, runs every entity script for a number of seconds and prints what the scripts did.
//   vb_world_sim [mapname] [-t seconds] [-game DIR] [-assets DIR] [-talk NAME] [-v]
#include "game/world.h"
#include "assets/col.h"
#include "assets/tre.h"
#include "common/gamedir.h"
#include "common/nav.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <map>
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    std::string name = "03_Tutorial_Junktown", assets, game, talk, useName;
    float delay = 0, secs = 30; bool useAtEnd = false, verbose = false, alltalk = false; float gx = 0, gz = 0; bool haveGoto = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-t" && i + 1 < argc) secs = (float)std::atof(argv[++i]);
        else if (a == "-assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "-game" && i + 1 < argc) game = argv[++i];
        else if (a == "-talk" && i + 1 < argc) talk = argv[++i];
        else if (a == "-v") verbose = true;
        else if (a == "-delay" && i + 1 < argc) delay = (float)std::atof(argv[++i]);
        else if (a == "-use" && i + 1 < argc) useName = argv[++i];
        else if (a == "-useend") useAtEnd = true;
        else if (a == "-alltalk") alltalk = true;
        else if (a == "-goto" && i + 2 < argc) { gx = (float)std::atof(argv[++i]); gz = (float)std::atof(argv[++i]); haveGoto = true; }
        else if (a[0] != '-') name = a;
    }
    if (assets.empty()) { if (!findAndMountGame(game, game)) return 1; assets = game; }       // (-assets DIR -game DIR reads an extracted copy instead)
    else if (game.empty()) game = (fs::path(assets).parent_path() / "Fallout 3 Van Buren Tech Demo").string();
    std::string dir = assets + "/Maps/"; if (!vb::vfs::exists(dir + name + ".tre")) dir = assets + "/Engine/";
    std::string stem, mapPath;
    for (const std::string& fn : vb::vfs::list(dir)) { if (fs::path(fn).extension() == ".map" && fn.size() >= name.size() + 4 && fn.compare(fn.size() - name.size() - 4, name.size(), name) == 0) { mapPath = dir + fn; stem = fs::path(fn).stem().string(); } }
    if (mapPath.empty()) { std::fprintf(stderr, "no map %s\n", name.c_str()); return 1; }
    vb::Map map = vb::loadMap(mapPath);
    vb::Tre tre = vb::loadTre(dir + name + ".tre"); vb::Col col = vb::loadCol(dir + name + ".col");
    NavGrid nav; nav.build(col, tre.min[0], tre.min[2]);
    vb::World w(assets, game); w.autoEndPlayerTurn = true;
    w.host.findPath = [&](float sx, float sz, float gx, float gz, std::vector<std::pair<float, float>>& p) { return nav.findPath(sx, sz, gx, gz, p); };
    w.host.lineOfSight = [&](float x0, float z0, float x1, float z1) { std::vector<uint8_t> g(nav.walk); return nav.lineClear(x0, z0, x1, z1, nav.walk); };
    w.host.walkable = [&](float x, float z) { return nav.walkableAt(x, z); };
    float px = 95.1f, pz = 273.9f; if (!map.entries.empty()) { px = map.entries[0].pos[0][0]; pz = map.entries[0].pos[0][2]; }
    for (size_t i = 0; i < map.entities.size(); i++) { std::string f = map.entities[i].file; for (auto& c : f) c = (char)std::tolower((unsigned char)c); if (f == "default_pc.crt") { px = map.entities[i].pos[0]; pz = map.entities[i].pos[2]; map.entities.erase(map.entities.begin() + (long)i); break; } }
    w.load(map, name, stem, px, 0, pz, 3.14159f);
    std::printf("map %s (%s): %zu world entities, player at %.1f %.1f\n", name.c_str(), stem.c_str(), w.count(), px, pz);
    static const char* kinds[] = {"player", "critter", "door", "container", "item", "prop", "trigger", "mapscript"};
    int scripted = 0; for (size_t id = 1; id <= w.count(); id++) { vb::Entity* e = w.find((int)id); if (e->vm) scripted++; if (verbose || e->kind == vb::EntKind::Critter || e->kind == vb::EntKind::Door || e->kind == vb::EntKind::Container || e->kind == vb::EntKind::Trigger || e->kind == vb::EntKind::MapScript) std::printf("  #%-3d %-9s %-40s script=%-40s dlg=%s pos %.1f %.1f team %d\n", e->id, kinds[(int)e->kind], e->instance.c_str(), e->scriptName.c_str(), e->dialogue.c_str(), e->x, e->z, e->team); }
    std::printf("%d scripted entities\n", scripted);
    if (verbose) for (auto& b : w.bag) std::printf("  start bag: %s x%d (%s) <- %s\n", w.itemInfo(b.file).name.c_str(), b.count, w.itemInfo(b.file).kind.c_str(), b.file.c_str());
    auto doUse = [&]() {
    if (!useName.empty()) for (size_t id = 1; id <= w.count(); id++) if (w.find((int)id)->instance == useName) {
        std::printf("USE %s: %s\n", useName.c_str(), w.use((int)id).c_str());
        if (w.openContainer) { for (auto& c : w.contents(w.openContainer)) std::printf("  holds: %s\n", c.c_str()); std::printf("  took %d item(s)\n", w.takeAll(w.openContainer)); w.openContainer = 0; for (auto& l : w.log) std::printf("  log: %s\n", l.c_str());
            for (auto& b : w.bag) std::printf("  bag: %s x%d (%s)\n", w.itemInfo(b.file).name.c_str(), b.count, w.itemInfo(b.file).kind.c_str()); }
    }
    };
    if (!useAtEnd) doUse();
    size_t logged = 0, floatsSeen = 0;
    std::vector<std::pair<float, float>> ppath; size_t pnext = 0;
    if (haveGoto) nav.findPath(px, pz, gx, gz, ppath);
    for (float t = 0; t < secs; t += 1.f / 30.f) {
        if (t >= delay && pnext < ppath.size()) {                      // the player walks the path at 5.4 units/s
            vb::Entity* pc = w.player(); float tx = ppath[pnext].first - pc->x, tz = ppath[pnext].second - pc->z, d = std::hypot(tx, tz), mv = 5.4f / 30.f;
            if (mv >= d) { pc->x = ppath[pnext].first; pc->z = ppath[pnext].second; pnext++; } else { pc->x += tx / d * mv; pc->z += tz / d * mv; pc->yaw = std::atan2(tx, tz); }
        }
        w.update(1.f / 30.f);
        if (w.hasMapChange) { std::printf("[%.1fs] MAP CHANGE -> %s entry %d\n", t, w.mapChange.map.c_str(), w.mapChange.entry); w.hasMapChange = false; }
        for (; logged < w.log.size(); logged++) std::printf("[%.1fs] LOG: %s\n", t, w.log[logged].c_str());
        for (auto& f : w.floats) if (f.t < 0.04f) std::printf("[%.1fs] SPEECH/TEXT #%d (%s): %s\n", t, f.ent, w.find(f.ent)->instance.c_str(), f.text.c_str());
        (void)floatsSeen;
    }
    if (w.dialogue().active) { std::printf("-- a dialogue is active with %s:\n", w.find(w.dialogue().npc)->display.c_str()); for (auto& t : w.dialogue().npcText) std::printf("   NPC: %s\n", t.c_str()); for (auto& r : w.dialogue().replies) std::printf("   > %s\n", r.text.c_str()); }
    if (useAtEnd) doUse();
    std::printf("-- after %.0fs: VM errors %d, player at %.1f %.1f (path %zu pts, reached %zu)\n", secs, w.vmErrors, w.player()->x, w.player()->z, ppath.size(), pnext);
    for (auto& d : w.debug) std::printf("DEBUG: %s\n", d.c_str());
    for (size_t id = 1; id <= w.count(); id++) { vb::Entity* e = w.find((int)id); if (e->kind == vb::EntKind::Critter) std::printf("  #%-3d %-36s pos %.1f %.1f yaw %.2f anim %s hp %d/%d dead %d active %d\n", e->id, e->instance.c_str(), e->x, e->z, e->yaw, e->anim.c_str(), e->hp, e->maxHp, e->dead, e->active); }
    if (const char* gv = std::getenv("VB_GLOBALS")) { for (size_t id = 1; id <= w.count(); id++) { vb::Entity* e = w.find((int)id); if (e->instance == std::string(gv)) { { int32_t v = 0; e->vm->cellAt(7872, v); std::printf("  [7872]=%d now=%d [1360]=", v, w.timeMs); e->vm->cellAt(1360, v); std::printf("%d [8808]=", v); e->vm->cellAt(8808, v); std::printf("%d\n", v); } for (const char* n : {"myState", "myLastState", "bIntroJunktown", "bSawVaultDoor", "bDoneTalking"}) { int32_t a = e->vm->global(n), v = 0; if (a >= 0) e->vm->cellAt(a, v); std::printf("  %s = %d\n", n, v); } } } }
    std::map<std::string, int> unb; for (size_t id = 1; id <= w.count(); id++) { vb::Entity* e = w.find((int)id); if (e->vm) for (auto& u : e->vm->unbound) unb[u.first] += u.second; }
    for (auto& u : unb) std::printf("UNBOUND native %s x%d\n", u.first.c_str(), u.second);
    if (alltalk) {         // random walks through every dialogue script
        std::map<std::string, int> unb; int ok = 0, fail = 0, steps = 0;
        std::vector<std::string> names = w.dialogueScripts(); std::sort(names.begin(), names.end());
        vb::Entity* npc = nullptr; for (size_t id = 1; id <= w.count(); id++) if (w.find((int)id)->kind == vb::EntKind::Critter) { npc = w.find((int)id); break; }
        if (!npc) { std::printf("no critter to host dialogues\n"); return 1; }
        for (const std::string& n : names) {
            npc->dialogue = n; w.endDialogue();
            if (!w.startDialogue(npc->id)) { std::printf("  %-28s did not start\n", n.c_str()); fail++; continue; }
            int turns = 0, firstText = 0;
            for (; turns < 60 && w.dialogue().active; turns++) {
                firstText += (int)w.dialogue().npcText.size(); steps++;
                if (w.dialogue().replies.empty()) break;
                w.chooseReply((int)(rand() % w.dialogue().replies.size()));
            }
            if (const vb::AmxVM* vm = w.dialogueVm()) for (auto& u : vm->unbound) unb[u.first] += u.second;
            std::printf("  %-28s ok  %2d turns, %d NPC lines\n", n.c_str(), turns, firstText); ok++; w.endDialogue();
        }
        std::printf("dialogues: %d ran, %d failed, %d steps, VM errors %d\n", ok, fail, steps, w.vmErrors);
        for (auto& u : unb) std::printf("UNBOUND (dialogue) %s x%d\n", u.first.c_str(), u.second);
        for (auto& d : w.debug) std::printf("DEBUG: %s\n", d.c_str());
        return 0;
    }
    if (!talk.empty()) {
        for (size_t id = 1; id <= w.count(); id++) { vb::Entity* e = w.find((int)id); if (e->instance == talk || e->display == talk) {
            std::printf("== dialogue with %s\n", e->display.c_str());
            if (!w.startDialogue(e->id)) { std::printf("(no dialogue)\n"); break; }
            for (int step = 0; step < 6 && w.dialogue().active; step++) {
                for (auto& t : w.dialogue().npcText) std::printf("  NPC: %s\n", t.c_str());
                for (size_t r = 0; r < w.dialogue().replies.size(); r++) std::printf("   [%zu] %s\n", r, w.dialogue().replies[r].text.empty() ? "(continue)" : w.dialogue().replies[r].text.c_str());
                if (w.dialogue().replies.empty()) { w.endDialogue(); break; }
                w.chooseReply(0);
            }
            break; } }
    }
}

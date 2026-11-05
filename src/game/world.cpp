#include "assets/vfs.h"
#include <set>
#include "game/world.h"
#include "assets/entity.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace vb {

namespace {
const float kPi = 3.14159265f;
std::mt19937& rng() { static std::mt19937 r(12345); return r; }
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
float asF(int32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
int32_t asI(float f) { int32_t v; std::memcpy(&v, &f, 4); return v; }
bool readVec(AmxVM& vm, int32_t addr, float* v) { for (int i = 0; i < 3; i++) { int32_t c; if (!vm.cellAt(addr + 4 * i, c)) return false; v[i] = asF(c); } return true; }
void writeVec(AmxVM& vm, int32_t addr, float x, float y, float z) { vm.setCell(addr, asI(x)); vm.setCell(addr + 4, asI(y)); vm.setCell(addr + 8, asI(z)); }
bool pointInPoly(const std::vector<float>& p, float x, float z) {
    bool in = false; size_t n = p.size() / 2;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        float xi = p[i * 2], zi = p[i * 2 + 1], xj = p[j * 2], zj = p[j * 2 + 1];
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi + 1e-12f) + xi)) in = !in;
    }
    return in;
}
std::string stripMarkup(std::string s) {      // "{c255}text" colour prefixes
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '{') { size_t e = s.find('}', i); if (e != std::string::npos && e - i < 16) { i = e; continue; } }
        out.push_back(s[i]);
    }
    return out;
}
} // namespace

int World::degOf(float dx, float dz) {
    int d = (int)std::lround(std::atan2(dx, dz) * 180.f / kPi);
    return ((d % 360) + 360) % 360;
}

std::string World::text(int strref) const {
    std::string s = stripMarkup(stf.text((uint32_t)strref));
    for (size_t i = 0; (i = s.find('<', i)) != std::string::npos;) {
        size_t e = s.find('>', i); if (e == std::string::npos || e - i > 24) break;
        std::string name = s.substr(i + 1, e - i - 1), rep;
        if (name == "CHARNAME") rep = playerName;
        else if (auto it = tokens_.find(name); it != tokens_.end()) rep = stringRef(it->second);
        else if (name.compare(0, 3, "GV_") == 0) rep = "";                                // a global-variable token (e.g. <GV_Jake> after the vault generator message): no value known, shown as nothing
        else if (name == "CDAMN") rep = "Damn";
        else if (name == "DDAMMIT") rep = "Dammit";
        else { rep = name; if (std::islower((unsigned char)rep[0]) || name == "ASS") { /* language-filter word: the word itself */ } }
        s.replace(i, e - i + 1, rep); i += rep.size();
    }
    return s;
}

World::World(const std::string& assetsDir, const std::string& gameDir) : assets_(assetsDir), game_(gameDir) {
    scriptsDir_ = assets_ + "/Scripts";
    for (const std::string& fn : vfs::list(scriptsDir_)) {
        fs::path fp(fn); std::string ext = lower(fp.extension().string());
        if (ext == ".scr" || ext == ".sma") scriptIndex_[lower(fp.stem().string()) + ext] = scriptsDir_ + "/" + fn;
    }
    std::string err; stf.load(game_ + "/English.stf", err);
    { vfs::Stream f(assets_ + "/Engine/tokens.txt"); std::string line; while (std::getline(f, line)) { size_t eq = line.find('='); if (eq != std::string::npos) tokens_[line.substr(0, eq)] = std::atoi(line.c_str() + eq + 1); } }
    // global variable defaults (Scripts/globals.ini: `name=value ;comment` lines)
    for (const char* fn : {"globals.ini", "locals.ini"}) {
        vfs::Stream f(scriptsDir_ + "/" + fn); std::string line;
        while (std::getline(f, line)) {
            size_t c = line.find(';'); if (c != std::string::npos) line.resize(c);
            size_t eq = line.find('='); if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            auto trim = [](std::string& s) { while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back(); size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) i++; s = s.substr(i); };
            trim(k); trim(v); if (!k.empty()) globals[k] = std::atoi(v.c_str());
        }
    }
}

std::shared_ptr<AmxScript> World::script(const std::string& stem, const char* ext) {
    std::string key = lower(stem) + ext;
    auto it = scripts_.find(key); if (it != scripts_.end()) return it->second;
    auto p = scriptIndex_.find(key); std::shared_ptr<AmxScript> s;
    if (p != scriptIndex_.end()) { std::string err; s = AmxScript::load(p->second, err); if (!s) debug.push_back("script load failed: " + p->second + ": " + err); }
    return scripts_[key] = s;
}

void World::attachScript(Entity& e, const std::string& stemIn) {
    std::string stem = stemIn; if (stem.size() > 4 && lower(stem.substr(stem.size() - 4)) == ".amx") stem.resize(stem.size() - 4);
    if (stem.empty()) return;
    e.script = script(stem, ".scr");
    if (!e.script) { if (e.kind != EntKind::MapScript && stem != "<No Script>") debug.push_back("no compiled script for '" + stem + "' (" + e.instance + ")"); return; }
    e.scriptName = stem;
    e.vm = std::make_unique<AmxVM>(e.script); e.vm->user = &e;
    bindNatives(*e.vm, false);
}

void World::resetGame() {
    bag.clear(); for (std::string& q : equipped) q.clear(); journal.clear(); globals.clear(); log.clear(); floats.clear(); timeMs = 0; starterGiven_ = false;
}

void World::load(const Map& map, const std::string& name, const std::string& mapFileStem, float px, float py, float pz, float pyaw) {
    ents_.clear(); byMap_.clear(); msgs_.clear(); vegs.clear(); mapEffects.clear(); floats.clear(); lists_.clear(); hasMapChange = false; endDialogue();
    cs_ = CombatState(); impacts_.clear(); attackHand_ = 0;
    map_ = &map; mapName = name;      // (game time keeps running across maps)
    if (!elib_) elib_ = new EntityLibrary(assets_);
    EntityLibrary* elib = (EntityLibrary*)elib_;
    if (!starterGiven_) { starterGiven_ = true; giveStarterKit(); }
    Entity pc; pc.id = 1; pc.kind = EntKind::Player; pc.display = "Protagonist"; pc.instance = "Protagonist"; pc.x = px; pc.y = py; pc.z = pz; pc.yaw = pyaw; pc.hp = pc.maxHp = 100; pc.team = 1;
    if (const EntityTemplate* pt = elib->tmpl("Default_PC.CRT")) if (pt->hasStats) { for (int k = 0; k < 7; k++) pc.special[k] = pt->special[k]; pc.hp = pc.maxHp = std::max(1, pt->maxHp); }
    ents_.push_back(std::move(pc));
    // the map's own script (named like the .map file)
    { Entity m; m.id = (int)ents_.size() + 1; m.kind = EntKind::MapScript; m.instance = mapFileStem; m.display = mapFileStem; ents_.push_back(std::move(m)); attachScript(ents_.back(), mapFileStem); }
    byMap_.assign(map.entities.size(), 0);
    for (size_t i = 0; i < map.entities.size(); i++) {
        const MapEntity& me = map.entities[i];
        Entity e; e.id = (int)ents_.size() + 1; e.file = me.file; e.instance = me.instance; e.display = me.display; e.mapIdx = (int)i;
        e.x = me.pos[0]; e.y = me.pos[1]; e.z = me.pos[2]; e.yaw = me.rot[0];
        const EntityTemplate* t = elib->tmpl(me.file);
        std::string kind = t ? t->kind : "", model = t ? lower(t->model) : "";
        if (kind == "door") e.kind = EntKind::Door;
        else if (kind == "con") e.kind = EntKind::Container;
        else if (kind == "use") e.kind = EntKind::Usable;
        else if (kind == "item" || kind == "weapon" || kind == "armor" || kind == "ammo") e.kind = EntKind::Item;
        else if (kind == "critter" && (model.empty() || (t && !t->portrait.empty()) || model.rfind("hum", 0) == 0 || model.rfind("cr_", 0) == 0)) e.kind = EntKind::Critter;
        else e.kind = EntKind::Prop;
        if (t && t->hasStats) {                         // GCRE: SPECIAL, race, gender, team; GENT: hit points
            for (int k = 0; k < 7; k++) e.special[k] = t->special[k];
            e.race = t->race; e.gender = t->gender; e.team = t->team;
        }
        if (t && t->maxHp > 0) { e.maxHp = std::max(1, t->maxHp); e.hp = t->race == 5 ? std::min(e.maxHp, std::max(1, t->hp)) : e.maxHp; }       // the template's current hit points only count for robots (the broken eye bot: 1 of 10); a few people carry 1 of 10 as an unfinished default
        else e.hp = e.maxHp = (e.kind == EntKind::Critter) ? 40 : 20;
        if (e.kind == EntKind::Critter && t && t->race == 5 && e.hp < e.maxHp) { e.brokenRobot = true; e.anim = "lying"; }       // a broken robot (the eye bot, 1 of 10 hit points) lies on the floor until it is repaired
        float gy = e.y; if (host.groundY && e.kind == EntKind::Critter && host.groundY(e.x, e.z, gy)) e.y = gy;
        std::string scr = me.script.empty() && t ? t->defaults.script : me.script;
        if (!me.display.empty() && scriptIndex_.count(lower(me.display) + ".sma")) e.dialogue = me.display;
        for (const std::string& it : me.items) if (!it.empty() && !(it.size() > 4 && lower(it.substr(it.size() - 4)) == ".veg")) e.cargo.push_back(it);
        for (const std::string& it : me.items) if (it.size() > 4 && lower(it.substr(it.size() - 4)) == ".veg") { Veg g; g.id = vegSerial_++; g.ent = e.id; g.name = it; vegs.push_back(g); }       // effects the map attaches to an entity (the generator's smoke)
        if (e.kind == EntKind::Critter) for (const std::string& f : e.cargo) { const EntityTemplate* wt = elib->tmpl(f); if (wt && wt->kind == "weapon") { e.weapon = f; break; } }
        byMap_[i] = e.id; ents_.push_back(std::move(e));
        Entity& ref = ents_.back();
        attachScript(ref, scr);
        if (ref.vm) for (const auto& v : me.vars) {                  // preloaded script variables (public variables, `p_` prefix)
            for (const AmxEntry& pv : ref.script->pubvars) if (pv.name == v.first || pv.name == "p_" + v.first) ref.vm->setCell((int32_t)pv.address, (int32_t)v.second);
        }
    }
    for (const MapTrigger& tr : map.triggers) {
        if (tr.type > 1 || tr.verts.size() < 9) continue;                  // boundary zones (type 6) are handled by area queries later
        Entity z; z.id = (int)ents_.size() + 1; z.kind = EntKind::Trigger; z.zoneKind = (int)tr.type; z.zoneTarget = tr.target; z.zoneEntry = tr.entry; z.zoneId = (int)tr.id;
        z.instance = tr.target; for (size_t i = 0; i + 2 < tr.verts.size(); i += 3) { z.zone.push_back(tr.verts[i]); z.zone.push_back(tr.verts[i + 2]); }
        ents_.push_back(std::move(z));
        if (tr.type == 1) attachScript(ents_.back(), tr.target);
    }
    // create the scripted entities
    for (Entity& e : ents_) if (e.vm) { int32_t r; run(e, "OnCreate", {timeMs}, r); }
}

void World::setPlayer(float x, float y, float z, float yaw) { Entity& p = ents_[0]; p.x = x; p.y = y; p.z = z; p.yaw = yaw; }

bool World::run(Entity& e, const std::string& fn, const std::vector<int32_t>& args, int32_t& ret) {
    if (!e.vm || e.vm->script->findPublic(fn) < 0) return false;
    e.vm->user = &e;
    if (!e.vm->call(fn, args, ret, 1500000)) {
        vmErrors++;
        if (debug.size() < 200) debug.push_back("VM error in " + e.scriptName + "::" + fn + " (" + e.instance + "): " + e.vm->errorText());
        return false;
    }
    return true;
}
bool World::callEntity(Entity& e, const std::string& fn, const std::vector<int32_t>& args, int32_t& ret) { return run(e, fn, args, ret); }

void World::damage(Entity& t, int amount, int source) {
    if (t.dead) return;
    int32_t r;
    if (amount > 0) run(t, "OnDamage", {timeMs, source, amount, 0}, r);        // the handler sees the HP from before the hit: the scripts compare the amount with GetCurrentHP to pick the "and die" voice
    if (t.dead) return;
    t.hp = std::max(0, std::min(t.maxHp, t.hp - amount));
    if (amount > 0 && t.hp > 0) t.hitT = 0.5f;
    if (t.hp <= 0 && !t.dead) {
        t.dead = true; t.moving = false; t.queue.clear(); t.hasCur = false; t.anim = "death";
        run(t, "OnDeath", {timeMs}, r);
    }
}

void World::setDoor(Entity& e, bool open) {
    if (e.open == open) return;
    e.open = open; if (host.doorToggled) host.doorToggled(e, open);
}

void World::say(Entity& e, int strref, float seconds, unsigned rgb) {
    std::string txt = strref > 0 ? text(strref) : "";
    if (txt.empty()) return;
    FloatText f; f.ent = e.id; f.text = txt; f.rgb = rgb; f.ttl = seconds > 0.f ? seconds : 3.f + 0.05f * (float)txt.size(); f.speech = true; f.x = e.x; f.y = e.y; f.z = e.z; f.dy = e.brokenRobot ? 2.5f : e.race == 5 ? 5.f : 8.5f;
    floats.push_back(f);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------ natives
void World::bindNatives(AmxVM& vm, bool dialogueVm) {
    (void)dialogueVm;
    World* W = this;
    static const char* traceName = std::getenv("VB_TRACE");        // VB_TRACE=<instance name>: print every native call of that entity (debugging scripts)
    auto B = [&](const char* nm, AmxVM::Native fn) {
        if (!traceName) { vm.bind(nm, std::move(fn)); return; }
        std::string name = nm;
        vm.bind(nm, [fn, name, W](AmxVM& v, const int32_t* a, int n) {
            int32_t r = fn(v, a, n); Entity* me = (Entity*)v.user;
            if (me && (std::string(traceName) == "*" || me->instance == traceName) && name != "Myself" && name.rfind("float", 0) != 0) { std::string line = "[" + std::to_string(W->timeMs) + "] " + me->instance + " " + name + "("; for (int i = 0; i < n; i++) line += (i ? ", " : "") + std::to_string(a[i]); line += ") = " + std::to_string(r); std::fprintf(stderr, "%s\n", line.c_str()); }
            return r; });
    };
    auto E = [W](int32_t id) { return W->find(id); };
    using A = const int32_t*;

    // ---- Pawn float library
    B("float", [](AmxVM&, A a, int) { return asI((float)a[0]); });
    B("floatadd", [](AmxVM&, A a, int) { return asI(asF(a[0]) + asF(a[1])); });
    B("floatsub", [](AmxVM&, A a, int) { return asI(asF(a[0]) - asF(a[1])); });
    B("floatmul", [](AmxVM&, A a, int) { return asI(asF(a[0]) * asF(a[1])); });
    B("floatdiv", [](AmxVM&, A a, int) { float d = asF(a[1]); return asI(d == 0.f ? 0.f : asF(a[0]) / d); });
    B("floatfract", [](AmxVM&, A a, int) { float f = asF(a[0]); return asI(f - std::trunc(f)); });
    B("floatcmp", [](AmxVM&, A a, int) { float x = asF(a[0]), y = asF(a[1]); return x < y ? -1 : x > y ? 1 : 0; });
    B("floatround", [](AmxVM&, A a, int n) { float f = asF(a[0]); int m = n > 1 ? a[1] : 0; return (int32_t)(m == 1 ? std::floor(f) : m == 2 ? std::ceil(f) : m == 3 ? std::trunc(f) : std::floor(f + 0.5f)); });

    // ---- identity, time, random, globals
    B("Myself", [](AmxVM& v, A, int) { Entity* e = (Entity*)v.user; return e ? e->id : 0; });
    B("Protagonist", [](AmxVM&, A, int) { return 1; });
    B("PCSpeaker", [](AmxVM&, A, int) { return 1; });
    B("GetTimeStamp", [W](AmxVM&, A, int) { return W->timeMs; });
    B("SecondsPassed", [W](AmxVM&, A a, int) { return (W->timeMs - a[0]) / 1000; });
    B("Hour", [W](AmxVM&, A, int) { return ((int)(W->clock0_ + W->timeMs / 1000) / 3600) % 24; });
    B("Minute", [W](AmxVM&, A, int) { return ((int)(W->clock0_ + W->timeMs / 1000) / 60) % 60; });
    B("RandomNumber", [](AmxVM&, A a, int) { int lo = a[0], hi = a[1]; if (hi < lo) std::swap(lo, hi); return (int32_t)(lo + (int)(rng()() % (uint32_t)(hi - lo + 1))); });
    B("GetGlobal", [W](AmxVM& v, A a, int) { std::string gn = v.getString(a[0]); auto it = W->globals.find(gn); if (std::getenv("VB_TRACE")) { static std::set<std::string> seen; if (seen.insert(gn).second) std::fprintf(stderr, "GetGlobal(\"%s\")\n", gn.c_str()); } return it == W->globals.end() ? 0 : it->second; });
    B("SetGlobal", [W](AmxVM& v, A a, int) { std::string gn = v.getString(a[0]); if (std::getenv("VB_DEBUG_SKILL")) std::fprintf(stderr, "SetGlobal(%s, %d)\n", gn.c_str(), a[1]); W->globals[gn] = a[1]; return 0; });
    B("IncGlobal", [W](AmxVM& v, A a, int n) { return W->globals[v.getString(a[0])] += n > 1 ? a[1] : 1; });
    B("DecGlobal", [W](AmxVM& v, A a, int n) { return W->globals[v.getString(a[0])] -= n > 1 ? a[1] : 1; });
    B("GetMapID", [W](AmxVM&, A, int) { return W->map_ ? (int32_t)W->map_->id : 0; });
    B("SetValue", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->values[a[1]] = a[2]; return 0; });
    B("GetValue", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); if (!e) return 0; auto it = e->values.find(a[1]); return it == e->values.end() ? 0 : it->second; });

    // ---- entity properties
    B("GetType", [E](AmxVM&, A a, int) {
        Entity* e = E(a[0]); if (!e) return 0;
        switch (e->kind) { case EntKind::Player: case EntKind::Critter: return 1; case EntKind::Item: case EntKind::Usable: return 2; case EntKind::Door: return 3; case EntKind::Container: return 4; case EntKind::Prop: return 5; default: return 6; } });
    B("GetRace", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->race : 0; });
    B("GetGender", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->gender : 0; });
    B("GetCurrentHP", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->hp : 0; });
    B("GetMaxHP", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->maxHp : 0; });
    B("IsDead", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return !e || e->dead || e->destroyed ? 1 : 0; });
    B("HasDied", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e && e->dead ? 1 : 0; });
    B("AddHP", [W, E](AmxVM& v, A a, int) { Entity* e = E(a[0]); if (!e) return 0; Entity* me = (Entity*)v.user; if (a[1] < 0) W->damage(*e, -a[1], me ? me->id : 0); else e->hp = std::min(e->maxHp, e->hp + a[1]); return e->hp; });
    B("GetPrimaryAttribute", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e && a[1] >= 0 && a[1] < 7 ? e->special[a[1]] : 5; });
    B("GetTotalSkill", [](AmxVM&, A, int) { return 50; });
    B("IsLocked", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e && e->locked ? 1 : 0; });
    B("LockDoor", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->locked = true; return 0; });
    B("UnlockDoor", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->locked = false; return 0; });
    B("ActivateEntity", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->active = true; return 0; });
    B("DeactivateEntity", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->active = false; return 0; });
    B("DestroyEntity", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) { e->destroyed = true; e->active = false; } return 0; });
    B("IsUnderFOW", [W, E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e && W->host.underFog && W->host.underFog(e->x, e->z) ? 1 : 0; });
    B("SetModel", [E](AmxVM& v, A a, int) { if (Entity* e = E(a[0])) e->modelOverride = v.getString(a[1]); return 0; });

    // ---- position, geometry
    B("GetPosition", [E](AmxVM& v, A a, int) { Entity* e = E(a[0]); if (!e) return 0; writeVec(v, a[1], e->x, e->y, e->z); return 1; });
    B("GetFacing", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); if (!e) return 0; int d = (int)std::lround(e->yaw * 180.f / kPi); return ((d % 360) + 360) % 360; });
    B("SetFacing", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->yaw = (float)a[1] * kPi / 180.f; return 0; });
    B("SetFacingTarget", [E](AmxVM&, A a, int) { Entity *e = E(a[0]), *t = E(a[1]); if (e && t) e->yaw = std::atan2(t->x - e->x, t->z - e->z); return 0; });
    B("GetDirectionToEntity", [E](AmxVM&, A a, int) { Entity *e = E(a[0]), *t = E(a[1]); if (!e || !t) return 0; return World::degOf(t->x - e->x, t->z - e->z); });
    B("Distance", [E](AmxVM&, A a, int) { Entity *e = E(a[0]), *t = E(a[1]); if (!e || !t) return 9999; return (int32_t)std::lround(std::hypot(t->x - e->x, t->z - e->z)); });
    B("PointDistance", [](AmxVM& v, A a, int) { float p[3], q[3]; if (!readVec(v, a[0], p) || !readVec(v, a[1], q)) return 9999; return (int32_t)std::lround(std::hypot(q[0] - p[0], q[2] - p[2])); });
    B("GetLOS", [W, E](AmxVM&, A a, int) { Entity *e = E(a[0]), *t = E(a[1]); if (!e || !t) return 0; return W->host.lineOfSight ? (W->host.lineOfSight(e->x, e->z, t->x, t->z) ? 1 : 0) : 1; });
    B("GetLOSPoint", [W, E](AmxVM& v, A a, int) { Entity* e = E(a[0]); float p[3]; if (!e || !readVec(v, a[1], p)) return 0; return W->host.lineOfSight ? (W->host.lineOfSight(e->x, e->z, p[0], p[2]) ? 1 : 0) : 1; });
    B("IsEntityInArea", [W, E](AmxVM& v, A a, int) { Entity* e = E(a[0]); if (!e) return 0; std::string area = lower(v.getString(a[1])); return area == lower(W->mapName) || area == lower(W->find(2) ? W->find(2)->instance : "") ? 1 : 0; });
    B("FindObjects", [W](AmxVM& v, A a, int) {         // (&list, pos[3], radius) -> count; the list handle is stored through the first argument
        float p[3]; if (!readVec(v, a[1], p)) return 0; float r = asF(a[2]);
        std::vector<int> found;
        for (Entity& e : W->ents_) { if (e.kind == EntKind::Trigger || e.kind == EntKind::MapScript || e.destroyed || !e.active) continue; if (std::hypot(e.x - p[0], e.z - p[2]) <= r) found.push_back(e.id); }
        int32_t old; if (v.cellAt(a[0], old) && old > 0) W->lists_.erase(old);
        int h = W->nextList_++; W->lists_[h] = found; v.setCell(a[0], h); return (int32_t)found.size(); });
    B("MatchEntity", [W](AmxVM& v, A a, int) {          // (&list, pattern) ; pattern with '*' wildcards against the instance name
        std::string pat = v.getString(a[1]); std::vector<int> found;
        auto match = [&](const std::string& s) { size_t pi = 0, si = 0, star = std::string::npos, mark = 0;
            while (si < s.size()) { if (pi < pat.size() && (pat[pi] == s[si])) { pi++; si++; } else if (pi < pat.size() && pat[pi] == '*') { star = pi++; mark = si; } else if (star != std::string::npos) { pi = star + 1; si = ++mark; } else return false; }
            while (pi < pat.size() && pat[pi] == '*') pi++; return pi == pat.size(); };
        for (Entity& e : W->ents_) if (!e.instance.empty() && match(e.instance)) found.push_back(e.id);
        int32_t old; if (v.cellAt(a[0], old) && old > 0) W->lists_.erase(old);
        int h = W->nextList_++; W->lists_[h] = found; v.setCell(a[0], h); return (int32_t)found.size(); });
    B("GetObject", [W](AmxVM&, A a, int) { auto it = W->lists_.find(a[0]); if (it == W->lists_.end() || a[1] < 0 || a[1] >= (int)it->second.size()) return 0; return it->second[(size_t)a[1]]; });
    B("FindEntity", [W](AmxVM& v, A a, int) { std::string n = v.getString(a[0]); for (Entity& e : W->ents_) if (e.instance == n) return e.id; return 0; });
    B("FindTeamMembers", [](AmxVM&, A, int) { return 0; });
    B("GetWayPointCount", [W](AmxVM& v, A a, int) { std::string n = v.getString(a[0]); if (W->map_) for (auto& p : W->map_->paths) if (p.name == n) return (int32_t)(p.pts.size() / 3); return 0; });
    B("GetWayPointPosition", [W](AmxVM& v, A a, int) { std::string n = v.getString(a[0]); if (W->map_) for (auto& p : W->map_->paths) if (p.name == n && a[1] >= 0 && (size_t)a[1] * 3 + 2 < p.pts.size()) { writeVec(v, a[2], p.pts[(size_t)a[1] * 3], p.pts[(size_t)a[1] * 3 + 1], p.pts[(size_t)a[1] * 3 + 2]); return 1; } return 0; });
    B("GetNearestPath", [W, E](AmxVM& v, A a, int) {   // (entity, &name buffer) -> waypoint count of the nearest path
        Entity* e = E(a[0]); if (!e || !W->map_) return 0; float best = 1e30f; const MapPath* bp = nullptr;
        for (auto& p : W->map_->paths) if (p.pts.size() >= 3) { float d = std::hypot(p.pts[0] - e->x, p.pts[2] - e->z); if (d < best) { best = d; bp = &p; } }
        if (!bp) return 0; for (size_t i = 0; i <= bp->name.size(); i++) v.setCell(a[1] + (int32_t)i * 4, i < bp->name.size() ? (unsigned char)bp->name[i] : 0); return (int32_t)(bp->pts.size() / 3); });
    B("GetNavPointCount", [W](AmxVM&, A, int) { return W->map_ ? (int32_t)W->map_->nav.size() : 0; });
    B("GetNavPointPosition", [W](AmxVM& v, A a, int) { if (!W->map_ || a[0] < 0 || (size_t)a[0] >= W->map_->nav.size()) return 0; auto& n = W->map_->nav[(size_t)a[0]]; writeVec(v, a[1], n.pos[0], n.pos[1], n.pos[2]); return 1; });

    // ---- teams, squads
    B("GetTeam", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->team : 0; });
    B("GetSquadID", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->squad : 0; });
    B("GetTeamRank", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->teamRank : 0; });
    B("GetSquadRank", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return e ? e->squadRank : 0; });
    B("SetTeamRank", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->teamRank = a[1]; return 0; });
    B("SetSquadRank", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->squadRank = a[1]; return 0; });
    B("CreateTeam", [W](AmxVM&, A, int) { return W->nextTeam_++; });
    B("CreateSquad", [W](AmxVM&, A, int) { return W->nextSquad_++; });
    B("AddToTeam", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->team = a[1]; return 0; });
    B("AddToSquad", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) e->squad = a[1]; return 0; });
    // attitudes: 0 neutral, 1 ally, 2 wary, 3 enemy (case order of the shipped OnMessage handler); own team/squad = ally
    B("GetTeamAttitude", [W](AmxVM&, A a, int) { if (a[0] == a[1] && a[0]) return 1; auto it = W->teamAtt_.find({a[0], a[1]}); return it == W->teamAtt_.end() ? 0 : it->second; });
    B("SetTeamAttitude", [W](AmxVM&, A a, int) { W->teamAtt_[{a[0], a[1]}] = a[2]; return 0; });
    B("GetSquadAttitude", [W](AmxVM&, A a, int) { if (a[0] == a[1] && a[0]) return 1; auto it = W->squadAtt_.find({a[0], a[1]}); return it == W->squadAtt_.end() ? 0 : it->second; });
    B("SetSquadAttitude", [W](AmxVM&, A a, int) { W->squadAtt_[{a[0], a[1]}] = a[2]; return 0; });

    // ---- messages
    B("SendMessage", [W](AmxVM& v, A a, int n) {         // (target, id, &arg...) : the extra arguments are passed by reference
        Msg m; m.to = a[0]; m.from = ((Entity*)v.user) ? ((Entity*)v.user)->id : 0; m.id = a[1];
        for (int i = 2; i < n; i++) { int32_t c = 0; v.cellAt(a[i], c); m.args.push_back(c); }
        W->msgs_.push_back(m); return 1; });
    B("SendMessageToTeam", [W](AmxVM& v, A a, int n) {
        for (Entity& e : W->ents_) if (e.team == a[0] && e.id != ((Entity*)v.user)->id) { Msg m; m.to = e.id; m.from = ((Entity*)v.user)->id; m.id = a[1]; for (int i = 2; i < n; i++) { int32_t c = 0; v.cellAt(a[i], c); m.args.push_back(c); } W->msgs_.push_back(m); }
        return 1; });
    B("MessageArgCount", [W](AmxVM&, A, int) { return (int32_t)W->curArgs_.size(); });
    B("MessageGetArg", [W](AmxVM&, A a, int) { return a[0] >= 0 && (size_t)a[0] < W->curArgs_.size() ? W->curArgs_[(size_t)a[0]] : 0; });

    // ---- actions (queued per entity)
    auto enqueue = [W, E](int32_t id, Action ac) { Entity* e = E(id); if (!e) return 0;
        if (std::getenv("VB_DEBUG_ACTIONS")) std::fprintf(stderr, "[%.1fs] action %s type %d a=%d b=%d seconds=%.2f target=%d at %.1f %.1f (%.1f %.1f)\n", (float)W->timeMs / 1000.f, e->instance.c_str(), (int)ac.type, ac.a, ac.b, ac.seconds, ac.target, ac.x, ac.z, e->x, e->z);
        e->queue.push_back(std::move(ac)); return 1; };
    B("ClearActions", [E](AmxVM&, A a, int) { if (Entity* e = E(a[0])) { e->queue.clear(); e->hasCur = false; e->moving = false; e->anim = e->dead ? "death" : "idle"; } return 0; });
    B("IsIdle", [E](AmxVM&, A a, int) { Entity* e = E(a[0]); return !e || (!e->hasCur && e->queue.empty()) ? 1 : 0; });
    B("A_Wait", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::Wait; ac.seconds = a[1] / 1000.f; return enqueue(a[0], ac); });
    B("A_MoveToPosition", [enqueue](AmxVM& v, A a, int) { float p[3]; if (!readVec(v, a[1], p)) return 0; Action ac; ac.type = Action::MoveToPos; ac.x = p[0]; ac.z = p[2]; ac.run = a[2]; return enqueue(a[0], ac); });
    B("A_MoveToEntity", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::MoveToEnt; ac.target = a[1]; ac.run = a[2]; return enqueue(a[0], ac); });
    B("A_Wander", [enqueue](AmxVM& v, A a, int) { float p[3], q[3]; if (!readVec(v, a[1], p) || !readVec(v, a[2], q)) return 0; Action ac; ac.type = Action::Wander; ac.x = std::min(p[0], q[0]); ac.z = std::min(p[2], q[2]); ac.x2 = std::max(p[0], q[0]); ac.z2 = std::max(p[2], q[2]); return enqueue(a[0], ac); });
    B("A_PlayAnim", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::PlayAnim; ac.a = a[1]; ac.seconds = 1.5f; return enqueue(a[0], ac); });
    B("A_Attack", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::Attack; ac.target = a[1]; ac.seconds = 1.0f; return enqueue(a[0], ac); });
    B("A_JustAttack", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::Attack; ac.target = a[1]; ac.seconds = 1.0f; return enqueue(a[0], ac); });
    B("A_Kill", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::Kill; return enqueue(a[0], ac); });
    B("A_ReloadWeapon", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::Wait; ac.seconds = 1.f; return enqueue(a[0], ac); });
    B("A_SendMessage", [enqueue](AmxVM&, A a, int) { Action ac; ac.type = Action::Send; ac.a = a[1]; return enqueue(a[0], ac); });
    B("A_FloatingSpeechText", [W, enqueue](AmxVM& v, A a, int n) { Action ac; ac.type = Action::Speech; ac.a = a[1]; ac.seconds = n > 2 ? a[2] / 1000.f : 3.f; ac.b = n >= 6 ? ((a[3] & 255) << 16 | (a[4] & 255) << 8 | (a[5] & 255)) : 0xffffff; return enqueue(a[0], ac); });

    // ---- text on screen, journal
    B("FloatingSpeechText", [W, E](AmxVM&, A a, int n) {         // (entity, strref, duration ms, r, g, b)
        Entity* e = E(a[0]); if (!e) return 0; unsigned rgb = n >= 6 ? ((a[3] & 255) << 16 | (a[4] & 255) << 8 | (a[5] & 255)) : 0xffffff;
        W->say(*e, a[1], n > 2 ? a[2] / 1000.f : 0.f, rgb); return 1; });
    B("FloatingDamageText", [W, E](AmxVM& v, A a, int n) {
        Entity* e = E(a[0]); if (!e) return 0; std::string s = v.getString(a[1]); if (s.empty() || s[0] < 32 || s[0] > 126) s = std::to_string(a[1]);
        FloatText f; f.ent = e->id; f.text = s; f.rgb = n >= 5 ? ((a[2] & 255) << 16 | (a[3] & 255) << 8 | (a[4] & 255)) : 0xff0000; f.ttl = 1.6f; f.speech = false; f.x = e->x; f.y = e->y; f.z = e->z; W->floats.push_back(f); return 1; });
    B("DisplayStringRef", [W](AmxVM&, A a, int) { std::string t = W->text(a[0]); if (!t.empty()) W->log.push_back(t); return 0; });
    B("OpenTask", [W](AmxVM&, A a, int) { for (auto& t : W->journal) if (t.id == a[0]) { t.state = 0; return 0; } JournalTask t; t.id = a[0]; W->journal.push_back(t); return 0; });
    B("AddEntry", [W](AmxVM&, A a, int) { for (auto& t : W->journal) if (t.id == a[0]) { t.entries.push_back(a[1]); return 0; } JournalTask t; t.id = a[0]; t.entries.push_back(a[1]); W->journal.push_back(t); return 0; });
    B("CloseTask", [W](AmxVM&, A a, int) { for (auto& t : W->journal) if (t.id == a[0]) t.state = 1; return 0; });

    // ---- sound and effects (no audio/VFX yet: only ids)
    B("AttachSound", [W](AmxVM& v, A a, int n) {          // (entity, wav file, loop flag)
        std::string wav = n > 1 ? v.getString(a[1]) : std::string(); if (!wav.empty() && (wav[0] < 32 || wav[0] > 126)) wav.clear();
        if (std::getenv("VB_DEBUG_SOUND")) { Entity* se = W->find(a[0]); std::fprintf(stderr, "AttachSound(ent %d, \"%s\", %d) hp %d/%d\n", a[0], wav.c_str(), n > 2 ? a[2] : 0, se ? se->hp : -1, se ? se->maxHp : -1); }
        int id = W->host.attachSound && !wav.empty() ? W->host.attachSound(a[0], wav, n > 2 && a[2] != 0) : 0; return id ? id : W->nextSound_++; });
    B("RemoveSound", [W](AmxVM&, A a, int) { if (W->host.removeSound) W->host.removeSound(a[0]); return 0; });
    // visual effects attached to an entity by name ("FX_ElectricalTechGeneratorActive_01.veg"); the host plays them (vegs) and stops them when they are removed
    B("AttachVEG", [W](AmxVM& v, A a, int) { Veg g; g.id = W->vegSerial_++; g.ent = a[0]; g.name = v.getString(a[1]); W->vegs.push_back(g); return g.id; });
    B("RemoveVEG", [W](AmxVM&, A a, int) { for (size_t i = 0; i < W->vegs.size(); i++) if (W->vegs[i].id == a[1]) { W->vegs.erase(W->vegs.begin() + (long)i); break; } return 0; });
    B("GetVEGID", [W](AmxVM& v, A a, int) { std::string n = v.getString(a[1]); for (const Veg& g : W->vegs) if (g.ent == a[0] && g.name == n) return g.id; return 0; });
    B("MapEffectSetActive", [W](AmxVM& v, A a, int) { W->mapEffects[v.getString(a[1])] = a[2] != 0; return 0; });       // (map id, effect group name, on): the map's emitters "<group>_NNN" are switched
    B("MapEffectResetElapsedTime", [](AmxVM&, A, int) { return 0; });

    // ---- party / items (inventory not implemented yet)
    B("IsInParty", [](AmxVM&, A, int) { return 0; });
    // Item references from scripts are read as an item name (template file or display name, case-insensitive substring); the exact script signatures are not verified,
    // so a call that does not parse as a name is noted in `debug` once.
    auto itemRef = [W](AmxVM& v, A a, int n, int idx) {
        std::string r; if (n > idx) { r = v.getString(a[idx]); for (char c : r) if ((unsigned char)c < 32 || (unsigned char)c > 126) { r.clear(); break; } }
        if (r.empty() && W->debug.size() < 200) W->debug.push_back("item native called with a reference that is not a string (arg " + std::to_string(idx) + " = " + (n > idx ? std::to_string(a[idx]) : "none") + ")");
        return r; };
    B("PartyHasItem", [W, itemRef](AmxVM& v, A a, int n) { return W->itemCount(itemRef(v, a, n, n > 1 ? 1 : 0)); });
    B("HasItem", [W, E, itemRef](AmxVM& v, A a, int n) { Entity* e = n > 0 ? E(a[0]) : nullptr; if (e && e->kind != EntKind::Player) { std::string r = lower(itemRef(v, a, n, 1)); int c = 0; for (auto& f : e->cargo) if (!r.empty() && lower(f).find(r) != std::string::npos) c++; return c; } return W->itemCount(itemRef(v, a, n, 1)); });
    B("IsItemEquipped", [W, itemRef](AmxVM& v, A a, int n) { std::string r = lower(itemRef(v, a, n, 1)); if (r.empty()) return 0; for (auto& f : W->equipped) if (!f.empty() && lower(f).find(r) != std::string::npos) return 1; return 0; });
    B("EquipItem", [W, E, itemRef](AmxVM& v, A a, int n) {
        Entity* e = n > 0 ? E(a[0]) : nullptr; if (!e || e->kind != EntKind::Player) return 1;       // only the protagonist's equipment is tracked
        std::string r = lower(itemRef(v, a, n, 1)); if (r.empty()) return 0;
        for (size_t i = 0; i < W->bag.size(); i++) if (lower(W->bag[i].file).find(r) != std::string::npos) { int sl = W->defaultSlot(W->bag[i].file); return sl >= 0 && W->equip((int)i, sl) ? 1 : 0; }
        return 0; });
    B("GiveNewItem", [W, E, itemRef](AmxVM& v, A a, int n) {
        Entity* e = n > 0 ? E(a[0]) : nullptr; std::string r = itemRef(v, a, n, 1); if (!e || r.empty()) return 0; int cnt = n > 2 && a[2] > 0 && a[2] < 10000 ? a[2] : 1;
        if (e->kind == EntKind::Player) W->addItem(r, cnt); else for (int i = 0; i < cnt; i++) e->cargo.push_back(r);
        return 1; });
    B("StartStore", [W](AmxVM&, A, int) { W->log.push_back("(trading is not implemented yet)"); return 0; });

    // ---- dialogue
    B("IsInDialogue", [W](AmxVM&, A, int) { return W->dlg_.active ? 1 : 0; });
    B("StartDialogue", [W](AmxVM&, A a, int) { W->pendingDialogue_ = true; W->pendingDialogueNpc_ = a[0]; return 1; });
    B("SetCurrentNodeName", [W](AmxVM& v, A a, int) { W->dlg_.nodeName = v.getString(a[0]); W->dlg_.serial++; return 0; });
    B("SetNodeTextFunction", [W](AmxVM& v, A a, int) { W->dlg_.nodeFunc = v.getString(a[0]); return 0; });
    B("RegisterNodeText", [W](AmxVM&, A a, int) { std::string t = W->text(a[0]); if (!t.empty()) W->dlg_.npcText.push_back(t); return 0; });
    B("RegisterReply", [W](AmxVM& v, A a, int) {
        DialogueReply r; r.index = a[0]; r.strref = a[1]; r.action = v.getString(a[2]); r.link = v.getString(a[3]); r.text = W->text(a[1]);
        W->dlg_.replies.push_back(r); return 0; });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------ simulation
bool World::beginMove(Entity& e, float gx, float gz, bool run) {
    std::vector<std::pair<float, float>> path;
    bool ok = host.findPath ? host.findPath(e.x, e.z, gx, gz, path) : (path.push_back({gx, gz}), true);
    if (!ok || path.empty()) return false;
    e.path = path; e.pathNext = 0; e.moving = true; e.running = run; e.speed = run ? kRunSpeed : kWalkSpeed; e.anim = run ? "run" : "walk";
    return true;
}

void World::startAction(Entity& e) {
    e.cur = e.queue.front(); e.queue.pop_front(); e.hasCur = true; e.actionT = e.cur.seconds;
    Action& a = e.cur;
    if (cs_.active && e.kind == EntKind::Critter && inOrder(e.id) && (a.type == Action::MoveToPos || a.type == Action::MoveToEnt || a.type == Action::Wander)) { e.hasCur = false; return; }      // in combat the engine moves them
    switch (a.type) {
        case Action::MoveToPos: if (!beginMove(e, a.x, a.z, a.run != 0)) e.hasCur = false; break;
        case Action::MoveToEnt: { Entity* t = find(a.target); if (!t || !beginMove(e, t->x, t->z, a.run != 0)) e.hasCur = false; break; }
        case Action::Wander: {
            bool ok = false;
            for (int tries = 0; tries < 10 && !ok; tries++) {
                float fx = a.x + (a.x2 - a.x) * (float)(rng()() % 1000) / 1000.f, fz = a.z + (a.z2 - a.z) * (float)(rng()() % 1000) / 1000.f;
                if (host.walkable && !host.walkable(fx, fz)) continue;
                ok = beginMove(e, fx, fz, false);
            }
            if (!ok) { e.hasCur = false; }
            break; }
        case Action::PlayAnim:
            if (e.kind == EntKind::Door) { setDoor(e, true); e.anim = "idle"; break; }
            // the dogs' scripts play three of the engine's animation ids (guessed from the order they use them in: 271 then 487 to lie down, 488 to get up): lying down, staying down, getting up
            // the broken eye bot: 490 = twitch (it falls back to lying), 7 = its repair / maintenance animation (guessed from the sequence in its script and the animations it has)
            if (e.kind == EntKind::Critter && (a.a == 490 || a.a == 7)) {
                const char* pose = a.a == 490 ? "twitch" : "maint"; e.anim = pose;
                float len = host.animSeconds ? host.animSeconds(e.id, pose) : 0.f; e.actionT = len > 0.f ? len : 1.5f;
                break;
            }
            if (e.kind == EntKind::Critter && (a.a == 271 || a.a == 487 || a.a == 488)) {
                const char* pose = a.a == 271 ? "laydown" : a.a == 487 ? "lying" : "getup"; e.anim = pose;
                float len = (a.a == 487 || !host.animSeconds) ? 0.f : host.animSeconds(e.id, pose); if (len > 0.f) e.actionT = len;
                break;
            }
            e.anim = "idle"; break;       // other animation ids are not played yet
        case Action::Attack: {            // turn-based: the script only says whom to fight; the engine plays the turns (see combatAi)
            Entity* t = find(a.target); e.hasCur = false;
            if (t && !t->dead && e.kind == EntKind::Critter && !e.dead) { e.aggro = a.target; if (!cs_.active) { if (enemies(e, *t) && std::hypot(t->x - e.x, t->z - e.z) < 45.f) startCombat(e.id); } else joinCombat(e); }       // (only against someone within earshot)
            break; }
        case Action::Speech: say(e, a.a, a.seconds, (unsigned)a.b); break;           // the action lasts as long as the text is shown
        case Action::Kill: damage(e, e.hp + 1, 0); e.hasCur = false; break;
        case Action::Send: { Msg m; m.to = e.id; m.from = 0; m.id = e.cur.a; msgs_.push_back(m); e.hasCur = false; break; }       // A_SendMessage(entity, id): the entity gets the message when the action comes up in its queue
        case Action::Wait: break;
    }
}

void World::stepAction(Entity& e, float dt) {
    if (e.dead) return;
    if (!e.hasCur && !e.queue.empty()) startAction(e);
    if (e.moving) {
        if (e.pathNext >= e.path.size()) { e.moving = false; }
        else {
            float tx = e.path[e.pathNext].first - e.x, tz = e.path[e.pathNext].second - e.z, d = std::hypot(tx, tz);
            float want = std::atan2(tx, tz), diff = std::remainder(want - e.yaw, 2 * kPi); e.yaw += std::max(-12.f * dt, std::min(12.f * dt, diff));
            float mv = e.speed * dt;
            if (cs_.active && e.kind == EntKind::Critter && inOrder(e.id)) { if (e.ap <= 0.f) { e.moving = false; e.path.clear(); e.anim = "idle"; } else e.ap = std::max(0.f, e.ap - mv / kUnitsPerAp); }
            if (e.hasCur && e.cur.type == Action::MoveToEnt) { Entity* t = find(e.cur.target); if (t && std::hypot(t->x - e.x, t->z - e.z) < 3.f) { e.moving = false; e.path.clear(); } }
            if (e.moving) {
                if (mv >= d) { e.x = e.path[e.pathNext].first; e.z = e.path[e.pathNext].second; e.pathNext++; }
                else { e.x += tx / d * mv; e.z += tz / d * mv; }
                if (e.pathNext >= e.path.size()) e.moving = false;
            }
        }
        float gy = e.y; if (host.groundY && host.groundY(e.x, e.z, gy)) e.y = gy;
        if (!e.moving) { e.anim = "idle"; e.path.clear(); if (e.hasCur && (e.cur.type == Action::MoveToPos || e.cur.type == Action::MoveToEnt || e.cur.type == Action::Wander)) e.hasCur = false; }
    } else if (e.hasCur) {
        if (e.cur.type == Action::Wait || e.cur.type == Action::PlayAnim || e.cur.type == Action::Attack || e.cur.type == Action::Speech) { e.actionT -= dt; if (e.actionT <= 0) { e.hasCur = false; if (e.anim == "laydown") e.anim = "lying"; else if (e.anim == "twitch") e.anim = e.brokenRobot ? "lying" : "idle"; else if (e.anim != "lying") e.anim = "idle"; } }      // a dog that lay down stays down until its script has it get up or walk
        else e.hasCur = false;
    }
}

void World::deliver() {
    int guard = 128;
    while (!msgs_.empty() && guard-- > 0) {
        Msg m = msgs_.front(); msgs_.pop_front();
        Entity* t = find(m.to); if (!t || t->destroyed || !t->vm) continue;
        curArgs_ = m.args; int32_t r; run(*t, "OnMessage", {m.from, m.id}, r);
    }
}

void World::updateZones(float) {
    Entity& pc = ents_[0];
    for (Entity& z : ents_) {
        if (z.kind != EntKind::Trigger) continue;
        bool in = pointInPoly(z.zone, pc.x, pc.z), was = z.inside.count(1) > 0;
        if (in && !was) {
            z.inside.insert(1);
            if (z.zoneKind == 0) { hasMapChange = true; mapChange.map = z.zoneTarget; mapChange.entry = z.zoneEntry; }
            else { int32_t r; run(z, "OnEnter", {1, timeMs}, r); }
        } else if (!in && was) { z.inside.erase(1); if (z.zoneKind == 1) { int32_t r; run(z, "OnLeave", {1, timeMs}, r); } }
    }
}

void World::update(float dt) {
    timeMs += (int)std::lround(dt * 1000.f);
    float now = (float)timeMs / 1000.f;
    for (Entity& e : ents_) { e.cooldown = std::max(0.f, e.cooldown - dt); e.attackT = std::max(0.f, e.attackT - dt); e.hitT = std::max(0.f, e.hitT - dt); }
    if (ents_[0].dead) { for (auto& f : floats) f.t += dt; floats.erase(std::remove_if(floats.begin(), floats.end(), [](const FloatText& f) { return f.t > f.ttl; }), floats.end()); return; }      // game over: the world stops (the shipped AI scripts spin forever without a living protagonist)
    updateImpacts(dt);
    deliver();
    // perception: nearby scripted people get a "detection" message about the protagonist twice a second (message 1 = sender noticed). The stray dogs are left out: on that message their script
    // parks them in AI state 5, which has no handler, so they would stand still for ever; the tech demo's strays wander, lie down and walk up to the player on their own (their own Distance <= 20 test).
    static float perceptT = 0; perceptT += dt;
    bool perceive = perceptT >= 0.5f; if (perceive) perceptT = 0;
    for (Entity& e : ents_) {
        if (e.destroyed || !e.active || e.kind == EntKind::Trigger) continue;
        if (e.kind == EntKind::Player) { if (playerScripted()) stepAction(e, dt); continue; }
        if (e.brokenRobot && e.hp >= e.maxHp && !e.dead) {       // repaired: it gets up and carries on
            e.brokenRobot = false; e.anim = "getup"; e.hasCur = true; e.cur = Action(); e.cur.type = Action::PlayAnim; e.moving = false; e.path.clear();
            float len = host.animSeconds ? host.animSeconds(e.id, "getup") : 0.f; e.actionT = len > 0.f ? len : 1.f;
        }
        stepAction(e, dt);
        if (e.kind == EntKind::Door && e.vm && !e.locked) {       // doors open by themselves for NPCs walking through them (the demo's people cross the vault's rooms), and close again a moment after the last one has passed
            bool near = false; for (const Entity& o : ents_) if (o.kind == EntKind::Critter && !o.dead && o.active && o.moving && std::hypot(o.x - e.x, o.z - e.z) < 5.5f) { near = true; break; }
            if (near && !e.open) { setDoor(e, true); e.autoOpened = true; e.autoCloseT = 0; }
            else if (e.open && e.autoOpened) { bool any = false; for (const Entity& o : ents_) if (o.kind == EntKind::Critter && !o.dead && o.active && std::hypot(o.x - e.x, o.z - e.z) < 7.f) { any = true; break; } if (any) e.autoCloseT = 0; else if ((e.autoCloseT += dt) > 2.5f) { setDoor(e, false); e.autoOpened = false; } }
            else if (!e.open) e.autoOpened = false;
        }
        if (e.kind == EntKind::Door) { float goal = e.open ? 1.f : 0.f; e.openAmt += std::max(-dt * 1.5f, std::min(dt * 1.5f, goal - e.openAmt)); }
        if (e.vm && !e.dead && now >= e.nextUpdate) { e.nextUpdate = now + 0.1f; int32_t r; run(e, "OnUpdate", {timeMs}, r); }
        if (const char* tn = std::getenv("VB_TRACE_STATE")) if (e.vm && e.instance == tn) { static int lastSec = -1; if (timeMs / 1000 != lastSec) { lastSec = timeMs / 1000; int32_t ai = -1, s244 = -1, s328 = -1, s348 = -1, s352 = -1; e.vm->cellAt(1460, ai); e.vm->cellAt(244, s244); e.vm->cellAt(328, s328); e.vm->cellAt(348, s348); e.vm->cellAt(352, s352); std::fprintf(stderr, "[%ds] %s aiState=%d [244]=%d [328]=%d sub348=%d sub352=%d moving=%d anim=%s hasCur=%d q=%zu\n", lastSec, tn, ai, s244, s328, s348, s352, (int)e.moving, e.anim.c_str(), (int)e.hasCur, e.queue.size()); } }
        if (perceive && e.kind == EntKind::Critter && !e.dead && e.vm && e.scriptName.rfind("00_Animals_Dog_Stray", 0) != 0 && !(e.team && e.team == ents_[0].team) && !(teamAtt_.count({e.team, ents_[0].team}) && teamAtt_[{e.team, ents_[0].team}] == 1) && std::hypot(e.x - ents_[0].x, e.z - ents_[0].z) < 60.f) {
            // enemies keep getting it every 0.5 s; everyone else only when the protagonist comes into range (the handler clears the listener's actions, which would cancel e.g. Frank's walk back to his door)
            if (enemies(e, ents_[0]) || !e.perceives) { Msg m; m.to = e.id; m.from = 1; m.id = 1; msgs_.push_back(m); }
            e.perceives = true;
        }
        if (perceive && e.perceives && std::hypot(e.x - ents_[0].x, e.z - ents_[0].z) > 70.f) e.perceives = false;
    }
    if (const char* tk = std::getenv("VB_TRACK")) { static int lastS = -1; if (timeMs / 1000 != lastS) { lastS = timeMs / 1000; for (Entity& e : ents_) if (e.instance == tk) std::fprintf(stderr, "[%ds] %s at %.1f %.1f yaw %.2f moving=%d hasCur=%d type=%d queue=%zu path=%zu anim=%s hp=%d/%d\n", lastS, tk, e.x, e.z, e.yaw, (int)e.moving, (int)e.hasCur, (int)e.cur.type, e.queue.size(), e.path.size(), e.anim.c_str(), e.hp, e.maxHp); } }       // (debug: VB_TRACK=<instance> prints its movement state every second)
    updateZones(dt);
    updateCombat(dt);
    deliver();
    // pending script-started dialogue
    if (const char* us = std::getenv("VB_USESKILL")) {          // (test hook: VB_USESKILL="Instance:menuIndex@seconds,..." uses that skill menu entry at that game time, no walking, no kit needed except Security / Mechanics)
        static std::set<std::string> done; std::string all = us; size_t pos = 0;
        while (pos < all.size()) { size_t c = all.find(',', pos); std::string item = all.substr(pos, c == std::string::npos ? std::string::npos : c - pos); pos = c == std::string::npos ? all.size() : c + 1;
            size_t col = item.find(':'), at = item.find('@'); if (col == std::string::npos || at == std::string::npos || done.count(item) || timeMs < (int)(std::atof(item.c_str() + at + 1) * 1000)) continue;
            done.insert(item); std::string inst = item.substr(0, col); int idx = std::atoi(item.c_str() + col + 1);
            for (Entity& e : ents_) if (e.instance == inst) { auto so = usableSkills(e.id); std::fprintf(stderr, "[%.1fs] use skill %d of %zu on %s\n", timeMs / 1000.f, idx, so.size(), inst.c_str()); if (idx < (int)so.size()) log.push_back(useSkill(e.id, so[(size_t)idx])); } }
    }
    if (std::getenv("VB_STARTCOMBAT") && timeMs > 500) { static bool once = false; if (!once) { once = true; Entity* best = nullptr; for (Entity& e : ents_) if (e.kind == EntKind::Critter && !e.dead && e.active && enemies(e, ents_[0]) && (!best || std::hypot(e.x - ents_[0].x, e.z - ents_[0].z) < std::hypot(best->x - ents_[0].x, best->z - ents_[0].z))) best = &e; if (best) startCombat(best->id); } }       // (test hook: starts a fight with the nearest enemy)
    if (std::getenv("VB_DIALOGUE_AUTOEND")) { static int startedAt = -1; if (dlg_.active && startedAt < 0) startedAt = timeMs; if (!dlg_.active) startedAt = -1; if (dlg_.active && timeMs - startedAt > 3000) { if (const char* g = std::getenv("VB_DIALOGUE_AUTOEND")) if (g[0] && g[0] != 49) globals[g] = 1; endDialogue(); } }       // (test hook: close every conversation after 3 s)
    // A script's StartDialogue(npc, protagonist) makes the speaker walk up to the protagonist first (Frank in the Vault crosses the room, talks, then returns to his door); give up after 15 s
    if (pendingDialogue_) {
        Entity* n = find(pendingDialogueNpc_);
        bool far = n && !n->dead && std::hypot(n->x - ents_[0].x, n->z - ents_[0].z) > 6.f && pendingDialogueT_ < 15.f;
        if (far) {
            if (!pendingDialogueWalking_) { pendingDialogueWalking_ = true; Action ac; ac.type = Action::MoveToEnt; ac.target = 1; ac.run = 0; n->queue.clear(); n->hasCur = false; n->queue.push_back(ac); }
            pendingDialogueT_ += dt;
        } else { if (std::getenv("VB_DEBUG_ACTIONS") && n) std::fprintf(stderr, "[%.1fs] dialogue starts with %s at distance %.1f\n", (float)timeMs / 1000.f, n->instance.c_str(), std::hypot(n->x - ents_[0].x, n->z - ents_[0].z)); pendingDialogue_ = false; pendingDialogueWalking_ = false; pendingDialogueT_ = 0; startDialogue(pendingDialogueNpc_); }
    }
    for (auto& f : floats) { f.t += dt; Entity* e = find(f.ent); if (e && f.speech) { f.x = e->x; f.y = e->y; f.z = e->z; f.dy = e->brokenRobot ? 2.5f : e->race == 5 ? 5.f : 8.5f; } }
    floats.erase(std::remove_if(floats.begin(), floats.end(), [](const FloatText& f) { return f.t > f.ttl; }), floats.end());
}

bool World::hostile(int id) {
    Entity* e = find(id); if (!e || e->kind != EntKind::Critter || e->dead || !e->active) return false;
    int pt = ents_[0].team; if (e->team == pt) return false;
    auto a = teamAtt_.find({e->team, pt}), b = teamAtt_.find({pt, e->team});
    return (a != teamAtt_.end() && a->second == 3) || (b != teamAtt_.end() && b->second == 3);
}

std::vector<std::string> World::contents(int id) {
    std::vector<std::string> out;
    for (const InvItem& it : cargoOf(id)) { ItemInfo inf = itemInfo(it.file); out.push_back(it.count > 1 ? inf.name + " x" + std::to_string(it.count) : inf.name); }
    return out;
}

std::vector<InvItem> World::cargoOf(int id) {
    std::vector<InvItem> out; Entity* e = find(id); if (!e) return out;
    for (const std::string& f : e->cargo) {
        bool merged = false; if (itemInfo(f).stack) for (InvItem& o : out) if (lower(o.file) == lower(f)) { o.count++; merged = true; break; }
        if (!merged) out.push_back({f, 1});
    }
    return out;
}

int World::takeFromCargo(int id, const std::string& file, int count) {
    Entity* e = find(id); if (!e) return 0; int taken = 0;
    for (size_t i = 0; i < e->cargo.size() && taken < count;) { if (lower(e->cargo[i]) == lower(file)) { e->cargo.erase(e->cargo.begin() + (long)i); taken++; } else i++; }
    if (taken) { addItem(file, taken); ItemInfo inf = itemInfo(file); log.push_back("You take " + (taken > 1 ? std::to_string(taken) + " " : std::string()) + inf.name + "."); }
    return taken;
}

int World::takeAll(int id) {
    int n = 0; for (const InvItem& it : cargoOf(id)) n += takeFromCargo(id, it.file, it.count); return n;
}

ItemInfo World::itemInfo(const std::string& file) {
    ItemInfo inf; inf.file = file; inf.name = file;
    const EntityTemplate* t = elib_ ? ((EntityLibrary*)elib_)->tmpl(file) : nullptr; if (!t) return inf;
    inf.kind = t->kind; inf.model = t->heldModel.empty() ? t->model : t->model; inf.heldModel = t->heldModel; if (t->nameStr > 0) inf.name = text(t->nameStr); else { std::string st = fs::path(file).stem().string(); inf.name = st; }
    if (t->descStr > 0) inf.desc = text(t->descStr);
    if (!t->icons.empty()) inf.icon = t->icons[0];
    if (t->icons.size() > 1) inf.actIcon = t->icons[1];
    if (inf.actIcon.empty() && !inf.icon.empty()) { std::string li = lower(inf.icon); size_t k = li.find("_inv"); if (k != std::string::npos) inf.actIcon = inf.icon.substr(0, k) + "_ACT" + inf.icon.substr(k + 4); }       // items without a second icon (the kits): the action icon sits next to the inventory one (IT_SecurityKit_INV -> _ACT)
    { std::string lf = lower(file); inf.skill = lf.find("security_kit") != std::string::npos ? "Security" : lf.find("tool_kit") != std::string::npos ? "Mechanics" : ""; }
    if (t->kind == "weapon" || !inf.skill.empty()) inf.slot = SlotHand1;       // the kits go in a hand like a weapon
    else if (t->kind == "armor") {
        bool hasBody = false, hasHead = lower(file).find("helmet") != std::string::npos;      // a helmet has a head part and no body part
        for (auto& p : t->parts) { std::string l = lower(p); if (l.compare(0, 3, "bod") == 0) hasBody = true; if (l.compare(0, 3, "hea") == 0) hasHead = true; }
        bool head = hasHead && !hasBody;
        inf.slot = head ? SlotHead : SlotBody;
    }
    inf.stack = t->kind == "ammo" || t->kind == "use" || t->kind == "item";
    return inf;
}

void World::addItem(const std::string& file, int count) {
    if (count <= 0) return;
    if (itemInfo(file).stack) for (InvItem& it : bag) if (lower(it.file) == lower(file)) { it.count += count; return; }
    if (itemInfo(file).stack) { bag.push_back({file, count}); return; }
    for (int i = 0; i < count; i++) bag.push_back({file, 1});
}

int World::defaultSlot(const std::string& file) {
    ItemInfo inf = itemInfo(file); if (inf.slot < 0) return -1;
    if (inf.slot == SlotHand1) return equipped[SlotHand1].empty() || !equipped[SlotHand2].empty() ? SlotHand1 : SlotHand2;
    return inf.slot;
}

bool World::equip(int bagIdx, int slot) {
    if (bagIdx < 0 || bagIdx >= (int)bag.size() || slot < 0 || slot > 3) return false;
    ItemInfo inf = itemInfo(bag[(size_t)bagIdx].file);
    bool fits = inf.slot == slot || (inf.slot == SlotHand1 && slot == SlotHand2);
    if (!fits) { log.push_back(inf.slot < 0 ? "You cannot equip the " + inf.name + "." : "The " + inf.name + " does not go there."); return false; }
    std::string file = bag[(size_t)bagIdx].file;
    if (--bag[(size_t)bagIdx].count <= 0) bag.erase(bag.begin() + bagIdx);
    std::string old = equipped[slot]; equipped[slot] = file; if (!old.empty()) addItem(old);
    if (slot >= SlotHand1) { pclip_[slot - SlotHand1] = -1; if (inf.kind == "weapon") attackHand_ = slot - SlotHand1; }
    log.push_back("You equip the " + inf.name + ".");
    return true;
}

bool World::unequip(int slot) {
    if (slot < 0 || slot > 3 || equipped[slot].empty()) return false;
    ItemInfo inf = itemInfo(equipped[slot]); addItem(equipped[slot]); equipped[slot].clear(); if (slot >= SlotHand1) pclip_[slot - SlotHand1] = -1; log.push_back("You put away the " + inf.name + ".");
    return true;
}

bool World::swapHands() { std::swap(equipped[SlotHand1], equipped[SlotHand2]); std::swap(pclip_[0], pclip_[1]); return true; }

int World::itemCount(const std::string& pattern, bool includeEquipped) {
    std::string r = lower(pattern); if (r.empty()) return 0; int n = 0;
    auto match = [&](const std::string& f) { return lower(f).find(r) != std::string::npos || lower(itemInfo(f).name).find(r) != std::string::npos; };
    for (const InvItem& it : bag) if (match(it.file)) n += it.count;
    if (includeEquipped) for (const std::string& f : equipped) if (!f.empty() && match(f)) n++;
    return n;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------ skills
bool World::holdsKitFor(const std::string& skill) {
    for (int h = SlotHand1; h <= SlotHand2; h++) if (!equipped[h].empty() && itemInfo(equipped[h]).skill == skill) return true;
    return false;
}

std::vector<SkillOption> World::usableSkills(int id) {
    std::vector<SkillOption> out; Entity* e = find(id);
    if (!e || e->destroyed || !e->active || !e->vm || e->vm->script->findPublic("GetUsableSkills") < 0) return out;
    AmxVM& vm = *e->vm; int32_t mark = vm.heapMark(), arr = vm.allocArray(26), r = 0;      // 13 pairs (string id, skill id), filled by the script's ShowSkills
    if (arr && run(*e, "GetUsableSkills", {arr}, r))
        for (int i = 0; i < 26; i += 2) { int32_t sr = 0, sid = 0; vm.cellAt(arr + i * 4, sr); vm.cellAt(arr + (i + 1) * 4, sid); if (!sr) break; out.push_back({sid, sr, text(sr)}); }
    vm.heapRelease(mark); return out;
}

// The menu text starts with the skill ("Security - Unlock"): Security needs the Security Kit in a hand (string 671 says so), Mechanics the Tool Kit (same wording, our own string).
std::string World::useSkill(int id, const SkillOption& opt) {
    Entity* e = find(id); if (!e || !e->vm) return "";
    std::string skill = opt.label.substr(0, opt.label.find(" - "));
    if ((skill == "Security" || skill == "Mechanics") && !holdsKitFor(skill)) return skill == "Security" ? text(671) : "You need a tool kit to use the Mechanics skill.";
    size_t before = msgs_.size(); int32_t r = 0; bool ok = run(*e, "UseSkill", {id, opt.id}, r);
    // The shipped UseSkill sends itself message 2 (SendMessage(entity, 2, &skill, &level)), yet every one of those scripts' OnMessage only handles id 3 ("Callback_UseSkill", skill check, UnlockDoor,
    // DisplayStringRef): the engine evidently delivers that message as id 3, so it is renumbered here.
    for (size_t i = before; i < msgs_.size(); i++) if (msgs_[i].id == 2) msgs_[i].id = 3;
    if (std::getenv("VB_DEBUG_SKILL")) std::fprintf(stderr, "UseSkill(%d, %d) ok=%d queued=%zu errs=%d %s\n", id, opt.id, (int)ok, msgs_.size(), vmErrors, debug.empty() ? "" : debug.back().c_str());
    return "";
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------ interaction
std::string World::use(int id) {
    Entity* e = find(id); if (!e || e->destroyed || !e->active) return "";
    int32_t r = 0;
    if (!usable(*e)) return "";
    if (e->kind == EntKind::Door || e->kind == EntKind::Container) {
        bool ok = true;
        if (e->vm && e->vm->script->findPublic(e->open ? "OnClose" : "OnOpen") >= 0) { run(*e, e->open ? "OnClose" : "OnOpen", {1}, r); ok = r != 0; }
        else ok = !e->locked;
        if (e->locked && !e->open) ok = false;
        if (!ok) return e->kind == EntKind::Door ? "The door is locked." : "It is locked.";
        setDoor(*e, !e->open);
        if (e->kind == EntKind::Door) return e->open ? "You open the door." : "You close the door.";
        if (!e->open) return "You close the container.";
        openContainer = id; return "";
    }
    if (e->kind == EntKind::Item) {
        std::string n = itemInfo(e->file).name; e->destroyed = true; e->active = false; addItem(e->file);
        return "You pick up the " + n + ".";
    }
    if (e->kind == EntKind::Critter && !e->dead && !e->dialogue.empty()) { startDialogue(id); return ""; }
    if (e->kind == EntKind::Critter && e->dead) { if (!e->cargo.empty()) openContainer = id; else return "There is nothing on the body."; return ""; }
    return "";
}

bool World::startDialogue(int npc) {
    Entity* e = find(npc); if (!e || e->dialogue.empty() || dlg_.active) return false;
    dscript_ = script(e->dialogue, ".sma"); if (!dscript_) return false;
    dvm_ = std::make_unique<AmxVM>(dscript_); dvm_->user = e; bindNatives(*dvm_, true); dlgNpc_ = e;
    dlg_ = Dialogue(); dlgNodeSerialSeen_ = 0; dlg_.active = true; dlg_.npc = npc; dlg_.script = e->dialogue;
    e->queue.clear(); e->hasCur = false; e->moving = false; e->anim = "idle";
    e->yaw = std::atan2(ents_[0].x - e->x, ents_[0].z - e->z);
    int32_t r; if (!dvm_->call("OnStartDialogue", {}, r, 1500000)) { debug.push_back("dialogue start failed: " + e->dialogue + ": " + dvm_->errorText()); endDialogue(); return false; }
    runDialogueNode();
    if (dlg_.active && host.uiSound) host.uiSound("CombatSequenceStart.wav");
    return dlg_.active;
}

// After a link function ran: if a node was entered, run its text function (registers the replies); with nothing to say the dialogue is over.
void World::runDialogueNode() {
    if (!dlg_.active) return;
    if (dlg_.serial == dlgNodeSerialSeen_ && dlg_.npcText.empty() && dlg_.replies.empty()) { endDialogue(); return; }
    dlgNodeSerialSeen_ = dlg_.serial;
    if (!dlg_.nodeFunc.empty()) { int32_t r; dvm_->call(dlg_.nodeFunc, {}, r, 1500000); }
    if (dlg_.npcText.empty() && dlg_.replies.empty()) { endDialogue(); return; }
}

void World::chooseReply(int idx) {
    if (!dlg_.active) return;
    if (host.uiSound) host.uiSound("f3_button_down_01.wav");
    if (idx < 0 || idx >= (int)dlg_.replies.size()) { endDialogue(); return; }
    DialogueReply rep = dlg_.replies[(size_t)idx];
    int before = dlg_.serial;
    dlg_.npcText.clear(); dlg_.replies.clear(); dlg_.nodeFunc.clear();
    int32_t r;
    if (!rep.action.empty()) dvm_->call(rep.action, {}, r, 1500000);
    if (!rep.link.empty()) dvm_->call(rep.link, {}, r, 1500000);
    if (dlg_.serial == before) { endDialogue(); return; }       // the link did not enter a new node: conversation over
    runDialogueNode();
}

void World::endDialogue() { if (dvm_) for (auto& k : dvm_->unbound) unboundNatives[k.first] += k.second; dlg_ = Dialogue(); dvm_.reset(); dscript_.reset(); dlgNpc_ = nullptr; dlgNodeSerialSeen_ = 0; }




// ------------------------------------------------------------------------------------------------------------------------------------------------------------ combat
// Numbers are a first model, not the original's formulas: damage and (ranged) range come from the weapon template's first attack mode (GIWP / GWAM words 6 and 12, see GAME_DATA_FINDINGS.md),
// unfinished weapon templates and fists use defaults; hit chance from agility, distance; armor is a percentage reduction (GIAR word 0 of body armor + helmet).
WeaponStats World::weaponStats(const std::string& file) {
    WeaponStats w; w.file = file; if (file.empty() || !elib_) { w.name = "Fists"; return w; }
    const EntityTemplate* t = ((EntityLibrary*)elib_)->tmpl(file); if (!t || t->kind != "weapon") { w.name = "Fists"; return w; }
    w.fists = false; w.name = itemInfo(file).name; std::string m = lower(t->model);
    static const char* meleeWords[] = {"knife", "bat", "crowbar", "pipe", "wrench", "hatchet", "machete", "leg", "stick", "shiv", "blade", "rock", "prod", "club", "axe", "sledge", "welder", "fist"};
    w.melee = t->wClass == 6; for (const char* k : meleeWords) if (m.find(k) != std::string::npos) w.melee = true;
    {   // the first single-shot (else the first) attack mode names the effect and the damage type
        const EntityTemplate::AttackMode* am = t->modes.empty() ? nullptr : &t->modes[0]; for (const auto& m : t->modes) if (m.kind == 2) { am = &m; break; }
        if (am) { w.effect = am->effect; w.dmgType = am->dmgType; } w.heldEffect = t->heldEffect; w.heldModel = t->heldModel;
        for (const char* k : {"minigun", "flamethrower", "railgun", "arraygun", "shotgun"}) if (m.find(k) != std::string::npos) w.twoHanded = true;
    }
    if (w.melee) { w.damage = t->wDamage > 0 ? t->wDamage : 8; w.range = 2.6f; w.cooldown = 0.9f; w.ap = 3; }
    else { w.damage = t->wDamage > 0 ? std::max(3, (int)std::lround(t->wDamage * 0.4)) : 8;        // the raw words read like per-burst values (machine pistol 50): scaled to this model's hit points (people have 15-80)
        w.range = t->wRange >= 8.f ? t->wRange : 15.f; w.clip = t->wClip > 0 ? t->wClip : 12; w.cooldown = 1.0f; w.ap = 4; }
    return w;
}

int World::activeHand() { int h = SlotHand1 + attackHand_; return !equipped[h].empty() && itemInfo(equipped[h]).kind == "weapon" ? h : -1; }
std::string World::playerWeapon() { int h = activeHand(); return h < 0 ? std::string() : equipped[h]; }

std::string World::nameOf(const Entity& e) {
    if (e.kind == EntKind::Player) return "You";
    const EntityTemplate* t = elib_ ? ((EntityLibrary*)elib_)->tmpl(e.file) : nullptr;
    if (t && t->nameStr > 0) { std::string n = text(t->nameStr); if (!n.empty()) return n; }
    std::string d = e.display; size_t i = 0; while (i < d.size() && std::isdigit((unsigned char)d[i])) i++; if (i && i < d.size() && d[i] == ' ') d = d.substr(i + 1);
    if (d.empty()) {                                    // an instance name such as "Communist_Insurgent00_TM_001" -> "Communist Insurgent"
        d = e.instance; size_t k = d.find("_TM_"); if (k != std::string::npos) d.resize(k);
        while (!d.empty() && std::isdigit((unsigned char)d.back())) d.pop_back();
        for (char& c : d) if (c == '_') c = ' ';
    }
    return d.empty() ? e.instance : d;
}

bool World::enemies(const Entity& a, const Entity& b) {
    if (&a == &b || a.team == b.team) return false;
    auto x = teamAtt_.find({a.team, b.team}), y = teamAtt_.find({b.team, a.team});
    return (x != teamAtt_.end() && x->second == 3) || (y != teamAtt_.end() && y->second == 3);
}

AttackResult World::tryAttack(int aid, int tid) {
    Entity* a = find(aid); Entity* t = find(tid);
    if (!a || !t || a == t || a->dead || t->dead || t->destroyed || !t->active) return AttackResult::Invalid;
    if (a->attackT > 0) return AttackResult::Cooldown;                                  // still playing the last attack
    bool player = a->kind == EntKind::Player; WeaponStats ws = weaponStats(player ? playerWeapon() : a->weapon);
    float dist = std::hypot(t->x - a->x, t->z - a->z);
    if (dist > ws.range + 0.3f) return AttackResult::OutOfRange;
    if (!ws.melee && !ws.fists && host.lineOfSight && !host.lineOfSight(a->x, a->z, t->x, t->z)) return AttackResult::NoLineOfSight;
    int* clip = nullptr;
    if (player && !ws.fists && !ws.melee) {                                              // the protagonist's guns have clips
        int h = activeHand(); clip = &pclip_[h - SlotHand1]; if (*clip < 0) *clip = ws.clip;
        if (*clip <= 0) { log.push_back("Your " + ws.name + " is empty."); return AttackResult::NoAmmo; }
    }
    if (!cs_.active) startCombat(a->id);
    joinCombat(*a); joinCombat(*t);                                                     // the victim takes part even when it was not hostile before (it becomes so: aggro below)
    if (cs_.order[cs_.cur] != a->id) return AttackResult::NotYourTurn;
    if (a->ap + 1e-3f < (float)ws.ap) return AttackResult::NoAp;
    a->ap -= (float)ws.ap; if (clip) --*clip;
    t->aggro = a->id;
    a->yaw = std::atan2(t->x - a->x, t->z - a->z); a->attackDur = ws.twoHanded ? 1.0f : 0.8f; a->attackT = a->attackDur; a->attackKind = ws.fists ? 0 : ws.melee ? 2 : 1; a->moving = false;
    int agiA = a->special[5], agiT = t->special[5];
    float chance = (ws.melee || ws.fists) ? 85.f + 3.f * (agiA - 5) - 2.f * (agiT - 5) : 80.f + 3.f * (agiA - 5) - 2.f * (agiT - 5) - 1.5f * dist;
    chance = std::max(10.f, std::min(95.f, chance));
    bool hit = (float)(rng()() % 100) < chance; int dmg = 0; if (std::getenv("VB_FORCEHIT") && player) hit = true;       // (test hook)
    a->shotSerial++; a->shotTarget = t->id; a->shotHit = hit;
    std::string an = nameOf(*a), tn = nameOf(*t); bool you = player, tyou = t->kind == EntKind::Player;
    FloatText f; f.ent = t->id; f.ttl = 1.4f; f.speech = false; f.x = t->x; f.y = t->y; f.z = t->z;
    Impact im; im.attacker = a->id; im.target = t->id; im.hit = hit;
    im.delay = ws.fists ? 0.40f : ws.melee ? 0.666f : ws.twoHanded ? 0.733f : 0.533f;      // the attack animation's release / damage cue: nothing lands before the shot is seen
    if (hit) {
        float base = (float)ws.damage * (0.8f + 0.4f * (float)(rng()() % 1000) / 1000.f);
        if (ws.melee || ws.fists) base += (float)std::max(0, a->special[0] - 5);
        if (tyou) { int red = 0; for (int sl : {SlotBody, SlotHead}) if (!equipped[sl].empty()) if (const EntityTemplate* at = ((EntityLibrary*)elib_)->tmpl(equipped[sl])) red += at->armorRating; base *= 1.f - (float)std::min(80, red) / 100.f; }
        dmg = std::max(1, (int)std::lround(base)); im.dmg = dmg;
        f.text = std::to_string(dmg); f.rgb = 0xff4040;       // damage is always red
        im.line = you ? "You hit " + tn + " for " + std::to_string(dmg) + "." : tyou ? an + " hits you for " + std::to_string(dmg) + "." : an + " hits " + tn + " for " + std::to_string(dmg) + ".";
        im.killLine = you ? "You kill " + tn + "." : tyou ? "You are dead." : an + " kills " + tn + ".";
    } else {
        f.text = "miss"; f.rgb = 0xc0c0c0;
        im.line = you ? "You miss " + tn + "." : tyou ? an + " misses you." : an + " misses " + tn + ".";
    }
    im.f = f; impacts_.push_back(im);
    return AttackResult::Fired;
}

// A shot's outcome (damage, hit reaction, floating numbers, log, script callbacks) lands at the attack animation's cue.
void World::updateImpacts(float dt) {
    for (size_t n = 0; n < impacts_.size();) {
        Impact& im = impacts_[n]; im.delay -= dt;
        if (im.delay > 0.f) { n++; continue; }
        Impact c = im; impacts_.erase(impacts_.begin() + (long)n);
        Entity* a = find(c.attacker); Entity* t = find(c.target); if (!a || !t || t->dead || t->destroyed) continue;
        c.f.x = t->x; c.f.y = t->y; c.f.z = t->z; floats.push_back(c.f); log.push_back(c.line);
        if (c.hit) { damage(*t, c.dmg, a->id); if (t->dead) log.push_back(c.killLine); }
        if (!t->dead) { int32_t r; run(*t, "OnAttacked", {a->id, t->id, c.hit ? 1 : 0, 0, c.dmg}, r); }
    }
}

// The protagonist starts with every kind of weapon the combat has art for (test kit) and ammo to reload them.
void World::giveStarterKit() {
    for (const char* w : {"Weapons9mmMachinePistol.WEA", "Weapons44_Revolver.WEA", "Weapons223_Minigun.WEA", "WeaponsLaserPistol.WEA", "WeaponsCombatKnife.WEA", "WeaponsFlamethrower.WEA"}) addItem(w);
    for (const char* a : {"Ammo9mm.AMO", "Ammo44.AMO", "Ammo223.AMO", "AmmoSmall_Energy_Cell.AMO", "AmmoNaphate.AMO"}) addItem(a, 6);
    addItem("KitsFirst_Aid_Kit.ITM", 3); addItem("KitsSecurity_Kit.ITM"); addItem("KitsTool_Kit.ITM");
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------ turn order
bool World::inOrder(int id) const { for (int o : cs_.order) if (o == id) return true; return false; }

void World::joinCombat(Entity& e) {
    if (e.kind != EntKind::Critter && e.kind != EntKind::Player) return;
    if (inOrder(e.id)) return; cs_.order.push_back(e.id); e.ap = 0;        // joins at the end of the round; its own turn gives it action points
}

void World::startCombat(int initiator) {
    if (cs_.active) return;
    { Entity* ie = find(initiator); if (!ie || ie->dead || ie->destroyed || !ie->active) return; }       // a deactivated entity's script cannot start a fight
    cs_ = CombatState(); cs_.active = true; cs_.round = 1; if (host.uiSound) host.uiSound("CombatSequenceStart.wav");
    Entity& pc = ents_[0]; std::vector<std::pair<int, int>> seq;              // (sequence, id)
    auto add = [&](Entity& e) { seq.push_back({2 * e.special[1] * 100 + (int)(rng()() % 100), e.id}); };
    add(pc);
    for (Entity& e : ents_) {
        if (e.kind != EntKind::Critter || e.dead || e.destroyed || !e.active) continue;
        bool near = std::hypot(e.x - pc.x, e.z - pc.z) < 40.f;
        if ((near && (e.team == pc.team || enemies(e, pc))) || e.id == initiator) add(e);
    }
    std::sort(seq.begin(), seq.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    for (auto& s : seq) cs_.order.push_back(s.second);
    for (size_t i = 0; i < cs_.order.size(); i++) if (cs_.order[i] == initiator) { std::rotate(cs_.order.begin(), cs_.order.begin() + (long)i, cs_.order.end()); break; }     // whoever starts it acts first
    cs_.cur = 0; log.push_back("Combat begins."); if (std::getenv("VB_DEBUG_COMBAT")) { Entity* ie = find(initiator); std::fprintf(stderr, "combat started by %s (%d), %zu participants\n", ie ? ie->instance.c_str() : "?", initiator, cs_.order.size()); } startTurn();
}

void World::startTurn() {
    Entity* e = find(cs_.order[cs_.cur]); if (!e) return;
    e->ap = (float)maxApOf(*e); e->turnT = 0;
    if (e->kind == EntKind::Player) log.push_back("Your turn.");
}

void World::endTurn() {
    if (!cs_.active || cs_.order.empty()) return;
    if (Entity* e = find(cs_.order[cs_.cur])) { e->moving = false; e->path.clear(); e->ap = 0; if (e->kind != EntKind::Player) e->anim = "idle"; }
    for (size_t guard = 0; guard <= cs_.order.size(); guard++) {
        if (++cs_.cur >= cs_.order.size()) { cs_.cur = 0; cs_.round++; }
        Entity* n = find(cs_.order[cs_.cur]); if (n && !n->dead && !n->destroyed && n->active) break;
    }
    startTurn();
}

void World::endCombat() {
    if (!cs_.active) return; cs_ = CombatState(); for (Entity& e : ents_) e.ap = 0; if (host.uiSound) host.uiSound("CombatSequenceEnd.wav");
    log.push_back("Combat ends.");
}

Entity* World::pickTarget(Entity& e) {
    auto valid = [&](Entity* t) { return t && t != &e && !t->dead && !t->destroyed && t->active && (t->kind == EntKind::Critter || t->kind == EntKind::Player); };
    if (e.aggro) { Entity* t = find(e.aggro); if (valid(t)) return t; e.aggro = 0; }
    Entity* best = nullptr; float bd = 1e30f;
    for (int id : cs_.order) { Entity* t = find(id); if (!valid(t) || !enemies(e, *t)) continue; float d = std::hypot(t->x - e.x, t->z - e.z); if (d < bd) { bd = d; best = t; } }
    return best;
}

// the engine plays an enemy's / ally's turn: close in (running, paying action points per step) until in range with a clear line, attack while it can pay, then pass
void World::combatAi(Entity& e, float) {
    if (e.attackT > 0) return;                                          // wait for the last attack animation
    Entity* t = pickTarget(e); if (!t) { endTurn(); return; }
    WeaponStats ws = weaponStats(e.weapon); float dist = std::hypot(t->x - e.x, t->z - e.z);
    bool los = ws.melee || ws.fists || !host.lineOfSight || host.lineOfSight(e.x, e.z, t->x, t->z);
    if (dist <= ws.range * 0.9f && los) {
        if (e.moving) { e.moving = false; e.path.clear(); e.anim = "idle"; }
        if (e.ap + 1e-3f >= (float)ws.ap) tryAttack(e.id, t->id); else endTurn();
        return;
    }
    if (e.ap < 1.f) { endTurn(); return; }
    if (!e.moving) { if (!beginMove(e, t->x, t->z, true)) endTurn(); }
}

void World::updateCombat(float dt) {
    if (!cs_.active) return;
    Entity& pc = ents_[0]; bool anyEnemy = false;
    for (int id : cs_.order) { Entity* e = find(id); if (e && e->kind == EntKind::Critter && !e->dead && !e->destroyed && e->active && (enemies(*e, pc) || e->aggro == 1)) anyEnemy = true; }
    if (!anyEnemy) { if (std::getenv("VB_DEBUG_COMBAT")) for (int id : cs_.order) { Entity* e = find(id); if (e) std::fprintf(stderr, "  end: %s team %d dead %d active %d enemyOfPc %d\n", e->instance.c_str(), e->team, (int)e->dead, (int)e->active, (int)enemies(*e, pc)); } endCombat(); return; }
    Entity* cur = find(cs_.order[cs_.cur]);
    if (!cur || cur->dead || cur->destroyed || !cur->active) { endTurn(); return; }
    if (cur->kind == EntKind::Player) { if (autoEndPlayerTurn) endTurn(); return; }
    cur->turnT += dt; if (cur->turnT > 12.f) { endTurn(); return; }
    combatAi(*cur, dt);
}

bool World::reload() {
    int h = activeHand(); if (h < 0) return false;
    if (cs_.active) { if (!playerTurn()) { log.push_back("It is not your turn."); return false; } if (ents_[0].ap + 1e-3f < (float)kReloadAp) { log.push_back("Not enough action points."); return false; } }
    WeaponStats ws = weaponStats(equipped[h]); if (ws.melee) return false;
    // matching ammo: the weapon name's caliber token (the word with a digit, "9mm", ".45") must appear in the ammo's name; weapons without one take energy cells / fuel
    std::string wn = lower(ws.name), tok; { std::string cur; for (char c : wn + " ") { if (c == ' ') { bool dig = false; for (char d : cur) if (std::isdigit((unsigned char)d)) dig = true; if (dig && tok.empty()) tok = cur; cur.clear(); } else cur.push_back(c); } }
    for (size_t i = 0; i < bag.size(); i++) {
        ItemInfo inf = itemInfo(bag[i].file); if (inf.kind != "ammo") continue; std::string an = lower(inf.name);
        bool ok = tok.empty() ? (an.find("cell") != std::string::npos || an.find("energy") != std::string::npos || an.find("fuel") != std::string::npos || an.find("naph") != std::string::npos) : an.find(tok) != std::string::npos;
        if (!ok) continue;
        if (--bag[i].count <= 0) bag.erase(bag.begin() + (long)i);
        if (cs_.active) ents_[0].ap -= (float)kReloadAp;
        pclip_[h - SlotHand1] = ws.clip; log.push_back("You reload the " + ws.name + "."); return true;
    }
    log.push_back("You have no ammo for the " + ws.name + "."); return false;
}

bool World::useItem(int idx) {
    if (idx < 0 || idx >= (int)bag.size()) return false;
    ItemInfo inf = itemInfo(bag[(size_t)idx].file); if (!isUsable(inf)) return false;
    if (cs_.active) { if (!playerTurn()) { log.push_back("It is not your turn."); return false; } if (ents_[0].ap + 1e-3f < (float)kReloadAp) { log.push_back("Not enough action points."); return false; } }
    Entity& p = ents_[0]; std::string n = lower(inf.name);
    if (n.find("aid") != std::string::npos || n.find("stim") != std::string::npos || n.find("heal") != std::string::npos) {
        if (cs_.active) p.ap -= (float)kReloadAp; int before = p.hp; p.hp = std::min(p.maxHp, p.hp + 25); if (--bag[(size_t)idx].count <= 0) bag.erase(bag.begin() + idx);
        { FloatText f; f.ent = p.id; f.ttl = 1.4f; f.speech = false; f.x = p.x; f.y = p.y; f.z = p.z; f.text = "+" + std::to_string(p.hp - before); f.rgb = 0x40ff40; floats.push_back(f); }       // health gained is green
        log.push_back("You use the " + inf.name + " and recover " + std::to_string(p.hp - before) + " hit points."); return true;
    }
    log.push_back("Nothing happens."); return false;
}

} // namespace vb

#pragma once
// Game world runtime: the entities of the loaded map, their scripts (Pawn/AMX, see assets/amx.h and AMX_FORMAT_FINDINGS.md), the natives scripts call into the engine, the
// action queues that move people about, messages, trigger zones, the dialogue state machine and the quest journal. No graphics: the host (vanburen, the headless
// vb_world_sim) feeds it the map and a few services (path finding, ground height, line of sight) and reads entity state back to draw it.
//
// Script protocol (decoded from the shipped scripts and their debug symbols, see GAME_DATA_FINDINGS.md "Script runtime"):
//   entity scripts export OnCreate(time) OnLoad(time) OnUpdate(time) OnDeath(time) OnDestroy(time) OnDamage(time, source, amount, type) OnAttacked(attacker, target, hit, location, volume)
//   OnMessage(sender, id) OnOpen(entity) / OnClose(entity) (doors, containers; return non-zero to allow) OnEnter(entity, time) / OnLeave(entity, time) (trigger zones)
//   GetUsableSkills(params[]) UseSkill(entity, id). Times are game milliseconds; angles are integer degrees; distances integer world units.
//   dialogue scripts (`Scripts/<entity display name>.sma`): OnStartDialogue(), node functions N<k> (SetCurrentNodeName, SetNodeTextFunction, RegisterNodeText), the node text function
//   (NU<k>_N<n>: RegisterReply(index, strref, actionFn, linkFn)); the reply's action function runs, then its link function, which calls the next node function.
#include "assets/amx.h"
#include "assets/map.h"
#include "assets/stf.h"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace vb {

enum class EntKind { Player, Critter, Door, Container, Item, Prop, Trigger, MapScript, Usable };       // Usable: a `.use` template (computer, generator, vending machine ...): scenery you use through skills, not an item to pick up

struct Action {
    enum Type { Wait, MoveToPos, MoveToEnt, Wander, PlayAnim, Attack, Speech, Kill, Send } type = Wait;
    float x = 0, z = 0, x2 = 0, z2 = 0;      // target position / wander rectangle
    int target = 0, run = 0, a = 0, b = 0;
    float seconds = 0;
    std::string text;
};

struct Entity {
    int id = 0;
    EntKind kind = EntKind::Prop;
    std::string file, instance, display, scriptName, dialogue;     // dialogue: name of the .sma (entity display name) if it has one
    int mapIdx = -1;                    // index in Map::entities (-1 for the player / zones)
    float x = 0, y = 0, z = 0, yaw = 0; // yaw radians, 0 looks down +Z
    int hp = 40, maxHp = 40;
    bool autoOpened = false; float autoCloseT = 0;      // a door an NPC opened walking through it, and how long it has been clear
    bool brokenRobot = false;           // lies broken (hit points below maximum) until repaired: the eye bot
    bool perceives = false;             // the protagonist is within perception range (the perception message is sent when this turns on)
    bool dead = false, active = true, destroyed = false, locked = false, open = false;
    int team = 0, squad = 0, teamRank = 0, squadRank = 0, race = 0, gender = 0;   // race 5 = robot; gender 0 male / 1 female
    int special[7] = {5, 5, 5, 5, 5, 5, 5};
    std::unordered_map<int, int> values;
    std::shared_ptr<AmxScript> script;
    std::unique_ptr<AmxVM> vm;
    // current action + queue
    std::deque<Action> queue; Action cur; bool hasCur = false; float actionT = 0;
    std::vector<std::pair<float, float>> path; size_t pathNext = 0;
    bool moving = false, running = false;
    float speed = 9.f;      // = kWalkSpeed
    std::string modelOverride;          // set by SetModel (e.g. the busted barrel)
    std::string anim = "idle";          // what the host should play: idle walk run death (+ a requested animation name)
    float openAmt = 0;                  // doors: 0 closed .. 1 open (animated by the world)
    float nextUpdate = 0;
    int soundCounter = 0;
    std::vector<float> zone;            // trigger zones: polygon x,z pairs
    int zoneKind = -1;                  // 0 map transition, 1 script zone, 6 boundary
    std::string zoneTarget; int zoneEntry = 0; int zoneId = 0;
    std::set<int> inside;               // entities currently inside the zone
    // combat: weapon a critter uses (template file of the first weapon in its inventory, "" = fists / natural attacks); timers in seconds; attackKind 0 fists / claws, 1 gun, 2 melee weapon
    std::string weapon; float cooldown = 0, attackT = 0, hitT = 0; int attackKind = 0;
    int shotSerial = 0, shotTarget = 0; bool shotHit = false; float attackDur = 0.7f;      // attackDur: how long the attack animation runs (attackT counts down from it)        // the last attack this entity made (the host plays its effects once per serial)
    float ap = 0, turnT = 0; int aggro = 0;        // turn-based combat: action points left this turn, seconds into the turn, who the AI script last told it to attack
    std::vector<std::string> cargo;     // template files of what a container / critter carries (from the map; taken items leave it)
};

// An item the protagonist carries (or a container holds): the template file ("Weapons9mm_Autoloader.WEA") and a stack count.
struct InvItem { std::string file; int count = 1; };
// What the interface shows about an item template.
struct ItemInfo {
    std::string file, name, desc, kind, icon, actIcon, model, heldModel;   // kind: weapon armor ammo use item; icon: inventory icon (.dds name, resolved by stem like textures)
    int slot = -1;                              // equipment slot it fits: 0 head, 1 body, 2 / 3 hands (a weapon fits either); -1 not equippable
    bool stack = false;                         // identical items share a slot (ammo, consumables, misc)
    std::string skill;                          // the skill a kit in the hand serves ("Security" for the Security Kit, "Mechanics" for the Tool Kit), "" for other items
};
// What a weapon does (decoded partly from the item template, partly assumed; see GAME_DATA_FINDINGS.md "Combat").
struct WeaponStats {
    std::string file, name, effect, heldEffect, heldModel;      // effect: the attack effect file of the first attack mode; heldModel: the model in the hand (WP_x)
    bool fists = true, melee = false, twoHanded = false; int damage = 3, clip = 0, ap = 3, dmgType = 0; float range = 2.2f, cooldown = 0.9f;       // dmgType: 0 normal, 2 electrical, 4 melee, 5 energy / fire
};       // ap: action points one attack costs
enum class AttackResult { Fired, Cooldown, OutOfRange, NoLineOfSight, NoAmmo, NotYourTurn, NoAp, Invalid };
enum EquipSlot { SlotHead = 0, SlotBody = 1, SlotHand1 = 2, SlotHand2 = 3 };

// One entry of the menu a target offers when a skill is used on it: the target script's GetUsableSkills fills (string id, skill id) pairs.
struct SkillOption { int id = 0, strref = 0; std::string label; };

struct Veg { int id = 0, ent = 0; std::string name; };         // an effect file attached to an entity by a script (AttachVEG)
struct FloatText { float dy = 8.5f; int ent = 0; std::string text; unsigned rgb = 0xffffff; float t = 0, ttl = 4.f; bool speech = true; float x = 0, y = 0, z = 0; };
struct JournalTask { int id = 0; int state = 0; std::vector<int> entries; };

struct DialogueReply { int index = 0, strref = 0; std::string action, link, text; };
struct Dialogue {
    bool active = false; int npc = 0; std::string script;
    std::vector<std::string> npcText;           // what the NPC says in the current node (already resolved)
    std::vector<DialogueReply> replies;
    std::string nodeName, nodeFunc; int serial = 0;
};

// Services the host provides.
struct WorldHost {
    std::function<bool(float, float, float, float, std::vector<std::pair<float, float>>&)> findPath;   // sx sz gx gz -> waypoints
    std::function<bool(float, float, float, float)> lineOfSight;                                         // x0 z0 x1 z1 -> clear?
    std::function<float(int, const char*)> animSeconds;                                                 // entity id, pose ("laydown", "getup") -> length of that animation in seconds (0 if unknown)
    std::function<bool(float, float, float&)> groundY;                                                   // x z -> y (hint in/out)
    std::function<bool(float, float)> walkable;                                                          // x z
    std::function<bool(float, float)> underFog;                                                          // x z -> hidden by the fog of war (outside the light circle)?
    std::function<int(int, const std::string&, bool)> attachSound;                                       // AttachSound(entity, wav, loop) -> voice id (0 = none); the host plays it at the entity
    std::function<void(const char*)> uiSound;                                                            // a non-positional interface sound (wav file name): combat start / end
    std::function<void(int)> removeSound;                                                                // RemoveSound(voice id)
    std::function<void(const struct Entity&, bool)> doorToggled;                                         // a door opened/closed (the host updates collision)
};

struct MapChange { std::string map; int entry = 0; };

class World {
public:
    World(const std::string& assetsDir, const std::string& gameDir);
    WorldHost host;

    // (Re)build the world for a map. `mapFile` is the .map stem including its numeric prefix ("00_03_Tutorial_Junktown"; the map-level script has the same name).
    void resetGame();                              // forget everything that persists across maps (belongings, journal, globals, game time): back to the main menu
    void load(const Map& map, const std::string& mapName, const std::string& mapFileStem, float px, float py, float pz, float pyaw);
    void update(float dt);
    // the player's position is owned by the host (click-to-walk); keep the world informed
    void setPlayer(float x, float y, float z, float yaw);
    // true while a script drives the protagonist (A_MoveToPosition / A_Wait queued on it, e.g. the vault-door console); the host then follows player() instead of its own input
    bool playerScripted() const { const Entity& p = ents_[0]; return p.hasCur || !p.queue.empty(); }
    Entity* player() { return &ents_[0]; }
    Entity* find(int id) { return id >= 1 && id <= (int)ents_.size() ? &ents_[(size_t)id - 1] : nullptr; }
    size_t count() const { return ents_.size(); }
    Entity* byMapIdx(int idx) { return idx >= 0 && idx < (int)byMap_.size() ? find(byMap_[(size_t)idx]) : nullptr; }

    // interaction (the player clicked on an entity); returns what happened as a short log line, "" if nothing
    std::string use(int id);
    // names of the items a container (or a critter's inventory) holds, from the map's item list and the item templates
    std::vector<std::string> contents(int id);
    // the protagonist's belongings (kept across maps)
    std::vector<InvItem> bag;                      // backpack, in pick-up order
    std::string equipped[4];                       // template file worn / held per EquipSlot ("" = empty); equipped items are not in the bag
    ItemInfo itemInfo(const std::string& file);
    void addItem(const std::string& file, int count = 1);                 // into the backpack (stacks ammo / consumables)
    bool equip(int bagIdx, int slot);              // moves a backpack item into an equipment slot (what was there goes back to the bag); false + log line if it does not fit
    bool unequip(int slot);
    bool swapHands();
    int defaultSlot(const std::string& file);      // the slot a quick-equip would use, -1 if none
    int itemCount(const std::string& pattern, bool includeEquipped = true);     // how many items match a script's item reference
    std::vector<InvItem> cargoOf(int id);          // a container's / critter's items grouped by template
    int takeFromCargo(int id, const std::string& file, int count = 1 << 20);    // moves them into the backpack, returns how many
    int takeAll(int id);
    // ---- combat: turn based. It starts with the first attack (by or against the protagonist's side, or an enemy script's A_Attack); everyone nearby who is an ally or enemy of the protagonist
    // takes part and acts in sequence order (2 x perception) with 5 + agility / 2 action points per turn: an attack costs the weapon's AP, moving costs 1 AP per `kUnitsPerAp` world units, reloading
    // and using an item 2. Enemies and allies are played by the engine (target = what the AI script asked to attack, else the nearest enemy); the protagonist by the host.
    // walking / running speed in world units per second; the animations play in place at their own speed, and the tech demo's people cross the ground faster than the first guess (5.4 / 10): 9 / 16 after the user compared with the demo
    static constexpr float kWalkSpeed = 9.f, kRunSpeed = 16.f;
    static constexpr float kUnitsPerAp = 2.f; static constexpr int kReloadAp = 2;
    bool inCombat() const { return cs_.active; }
    bool playerTurn() const { return cs_.active && !cs_.order.empty() && cs_.order[cs_.cur] == 1; }
    int currentActor() const { return cs_.active && !cs_.order.empty() ? cs_.order[cs_.cur] : 0; }
    int combatRound() const { return cs_.round; }
    float playerAp() const { return const_cast<World*>(this)->ents_[0].ap; }
    int maxApOf(const Entity& e) const { return 5 + e.special[5] / 2; }
    bool spendPlayerAp(float ap) { Entity& p = ents_[0]; if (p.ap + 1e-3f < ap) return false; p.ap = std::max(0.f, p.ap - ap); return true; }
    void endTurn();                                // the protagonist ends their turn early (also called automatically when out of action points)
    bool autoEndPlayerTurn = false;                // headless tests: the protagonist passes every turn
    bool enemies(const Entity& a, const Entity& b);
    // A door without a script cannot be used by clicking it (the Junktown vault door only opens through its terminal's script); such a door is neither highlighted nor picked.
    bool usable(const Entity& e) const { return e.kind != EntKind::Door || e.vm != nullptr; }
    WeaponStats weaponStats(const std::string& file);
    std::string playerWeapon();                    // equipped weapon file, first hand first; "" = fists
    void selectHand(int h) { attackHand_ = h ? 1 : 0; }
    int selectedHand() const { return attackHand_; }
    int activeHand();                              // EquipSlot of that weapon, -1 for fists
    AttackResult tryAttack(int attacker, int target);
    bool reload();                                 // protagonist: refill the held weapon's clip from a matching ammo item in the backpack
    int clipOf(int hand) const { return pclip_[hand - SlotHand1]; }
    bool isUsable(const ItemInfo& i) const { std::string n = i.name; for (auto& c : n) c = (char)std::tolower((unsigned char)c); return i.kind == "use" || n.find("aid kit") != std::string::npos || n.find("stim") != std::string::npos; }
    bool useItem(int bagIdx);                      // consumables (first aid kits heal)
    // skills ("Use Skill" button, kits in the hands): the target's script lists what can be done to it ("Security - Unlock", "Mechanics - Repair"); `useSkill` checks the kit and runs the script's UseSkill
    std::vector<SkillOption> usableSkills(int target);
    std::string useSkill(int target, const SkillOption& opt);
    bool holdsKitFor(const std::string& skill);    // a kit of that skill is in one of the hands
    std::string nameOf(const Entity& e);           // display name of a critter without the editor's numeric prefix
    bool playerDead() const { return const_cast<World*>(this)->ents_[0].dead; }
    void revivePlayer() { Entity& p = ents_[0]; p.dead = false; impacts_.clear(); p.hp = p.maxHp; p.anim = "idle"; p.attackT = p.hitT = p.cooldown = 0; }
    bool hostile(int id);                          // a living critter whose team is an enemy of the protagonist's
    // dialogue
    bool startDialogue(int npc);
    void chooseReply(int index);
    void endDialogue();
    const Dialogue& dialogue() const { return dlg_; }
    const AmxVM* dialogueVm() const { return dvm_.get(); }
    std::vector<std::string> dialogueScripts() const { std::vector<std::string> v; for (auto& k : scriptIndex_) if (k.first.size() > 4 && k.first.compare(k.first.size() - 4, 4, ".sma") == 0) v.push_back(k.first.substr(0, k.first.size() - 4)); return v; }
    bool wantsDialogue(int id) const { const Entity* e = const_cast<World*>(this)->find(id); return e && !e->dead && !e->dialogue.empty(); }

    // outputs for the host
    std::map<std::string, bool> mapEffects;          // MapEffectSetActive: effect group -> on (groups a script never mentions keep the host's default)
    std::vector<Veg> vegs; int vegSerial_ = 1;      // attached effects: the host starts / stops them
    std::vector<FloatText> floats;
    std::vector<std::string> log;                  // HUD message log (DisplayStringRef etc.)
    std::vector<JournalTask> journal;
    std::vector<std::string> debug;                // script debug / unbound native notes (VB_SCRIPT_LOG)
    bool hasMapChange = false; MapChange mapChange;
    int openContainer = 0;                         // set when the player opens a container; the host shows its contents window and clears it
    int timeMs = 0;
    std::map<std::string, int> globals;
    StringTable stf;
    const std::string& stringRef(int id) const { return stf.text((uint32_t)id); }
    std::string playerName = "Stranger";             // <CHARNAME>
    std::string text(int strref) const;              // string table text with {cNNN} colour markup removed and <TOKENS> expanded (Engine/tokens.txt, player name, language filter words)
    const Map* map() const { return map_; }
    std::string mapName;
    // script stats for tests
    int vmErrors = 0; std::map<std::string, int> unboundNatives;

    // fire a public function on an entity script (host/test hook); false when it has none / errored
    bool callEntity(Entity& e, const std::string& fn, const std::vector<int32_t>& args, int32_t& ret);

private:
    std::string assets_, game_, scriptsDir_;
    void* elib_ = nullptr;                          // EntityLibrary (item/critter templates), shared by all maps
    std::map<std::string, int> tokens_;            // Engine/tokens.txt: NAME=string id
    std::deque<Entity> ents_;                      // id = index + 1; entity 1 is the protagonist
    std::vector<int> byMap_;
    const Map* map_ = nullptr;
    std::unordered_map<std::string, std::string> scriptIndex_;      // lowercase stem (and "stem.sma") -> path
    std::unordered_map<std::string, std::shared_ptr<AmxScript>> scripts_;
    struct Msg { int to, from, id; std::vector<int32_t> args; };
    std::deque<Msg> msgs_;
    std::vector<int32_t> curArgs_;
    std::map<std::pair<int, int>, int> teamAtt_, squadAtt_;
    bool starterGiven_ = false;
    void giveStarterKit();
    int attackHand_ = 0;                           // which hand's weapon the protagonist attacks with (0 / 1), chosen with the action bar's weapon buttons
    int pclip_[2] = {-1, -1};                      // rounds in the clip of each held weapon (-1: not loaded yet = full)
    struct Impact { int attacker = 0, target = 0; float delay = 0; bool hit = false; int dmg = 0; std::string line, killLine; FloatText f; };
    std::vector<Impact> impacts_;
    void updateImpacts(float dt);
    struct CombatState { bool active = false; std::vector<int> order; size_t cur = 0; int round = 1; } cs_;
    void startCombat(int initiator); void joinCombat(Entity& e); void endCombat(); void startTurn(); void updateCombat(float dt); void combatAi(Entity& e, float dt);
    Entity* pickTarget(Entity& e); bool inOrder(int id) const;
    int nextTeam_ = 100, nextSquad_ = 100, nextList_ = 1, nextSound_ = 1;
    std::map<int, std::vector<int>> lists_;
    // dialogue VM
    Dialogue dlg_; std::unique_ptr<AmxVM> dvm_; std::shared_ptr<AmxScript> dscript_; Entity* dlgNpc_ = nullptr;
    bool pendingDialogue_ = false, pendingDialogueWalking_ = false; int pendingDialogueNpc_ = 0; float pendingDialogueT_ = 0;
    int dlgNodeSerialSeen_ = 0;
    float clock0_ = 13 * 3600 + 13 * 60 + 13;

    std::shared_ptr<AmxScript> script(const std::string& stem, const char* ext);
    void attachScript(Entity& e, const std::string& stem);
    void bindNatives(AmxVM& vm, bool dialogueVm);
    void dispatch(Entity& e, float dt);
    void stepAction(Entity& e, float dt);
    void startAction(Entity& e);
    bool beginMove(Entity& e, float gx, float gz, bool run);
    void deliver();
    void updateZones(float dt);
    void runDialogueNode();
    void damage(Entity& target, int amount, int source);
    void setDoor(Entity& e, bool open);
    bool run(Entity& e, const std::string& fn, const std::vector<int32_t>& args, int32_t& ret);
    void say(Entity& e, int strref, float seconds, unsigned rgb);
    int stuffId(Entity* e) const { return e ? e->id : 0; }
    Entity* self(AmxVM& vm) { return (Entity*)vm.user; }
    static int degOf(float dx, float dz);
    friend struct NativeBinder;
};

} // namespace vb

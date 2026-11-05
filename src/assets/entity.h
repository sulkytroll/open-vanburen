#pragma once
// Entity template files (.critter/.door/.item/.weapon/.armor/.ammo/.use/.con) and the lookup that turns a placed map entity
// into the G3D model(s) to draw. Layout: MAP_FORMAT_FINDINGS.md "Entity templates".
#include "map.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vb {

struct EntityTemplate {
    std::string path, kind;              // kind: critter door item weapon armor ammo use con
    std::string model;                   // G3D base name ("" for the player critters)
    std::vector<std::string> icons;      // inventory / action icons (.dds names), items/weapons/armor
    MapEntity defaults;                  // the template's own EEOV (default skin texture, script, ...)
    // critter (GCRE)
    std::string portrait, armor;
    // critter stats (GCRE words, verified over all 100+ critter templates: SPECIAL is 7 words, then race/age/gender/team) and hit points (GENT's last two words)
    bool hasStats = false;
    int special[7] = {5, 5, 5, 5, 5, 5, 5};   // strength perception endurance charisma intelligence agility luck
    int race = 0, age = 0, gender = 0, team = 0;   // race 5 = robot, 3 = animal; gender 0 male / 1 female; team 1 = the player's friends, 2 = hostile insurgents, 3.. other factions
    int hp = 0, maxHp = 0;
    int nameStr = 0, descStr = 0;             // GENT: string table ids
    // worn body parts in file order: mesh suffix ("HaiBald", "BodPrisonSuit", ...) + optional texture override ("BacPrisonSuit_13_LG.dds")
    std::vector<std::pair<std::string, std::string>> wear;
    // armor (GITM): body-part mesh suffixes worn when equipped (Bod*, Han*, Fee*, Sho*, Hea*, ...)
    std::vector<std::string> parts;
    // weapons (GIWP, partly decoded: heuristics, see GAME_DATA_FINDINGS.md "Combat"): class word, clip size, and the first attack mode's damage / range. 0 = not set (many shipped weapons are unfinished stubs)
    int wClass = -1, wClip = 0, wDamage = 0; float wRange = 0;
    // attack modes (the `GWAM` blocks inside GIWP): kind 2 single shot / 1 burst / 6 slash / 8 thrust / 9 throw, damage type (0 normal, 2 electrical, 4 melee, 5 energy / fire), shots, damage, mode caption string id, and the effect file played
    struct AttackMode { int kind = 0, dmgType = 0, shots = 1, damage = 0, nameStr = 0; float range = 0; std::string effect; };
    std::vector<AttackMode> modes;
    std::string heldEffect;                  // an effect that runs while the weapon is held (the flamethrower's pilot flame)
    std::string heldModel;                   // the model shown in the hand (`WP_x`, GITM)
    // armor (GIAR): first word, the normal-damage resistance (power armor 32, helmet 16, leather 4)
    int armorRating = 0;
};

// Throws std::runtime_error if the chunk structure is not fully consumed.
EntityTemplate loadEntityTemplate(const std::string& path);

// One mesh of a drawable entity, with an optional diffuse texture override (a .dds/.tga name).
struct ModelPart { std::string g3d_path, texture; std::vector<std::string> hideGroups; };   // hideGroups: material groups not to draw (body skin under clothes)

class EntityLibrary {
public:
    explicit EntityLibrary(const std::string& assetsRoot);
    // Template for a map entity file such as "Critters00_TMBobby.CRT" (case-insensitive); nullptr if unknown.
    const EntityTemplate* tmpl(const std::string& entityFile);
    // Path of a G3D by base name ("HumMNor", "GD_WP_50Revolver"); "" if absent.
    std::string g3dPath(const std::string& name) const;
    // Meshes for a human character built from parts: base body `model` (e.g. "HumMNor") with `skin` texture, the worn parts (hair, beard, clothing, ...), and an optional armor file
    // (e.g. "ClothingPrison_Suit.ARM"); the covered body groups are hidden.
    std::vector<ModelPart> assemble(const std::string& model, const std::string& skin, const std::vector<std::pair<std::string, std::string>>& wear, const std::string& armorFile);
    // The protagonist (`Default_PC.CRT`): the template's own clothes, or the worn armor instead. `bodyArmor` replaces the template's outfit, `headArmor` replaces the hair;
    // either may be "" (nothing equipped).
    std::vector<ModelPart> assemblePlayer(const std::string& file, const std::string& bodyArmor, const std::string& headArmor);
    // The meshes to draw for a placed entity (human critters are assembled from body + hair + armor parts).
    std::vector<ModelPart> parts(const MapEntity& e);
private:
    std::string root_;
    std::map<std::string, std::string> g3d_, templates_;      // lowercase name -> path
    std::map<std::string, std::unique_ptr<EntityTemplate>> cache_;
    std::map<std::string, std::vector<std::string>> hideCache_;
};

} // namespace vb

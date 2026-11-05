#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

// Lump types (from Van-Buren-Explorer LumpType.cs). Values are the numeric
// codes stored in resource.rht entries.
enum class LumpType : int32_t {
    G3D = 100, TGA = 200, SMA = 300, TRE = 400, SKEL = 600, ANIM = 700,
    INI = 800, WAV = 1100, SCR = 1200, MAP = 1300, CRITTER = 1400, VFX = 1500,
    GUI = 1600, COL = 1700, ITEM = 1800, SND_CONF = 1900, SND_LST = 2000,
    SND_LST_2 = 2100, SND_TXT = 2200, SND_TXT_2 = 2300, WEAPON = 2400,
    ARMOR = 2500, DOOR = 2600, USE = 2700, AMMO = 3500, CON = 3600, TXT = 3700,
    Unknown = 0
};

// One named resource: lives in a .grp archive (group + index), typed by the
// RHT catalog.
struct RhtEntry {
    int32_t  version      = 0;
    int32_t  index        = 0;   // lump index inside <group>.grp
    int32_t  type         = 0;   // LumpType code
    int32_t  nameOff      = 0;   // offset into resource-name pool
    int32_t  groupNameOff = 0;   // offset into group-name pool
    std::string name;
    std::string group;
};

} // namespace vb

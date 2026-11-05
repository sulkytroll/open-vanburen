#pragma once
// Van Buren map file (".map", lump type 1300): EMAP chunk stream. Layout: MAP_FORMAT_FINDINGS.md.
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

class BinReader;

struct MapEntity {                  // EME2 + EEOV
    std::string file;               // entity template (.DOR/.CRT/.ITM/...), e.g. "DS_DoorsOuter_Entrance.DOR"
    float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0};   // rot in radians
    uint8_t flag = 0;
    std::string instance, display, script, skin;
    std::vector<std::pair<std::string, uint32_t>> vars;
    std::vector<std::string> items;
};
struct MapEffect { std::string name; float pos[3], rot[3]; std::vector<std::string> effects; };   // EMEF
struct MapSound  { std::string name; float pos[3]; std::string sound; };                           // EMSD
struct MapTrigger {                                                                                 // EMTR + ETTR/ESTR/EBTR
    uint32_t type = 0;              // 0 map transition, 1 script, 6 boundary
    std::vector<float> verts;       // x,y,z triples
    std::string target;             // destination map (type 0) or .amx script (type 1)
    uint32_t id = 0;                // EBTR id / ESTR value
    uint8_t entry = 0;              // ETTR entry point id
};
struct MapEntry { uint8_t id = 0; float pos[6][3] = {}; float angle[6] = {}; };                     // EMEP
struct MapPath  { std::string name; std::vector<float> pts; };                                      // EPTH (x,y,z per point)
struct MapNavNode { uint8_t flag; float pos[3]; uint8_t links[5]; };                                // EMNP
struct MapNote  { float pos[3]; std::string icon; };                                                // EMNO

struct Map {
    uint32_t id = 0;
    std::string heightmap, rle, ambient_map;
    uint8_t ambient_rgb[3] = {0, 0, 0}, light_rgba[4] = {0, 0, 0, 0}, fog_rgb[3] = {0, 0, 0};
    uint8_t fog_enable = 0;
    float fog_params[3] = {0, 0, 0};
    float camera[4] = {0, 1000, 1000, 0};
    std::vector<MapEntity> entities;
    std::vector<MapEffect> effects;
    std::vector<MapSound> sounds;
    std::vector<MapTrigger> triggers;
    std::vector<MapEntry> entries;
    std::vector<MapPath> paths;
    std::vector<MapNavNode> nav;
    std::vector<MapNote> notes;
};

struct MapEntity;
/// Parse an EEOV (per-instance override) chunk payload; used by both map entities and entity templates.
void readEeov(BinReader& r, uint32_t ver, MapEntity& e);

Map loadMap(const std::string& path);   // throws std::runtime_error

} // namespace vb

// vb_extract — native asset extractor for the Van Buren tech demo.
//
// Reads resource.rht (the catalog) plus every <group>.grp archive and writes
// each named resource out to <out>/<group>/<name>.<ext>, using the type code
// from the catalog to pick the extension.
//
// Usage: vb_extract <demo_dir> <out_dir> [types...]
//   types: optional space-separated filter, e.g. TGA WAV MAP G3D

#include "assets/rht.h"
#include "assets/grp.h"
#include "assets/format.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string extFor(int32_t t) {
    switch (vb::LumpType(t)) {
        case vb::LumpType::G3D:    return "g3d";
        case vb::LumpType::TGA:    return "tga";
        case vb::LumpType::SMA:    return "sma";
        case vb::LumpType::TRE:    return "tre";
        case vb::LumpType::SKEL:   return "skel";
        case vb::LumpType::ANIM:   return "anim";
        case vb::LumpType::INI:    return "ini";
        case vb::LumpType::WAV:    return "wav";
        case vb::LumpType::SCR:    return "scr";
        case vb::LumpType::MAP:    return "map";
        case vb::LumpType::CRITTER:return "critter";
        case vb::LumpType::VFX:    return "vfx";
        case vb::LumpType::GUI:    return "gui";
        case vb::LumpType::COL:    return "col";
        case vb::LumpType::ITEM:   return "item";
        case vb::LumpType::SND_CONF:return "sndconf";
        case vb::LumpType::SND_LST:return "sndlst";
        case vb::LumpType::SND_LST_2:return "sndlst2";
        case vb::LumpType::SND_TXT:return "sndtxt";
        case vb::LumpType::SND_TXT_2:return "sndtxt2";
        case vb::LumpType::WEAPON: return "weapon";
        case vb::LumpType::ARMOR:  return "armor";
        case vb::LumpType::DOOR:   return "door";
        case vb::LumpType::USE:    return "use";
        case vb::LumpType::AMMO:   return "ammo";
        case vb::LumpType::CON:    return "con";
        case vb::LumpType::TXT:    return "txt";
        default:                   return "bin";
    }
}

// The RHT stores type names like "TGA"; keep a set of those for filtering.
static std::string typeName(int32_t t) {
    switch (vb::LumpType(t)) {
        case vb::LumpType::G3D: return "G3D";   case vb::LumpType::TGA: return "TGA";
        case vb::LumpType::SMA: return "SMA";   case vb::LumpType::TRE: return "TRE";
        case vb::LumpType::SKEL:return "SKEL";  case vb::LumpType::ANIM:return "ANIM";
        case vb::LumpType::INI: return "INI";   case vb::LumpType::WAV: return "WAV";
        case vb::LumpType::SCR: return "SCR";   case vb::LumpType::MAP: return "MAP";
        case vb::LumpType::CRITTER:return "CRITTER"; case vb::LumpType::VFX:return "VFX";
        case vb::LumpType::GUI: return "GUI";   case vb::LumpType::COL: return "COL";
        case vb::LumpType::ITEM:return "ITEM";  case vb::LumpType::SND_CONF:return "SND_CONF";
        case vb::LumpType::SND_LST:return "SND_LST"; case vb::LumpType::SND_LST_2:return "SND_LST_2";
        case vb::LumpType::SND_TXT:return "SND_TXT"; case vb::LumpType::SND_TXT_2:return "SND_TXT_2";
        case vb::LumpType::WEAPON:return "WEAPON"; case vb::LumpType::ARMOR:return "ARMOR";
        case vb::LumpType::DOOR: return "DOOR";  case vb::LumpType::USE: return "USE";
        case vb::LumpType::AMMO: return "AMMO";  case vb::LumpType::CON: return "CON";
        case vb::LumpType::TXT: return "TXT";   default: return "UNKNOWN";
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <demo_dir> <out_dir> [types...]\n";
        return 1;
    }
    fs::path demo = argv[1];
    fs::path out  = argv[2];

    std::set<std::string> filter;
    for (int i = 3; i < argc; ++i) filter.insert(argv[i]);

    auto rht = vb::Rht::load(demo / "resource.rht");
    std::cout << "catalog: " << rht.entries().size()
              << " resources, " << rht.groups().size() << " groups\n";

    // Group the entries by group name.
    std::map<std::string, std::vector<const vb::RhtEntry*>> byGroup;
    for (auto& e : rht.entries()) byGroup[e.group].push_back(&e);

    fs::create_directories(out);
    long long written = 0, bytes = 0;
    std::map<std::string, int> perType;
    std::vector<std::string> errors;

    for (auto& [group, entries] : byGroup) {
        fs::path grpPath = demo / "data" / (group + ".grp");
        if (!fs::exists(grpPath)) {
            // Group with no archive (e.g. empty template groups) — skip.
            continue;
        }
        vb::Grp grp;
        try { grp = vb::Grp::load(grpPath.string()); }
        catch (const std::exception& ex) { // NOLINT
            errors.push_back(std::string(group) + ": " + ex.what());
            continue;
        }
        fs::path dir = out / group;
        fs::create_directories(dir);
        for (auto* e : entries) {
            if (!filter.empty() && !filter.count(typeName(e->type))) continue;
            std::vector<uint8_t> blob;
            if (!grp.read(e->index, blob)) {
                errors.push_back(group + "/" + e->name + " bad lump");
                continue;
            }
            std::string fn = e->name + "." + extFor(e->type);
            // Sanitize: names here are already clean, but be safe.
            for (auto& c : fn) if (c == '/' || c == '\\') c = '_';
            fs::path dest = dir / fn;
            { std::ofstream o(dest, std::ios::binary);
              o.write(reinterpret_cast<const char*>(blob.data()), blob.size()); }
            ++written;
            bytes += blob.size();
            ++perType[typeName(e->type)];
        }
    }

    std::cout << "wrote " << written << " files ("
              << (bytes / 1048576.0) << " MB) to " << out << "\n";
    std::cout << "by type:\n";
    for (auto& [t, n] : perType) std::cout << "  " << t << ": " << n << "\n";
    if (!errors.empty()) {
        std::cout << "warnings (" << errors.size() << "):\n";
        for (auto& e : errors) std::cout << "  " << e << "\n";
    }
    return errors.size() > 20 ? 2 : 0;
}

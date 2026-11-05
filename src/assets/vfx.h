#pragma once
// Effect files (Effects/*.vfx, "VEG V1.1"): a group of effects, each a self-describing property bag. Layout and field meanings: VFX_FORMAT_FINDINGS.md.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vb {

struct VfxEffect {
    std::string type;                                   // "particlesystem" "model" "sprite" "sound" "light" "lightning" "trail" "collision" "rain" "snow" "wind" "modeleffect"
    std::map<std::string, std::pair<std::string, std::vector<uint8_t>>> props;     // name -> (type string, raw value)
    bool has(const std::string& k) const { return props.count(k) != 0; }
    float f(const std::string& k, float def = 0.f) const;
    int i(const std::string& k, int def = 0) const;       // int, byte and triggered-effect values (-1 = none)
    bool b(const std::string& k, bool def = false) const;
    std::string s(const std::string& k) const;            // string / enum value, enums without their prefix up to the first '_' ("RP_BLEND_ADD" stays whole; callers compare whole names)
    std::array<float, 3> v3(const std::string& k) const;  // vectors, rotations (degrees)
    std::array<float, 3> rgb(const std::string& k) const; // colours 0..1
    std::array<float, 4> uv(const std::string& k) const;  // texture rectangle u0 v0 u1 v1
    std::string res(const std::string& k) const;          // a resource's file name ("" if none)
    std::string name() const { return s("Name"); }
};

struct VfxFile { std::string path; std::vector<VfxEffect> effects; bool loop = false; float minStart = 0, maxStart = 0; };

// Throws std::runtime_error on a malformed file.
VfxFile loadVfx(const std::string& path);

} // namespace vb

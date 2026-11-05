#pragma once
// Granny2 container (.skel / .anim): header, sections (Oodle0-compressed), pointer relocations, and a generic decoder driven by the
// file's own type descriptors. Layout notes: GR2_FORMAT_FINDINGS.md.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vb {

// Generic decoded Granny value (struct / array / scalar). Struct members keep file order.
struct GVal {
    enum Kind { Null, Num, Str, Obj, Arr, Floats } kind = Null;
    double num = 0;
    std::string str;
    std::vector<std::pair<std::string, GVal>> members;   // Obj
    std::vector<GVal> items;                             // Arr
    std::vector<float> floats;                           // Floats: an array whose elements are single-Real32 structs ("Real32")
    const GVal& at(const char* name) const;              // Null value if absent
    int i() const { return (int)num; }
    float f() const { return (float)num; }
    size_t size() const { return kind == Floats ? floats.size() : items.size(); }
};

struct GrTransform {
    uint32_t flags = 0;
    float pos[3] = {0, 0, 0};
    float orient[4] = {0, 0, 0, 1};      // quaternion x y z w
    float ss[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   // scale/shear 3x3 (row-major as stored)
};

struct GrBone {
    std::string name;
    int parent = -1;
    GrTransform local;
    float inverseWorld[16] = {0};
};
struct GrSkeleton { std::string name; std::vector<GrBone> bones; };

// B-spline curve as stored in animations (Granny "Curve" with Real32 knots/controls). dim: 3 position, 4 orientation (quaternion), 9 scale/shear.
struct GrCurve {
    int degree = 0, dim = 0;
    std::vector<float> knots, controls;
    bool empty() const { return controls.empty(); }
    void sample(float t, float* out) const;              // evaluates the spline; orientation is normalised by the caller
};
struct GrTrack { std::string name; GrCurve position, orientation, scaleShear; };
struct GrAnimation {
    std::string name;
    float duration = 0, timeStep = 0;
    std::vector<GrTrack> tracks;                         // merged from all track groups
};

struct Gr2 {
    std::unique_ptr<GVal> root;                          // whole file info tree
    std::vector<GrSkeleton> skeletons;
    std::vector<GrAnimation> animations;
};

// Throws std::runtime_error on structural errors (bad magic, unsupported compression, corrupt stream).
Gr2 loadGr2(const std::string& path);

} // namespace vb

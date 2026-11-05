#pragma once
#include "format.h"
#include <string>
#include <vector>

namespace vb {

// resource.rht — the resource catalog that maps named resources to
// (group, index) locations inside .grp archives.
//
// Layout (all little-endian):
//   int32 Major, Minor, NumEntries, OffsetToGroupNames, OffsetToResourceNames
//   NumEntries * ( int32 Version, Index, Type, NameOff, GroupNameOff )
//   group-name pool  (null-terminated strings) at OffsetToGroupNames
//   resource-name pool (null-terminated) at OffsetToResourceNames
class Rht {
public:
    // Load and parse. Throws std::runtime_error on a malformed file.
    static Rht load(const std::string& path);

    const std::vector<RhtEntry>& entries() const { return entries_; }
    int major() const { return major_; }
    int minor() const { return minor_; }
    // Distinct group (i.e. .grp filename stem) names, in file order.
    const std::vector<std::string>& groups() const { return groups_; }

private:
    Rht() = default;
    int major_ = 0, minor_ = 0;
    std::vector<RhtEntry> entries_;
    std::vector<std::string> groups_;
};

} // namespace vb

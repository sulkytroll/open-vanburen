#pragma once
// String table ("English.stf"): all in-game text. 12-byte header u32 version(3), u32 1, u32 count; then `count` 16-byte records
// {u32 offset, u32 length, u32 file_size, u32 0}; the text follows contiguously (plain bytes, no terminator). Entity templates and scripts refer to strings by
// 1-BASED id (0 = none): name = text(GENT.name_id), `DisplayStringRef(id)`, `RegisterNodeText(id)`. Markup seen: "{c16711680}" colour prefix on NPC speech.
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

class StringTable {
public:
    bool load(const std::string& path, std::string& err);
    size_t size() const { return strings_.size(); }
    const std::string& text(uint32_t id1based) const { static const std::string empty; return id1based >= 1 && id1based <= strings_.size() ? strings_[id1based - 1] : empty; }
private:
    std::vector<std::string> strings_;
};

} // namespace vb

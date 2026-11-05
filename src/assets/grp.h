#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace vb {

// A .grp archive:  vMajor, vMinor, nFiles (int32), then
// nFiles * ( offset, length ) (int32), then the raw data.
struct GrpLump {
    int32_t offset = 0;
    int32_t length = 0;
};
class Grp {
public:
    static Grp load(const std::string& path);
    int32_t major() const { return major_; }
    int32_t minor() const { return minor_; }
    const std::vector<GrpLump>& lumps() const { return lumps_; }
    // Copy out the byte range for lump i. Returns false if out of range.
    bool read(int i, std::vector<uint8_t>& out) const;
    const std::string& path() const { return path_; }
private:
    int32_t major_ = 0, minor_ = 0;
    std::vector<GrpLump> lumps_;
    std::vector<uint8_t> data_;
    std::string path_;
};

} // namespace vb

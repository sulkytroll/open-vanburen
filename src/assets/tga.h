#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

// Decoded RGBA8 texture.
struct Texture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4, top-to-bottom
};

// Load a TGA image (the format used for all .tga assets in the demo).
// Supports 8/16/24/32-bit, top- and bottom-up, RGB and RGBA.
// Returns false (and sets err) on failure.
bool loadTga(const std::string& path, Texture& out, std::string& err);

} // namespace vb

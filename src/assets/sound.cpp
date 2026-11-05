#include "assets/vfs.h"
#include "assets/sound.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace vb {

bool parseSndConf(const std::string& path, SndPreset& out) {
    vfs::Stream f(path); if (!f) return false;
    out = SndPreset(); std::string line; SndSample* cur = nullptr; bool inBlock = false; std::string pending;
    while (std::getline(f, line)) {
        size_t c = line.find("//"); if (c != std::string::npos) line.resize(c);
        std::istringstream ls(line); std::string key; ls >> key; if (key.empty()) continue;
        if (key == "{") { inBlock = true; continue; }
        if (key == "}") { inBlock = false; cur = nullptr; continue; }
        float v = 0; ls >> v;
        if (!inBlock && key.find('.') != std::string::npos) { out.samples.push_back(SndSample()); cur = &out.samples.back(); cur->file = key; continue; }
        if (!inBlock) {
            if (key == "PlaySequencing") out.sequencing = (int)v; else if (key == "FullSoundRadius") out.fullRadius = v; else if (key == "NoSoundRadius") out.noRadius = v;
        } else if (cur) {
            if (key == "BaseVolume") cur->baseVolume = v; else if (key == "RandomVolume") cur->randomVolume = v; else if (key == "BasePitch") cur->basePitch = v;
            else if (key == "RandomPitch") cur->randomPitch = v; else if (key == "BaseOffset") cur->baseOffset = v; else if (key == "RandomOffset") cur->randomOffset = v;
        }
    }
    return !out.samples.empty();
}

bool loadWav(const std::string& path, Wav& out) {
    vfs::Stream f(path); if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(&d[8], "WAVE", 4)) return false;
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &d[o], 4); return v; }; auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, &d[o], 2); return v; };
    bool haveFmt = false; int bits = 0;
    for (size_t p = 12; p + 8 <= d.size();) {
        uint32_t len = u32(p + 4); size_t body = p + 8;
        if (!std::memcmp(&d[p], "fmt ", 4) && body + 16 <= d.size()) { if (u16(body) != 1) return false; out.channels = u16(body + 2); out.rate = (int)u32(body + 4); bits = u16(body + 14); haveFmt = true; }
        else if (!std::memcmp(&d[p], "data", 4) && haveFmt && bits == 16) { size_t n = std::min<size_t>(len, d.size() - body) / 2; out.pcm.resize(n); std::memcpy(out.pcm.data(), &d[body], n * 2); return out.channels >= 1 && out.channels <= 2; }
        p = body + len + (len & 1);
    }
    return false;
}

} // namespace vb

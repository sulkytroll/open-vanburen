// Test helper: decompress every compressed section of a Granny .skel/.anim file natively and dump them (for comparison with the emulator).
#include "assets/oodle0.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    uint32_t nsec; memcpy(&nsec, &d[32 + 16], 4); uint32_t soff; memcpy(&soff, &d[32 + 12], 4);
    for (uint32_t i = 0; i < nsec; i++) {
        uint32_t s[11]; memcpy(s, &d[32 + soff + i * 44], 44);
        if (s[0] != 1 || s[2] == 0) continue;
        std::vector<uint8_t> out(s[3] + 16);
        bool ok = vb::oodle0Decompress(&d[s[1]], s[2], s[5], s[6], s[3], out.data());
        std::printf("%u %d %u\n", i, (int)ok, s[3]);
        std::string name = std::string(argv[2]) + "." + std::to_string(i);
        std::ofstream o(name, std::ios::binary); o.write((const char*)out.data(), s[3]);
    }
}

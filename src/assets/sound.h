#pragma once
// Van Buren sound data: ".sndconf" preset files (text; the engine compiles them to the ".psf" names that maps and effects use) and plain PCM ".wav" samples.
// Format notes: GAME_DATA_FINDINGS.md "Sound".
#include <cstdint>
#include <string>
#include <vector>

namespace vb {

struct SndSample {
    std::string file;                                   // wav in Sounds/
    float baseVolume = 100, randomVolume = 0;           // percent; the played volume is base +- random
    float basePitch = 100, randomPitch = 0;             // percent
    float baseOffset = 0, randomOffset = 0;             // ms; delay before a looping preset plays its next sample (inferred, see the findings)
};
struct SndPreset {
    std::string name;
    int sequencing = 2;                                 // PlaySequencing: 0 / 1 / 2 (meaning inferred: 1 in order, 2 random, 0 as 1)
    float fullRadius = 40, noRadius = 60;               // world units: full volume inside, silent outside, linear in between
    std::vector<SndSample> samples;
};
struct Wav { int channels = 1, rate = 22050; std::vector<int16_t> pcm; };      // interleaved 16-bit

bool parseSndConf(const std::string& path, SndPreset& out);
bool loadWav(const std::string& path, Wav& out);        // 16-bit PCM only (all the shipped files are)

} // namespace vb

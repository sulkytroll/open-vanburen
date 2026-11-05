#pragma once
// OpenAL sound engine for the game: plays the presets (".psf" names in maps / effects = Sounds/<stem>.sndconf, or a bare wav) with 3D attenuation.
// Distance model: full volume inside the preset's FullSoundRadius, silent beyond NoSoundRadius, linear in between (AL_LINEAR_DISTANCE_CLAMPED).
#include "assets/vfs.h"
#include "assets/sound.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

struct Audio {
    struct Voice { int id = 0; ALuint src = 0; bool loop = false, positional = true; vb::SndPreset* preset = nullptr; size_t next = 0, lastSample = 0; float gap = -1.f; bool ended = false; float x = 0, y = 0, z = 0; };
    ALCdevice* dev = nullptr; ALCcontext* ctx = nullptr; bool ok = false, mute = false; std::string dir;
    std::map<std::string, std::string> files;                                   // lower-case name -> real file name in Sounds/
    std::map<std::string, std::unique_ptr<vb::SndPreset>> presets;
    std::map<std::string, std::pair<ALuint, ALuint>> buffers;                    // wav -> (stereo / native buffer, mono downmix)
    // Streamed music: the segments of one piece are queued one after another (gapless); after the last one playback jumps back to segment `mLoop`.
    std::vector<std::string> mSegs; size_t mLoop = 0, mCur = 0; std::unique_ptr<vb::vfs::Stream> mFile; size_t mLeft = 0; int mCh = 2, mRate = 44100; ALuint mSrc = 0, mBufs[4] = {0, 0, 0, 0}; bool mOn = false; std::string mName; float musicGain = 0.6f;
    std::vector<Voice> voices; int nextId = 0; std::mt19937 rng{12345}; float master = 1.f;

    bool init(const std::string& assets) {
        dir = assets + "/Sounds/"; mute = std::getenv("VB_NOSOUND") != nullptr; if (mute) return false;
        try { for (const std::string& n : vb::vfs::list(dir)) { std::string l = n; for (char& c : l) c = (char)std::tolower((unsigned char)c); files[l] = n; } } catch (...) { return false; }
        dev = alcOpenDevice(nullptr); if (!dev) { std::fprintf(stderr, "audio: no OpenAL device\n"); return false; }
        ctx = alcCreateContext(dev, nullptr); if (!ctx || !alcMakeContextCurrent(ctx)) { std::fprintf(stderr, "audio: no OpenAL context\n"); return false; }
        alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED); ok = true; return true;
    }
    void shutdown() { if (!ok) return; musicStop(); for (Voice& v : voices) { alSourceStop(v.src); alDeleteSources(1, &v.src); } voices.clear(); for (auto& b : buffers) { alDeleteBuffers(1, &b.second.first); if (b.second.second) alDeleteBuffers(1, &b.second.second); } buffers.clear();
        alcMakeContextCurrent(nullptr); alcDestroyContext(ctx); alcCloseDevice(dev); ok = false; }

    float frand() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }
    std::string findFile(const std::string& n) { std::string l = n; for (char& c : l) c = (char)std::tolower((unsigned char)c); auto it = files.find(l); return it == files.end() ? std::string() : it->second; }

    // "X.psf" -> Sounds/X.sndconf when it exists, else Sounds/X.wav as a one-sample preset; "X.wav" -> that sample (default radii 40 / 60 from the group files)
    vb::SndPreset* preset(const std::string& name) {
        auto it = presets.find(name); if (it != presets.end()) return it->second.get();
        std::unique_ptr<vb::SndPreset> p(new vb::SndPreset()); std::string stem = name; size_t dot = stem.rfind('.'); std::string ext = dot == std::string::npos ? "" : stem.substr(dot + 1); if (dot != std::string::npos) stem.resize(dot);
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        bool got = false;
        if (ext != "wav") { std::string f = findFile(stem + ".sndconf"); if (!f.empty() && vb::parseSndConf(dir + f, *p)) got = true; }
        if (!got) { std::string f = findFile(stem + ".wav"); if (f.empty()) { presets[name] = nullptr; return nullptr; } vb::SndSample s; s.file = f; p->samples.push_back(s); }
        p->name = name; vb::SndPreset* r = p.get(); presets[name] = std::move(p); return r;
    }
    ALuint buffer(const std::string& wav, bool mono) {
        auto it = buffers.find(wav);
        if (it == buffers.end()) {
            vb::Wav w; std::string f = findFile(wav); std::pair<ALuint, ALuint> bp{0, 0};
            if (!f.empty() && vb::loadWav(dir + f, w)) {
                alGenBuffers(1, &bp.first); alBufferData(bp.first, w.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, w.pcm.data(), (ALsizei)(w.pcm.size() * 2), w.rate);
                if (w.channels == 2) { std::vector<int16_t> m(w.pcm.size() / 2); for (size_t i = 0; i < m.size(); i++) m[i] = (int16_t)((w.pcm[i * 2] + w.pcm[i * 2 + 1]) / 2); alGenBuffers(1, &bp.second); alBufferData(bp.second, AL_FORMAT_MONO16, m.data(), (ALsizei)(m.size() * 2), w.rate); }
            } else std::fprintf(stderr, "audio: cannot load %s\n", wav.c_str());
            it = buffers.emplace(wav, bp).first;
        }
        return mono && it->second.second ? it->second.second : it->second.first;
    }

    void listener(float x, float y, float z, float fx, float fy, float fz, float ux, float uy, float uz) {
        if (!ok) return; alListener3f(AL_POSITION, x, y, z); float o[6] = {fx, fy, fz, ux, uy, uz}; alListenerfv(AL_ORIENTATION, o); alListenerf(AL_GAIN, master);
    }

    // Starts a preset (or wav). Looping presets play their samples one after another with the preset's gap (BaseOffset + random RandomOffset ms) between them until stop().
    // Returns a voice id (0 when nothing is played).
    int play(const std::string& name, float x, float y, float z, bool loop = false, bool positional = true, float volume = 1.f) {
        if (!ok || name.empty()) return 0; vb::SndPreset* p = preset(name); if (!p || p->samples.empty()) return 0;
        Voice v; v.id = ++nextId; v.loop = loop; v.positional = positional; v.preset = p; v.x = x; v.y = y; v.z = z; alGenSources(1, &v.src); if (alGetError() != AL_NO_ERROR) return 0;
        alSourcef(v.src, AL_GAIN, volume);
        if (positional) { alSourcei(v.src, AL_SOURCE_RELATIVE, AL_FALSE); alSourcef(v.src, AL_REFERENCE_DISTANCE, std::max(0.1f, p->fullRadius)); alSourcef(v.src, AL_MAX_DISTANCE, std::max(p->fullRadius + 0.1f, p->noRadius)); alSourcef(v.src, AL_ROLLOFF_FACTOR, 1.f); alSource3f(v.src, AL_POSITION, x, y, z); }
        else { alSourcei(v.src, AL_SOURCE_RELATIVE, AL_TRUE); alSource3f(v.src, AL_POSITION, 0, 0, 0); alSourcef(v.src, AL_ROLLOFF_FACTOR, 0.f); }
        voices.push_back(v); startSample(voices.back(), volume); return v.id;
    }
    void move(int id, float x, float y, float z) { for (Voice& v : voices) if (v.id == id && v.positional) { v.x = x; v.y = y; v.z = z; alSource3f(v.src, AL_POSITION, x, y, z); } }
    void stop(int id) { for (Voice& v : voices) if (v.id == id) { v.loop = false; v.gap = -1.f; alSourceStop(v.src); v.ended = true; } }

    void update(float dt) {
        if (!ok) return;
        if (mOn) {
            ALint done = 0; alGetSourcei(mSrc, AL_BUFFERS_PROCESSED, &done);
            while (done-- > 0) { ALuint b; alSourceUnqueueBuffers(mSrc, 1, &b); fillBuf(b, false); }
            ALint st; alGetSourcei(mSrc, AL_SOURCE_STATE, &st); if (st != AL_PLAYING) alSourcePlay(mSrc);           // (starved: restart)
        }
        for (Voice& v : voices) {
            ALint st = AL_STOPPED; alGetSourcei(v.src, AL_SOURCE_STATE, &st);
            if (st == AL_PLAYING || v.ended) continue;
            if (!v.loop) { v.ended = true; continue; }
            if (v.gap < 0.f) { const vb::SndSample& s = v.preset->samples[v.lastSample % v.preset->samples.size()]; v.gap = (s.baseOffset + frand() * s.randomOffset) / 1000.f; }
            v.gap -= dt; if (v.gap <= 0.f) { v.gap = -1.f; startSample(v, 1.f); }
        }
        for (size_t i = 0; i < voices.size();) if (voices[i].ended) { alDeleteSources(1, &voices[i].src); voices.erase(voices.begin() + (long)i); } else i++;
    }
    // Opens segment `i` for streaming: positions the file at its PCM data.
    bool openSeg(size_t i) {
        mFile.reset(); std::string f = findFile(mSegs[i]); if (f.empty()) return false; mFile = std::make_unique<vb::vfs::Stream>(dir + f); if (!*mFile) return false;
        char h[12]; mFile->read(h, 12); if (!mFile || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4)) return false;
        for (;;) {
            char c[8]; mFile->read(c, 8); if (!*mFile) return false; uint32_t len; std::memcpy(&len, c + 4, 4);
            if (!std::memcmp(c, "fmt ", 4)) { char b[16]; mFile->read(b, 16); uint16_t ch; uint32_t rate; std::memcpy(&ch, b + 2, 2); std::memcpy(&rate, b + 4, 4); mCh = ch == 1 ? 1 : 2; mRate = (int)rate; if (len > 16) mFile->seekg(len - 16 + (len & 1), std::ios::cur); }
            else if (!std::memcmp(c, "data", 4)) { mLeft = len; return true; }
            else mFile->seekg(len + (len & 1), std::ios::cur);
        }
    }
    // Plays the pieces' segments in order, then repeats from segment `loopFrom` (the .sndtxt2 "start" / "loop" rows).
    void musicStart(const std::string& name, const std::vector<std::string>& segs, size_t loopFrom) {
        if (!ok || segs.empty()) return; if (mOn && mName == name) return; musicStop();
        mName = name; mSegs = segs; mLoop = std::min(loopFrom, segs.size() - 1); mCur = 0; if (!openSeg(0)) return;
        alGenSources(1, &mSrc); alGenBuffers(4, mBufs); alSourcei(mSrc, AL_SOURCE_RELATIVE, AL_TRUE); alSource3f(mSrc, AL_POSITION, 0, 0, 0); alSourcef(mSrc, AL_ROLLOFF_FACTOR, 0.f); alSourcef(mSrc, AL_GAIN, musicGain);
        for (int i = 0; i < 4; i++) fillBuf(mBufs[i], true); alSourcePlay(mSrc); mOn = true;
    }
    // Piece "MA1" = Sounds/MA1_PS2.sndtxt2: the Song rows name the segment wavs (and the ending); the row marked "loop" is where playback repeats from.
    void musicPiece(const std::string& piece) {
        if (!ok) return; std::string f = findFile(piece + "_ps2.sndtxt2"); if (f.empty()) return; vb::vfs::Stream in(dir + f); std::string line; bool song = false; std::vector<std::string> segs; size_t loop = 0;
        while (std::getline(in, line)) {
            size_t c = line.find("//"); if (c != std::string::npos) line.erase(c); std::vector<std::string> t; { size_t i = 0; while (i < line.size()) { while (i < line.size() && std::isspace((unsigned char)line[i])) i++; size_t j = i; while (j < line.size() && !std::isspace((unsigned char)line[j])) j++; if (j > i) t.push_back(line.substr(i, j - i)); i = j; } }
            if (t.empty()) continue; if (t[0] == "Song") { song = true; continue; } if (!song || t[0] == "{" || t[0] == "}") continue;
            size_t k = 0; if (t[0] == "start") k = 1; else if (t[0] == "loop") { loop = segs.size(); k = 1; } if (k < t.size()) segs.push_back(t[k] + ".wav");
        }
        musicStart(piece, segs, loop);
    }
    void musicStop() { if (!mOn) return; alSourceStop(mSrc); ALint q = 0; alGetSourcei(mSrc, AL_BUFFERS_QUEUED, &q); while (q-- > 0) { ALuint b; alSourceUnqueueBuffers(mSrc, 1, &b); } alDeleteSources(1, &mSrc); alDeleteBuffers(4, mBufs); mFile.reset(); mOn = false; mName.clear(); }
    void stopAll() { for (Voice& v : voices) { v.loop = false; alSourceStop(v.src); v.ended = true; } }
    size_t active() const { return voices.size(); }

private:
    // Reads the next ~0.4 s of music into `b` and queues it (moving on to the next segment, or back to the loop segment, when one runs out).
    void fillBuf(ALuint b, bool first) {
        (void)first; std::vector<char> d(65536); size_t got = 0;
        while (got < d.size()) {
            if (mLeft == 0) { mCur = mCur + 1 < mSegs.size() ? mCur + 1 : mLoop; if (!openSeg(mCur)) break; }
            size_t n = std::min(d.size() - got, mLeft); mFile->read(d.data() + got, (std::streamsize)n); size_t r = (size_t)mFile->gcount(); got += r; mLeft -= r; if (r < n) mLeft = 0;
        }
        got &= ~(size_t)3; if (!got) return;
        alBufferData(b, mCh == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, d.data(), (ALsizei)got, mRate); alSourceQueueBuffers(mSrc, 1, &b);
    }
    void startSample(Voice& v, float volume) {
        vb::SndPreset& p = *v.preset; size_t idx = p.sequencing == 2 ? (size_t)(frand() * (float)p.samples.size()) % p.samples.size() : v.next++ % p.samples.size(); v.lastSample = idx;
        const vb::SndSample& s = p.samples[idx]; ALuint b = buffer(s.file, v.positional); if (!b) { v.ended = true; return; }
        float vol = std::max(0.f, (s.baseVolume + (frand() * 2.f - 1.f) * s.randomVolume) / 100.f), pitch = std::max(0.1f, (s.basePitch + (frand() * 2.f - 1.f) * s.randomPitch) / 100.f);
        alSourceStop(v.src); alSourcei(v.src, AL_BUFFER, (ALint)b); alSourcef(v.src, AL_GAIN, vol * volume); alSourcef(v.src, AL_PITCH, pitch); alSourcePlay(v.src);
    }
};

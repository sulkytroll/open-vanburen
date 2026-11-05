#include "assets/vfs.h"
#include "assets/format.h"
#include "assets/rht.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <unordered_map>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace vb {
namespace vfs {

namespace {

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string norm(const std::string& p) {
    std::string q = p; for (char& c : q) if (c == '\\') c = '/';
    std::string n = fs::path(q).lexically_normal().generic_string();
    while (n.size() > 1 && n.back() == '/') n.pop_back();
    return n;
}

// the file extension the extractor gives each catalog type (the loaders ask for "name.ext")
const char* lumpExt(int32_t t) {
    switch (LumpType(t)) {
        case LumpType::G3D: return "g3d";       case LumpType::TGA: return "tga";       case LumpType::SMA: return "sma";
        case LumpType::TRE: return "tre";       case LumpType::SKEL: return "skel";     case LumpType::ANIM: return "anim";
        case LumpType::INI: return "ini";       case LumpType::WAV: return "wav";       case LumpType::SCR: return "scr";
        case LumpType::MAP: return "map";       case LumpType::CRITTER: return "critter"; case LumpType::VFX: return "vfx";
        case LumpType::GUI: return "gui";       case LumpType::COL: return "col";       case LumpType::ITEM: return "item";
        case LumpType::SND_CONF: return "sndconf"; case LumpType::SND_LST: return "sndlst"; case LumpType::SND_LST_2: return "sndlst2";
        case LumpType::SND_TXT: return "sndtxt"; case LumpType::SND_TXT_2: return "sndtxt2"; case LumpType::WEAPON: return "weapon";
        case LumpType::ARMOR: return "armor";   case LumpType::DOOR: return "door";    case LumpType::USE: return "use";
        case LumpType::AMMO: return "ammo";     case LumpType::CON: return "con";       case LumpType::TXT: return "txt";
        default: return "bin";
    }
}

struct Map {                         // a memory-mapped .grp
    const uint8_t* base = nullptr; size_t size = 0;
};
struct Entry { int grp = 0; uint32_t off = 0, len = 0; std::string group, file; };

struct Mount {
    std::string root;
    std::vector<Map> grps;
    std::unordered_map<std::string, Entry> index;                   // lower("Group/name.ext")
    std::map<std::string, std::vector<std::string>> groups;         // group (as the catalog spells it) -> file names
    std::unordered_map<std::string, std::string> overrides;         // lower(path inside Override) and lower(file name) -> real file
    std::vector<std::string> overrideFiles;                         // paths inside Override (relative), for listing
};
Mount* M = nullptr;
std::map<std::string, std::pair<const uint8_t*, size_t>>& embedded() { static std::map<std::string, std::pair<const uint8_t*, size_t>> e; return e; }

bool relTo(const std::string& root, const std::string& path, std::string& rel) {       // path below root (both normalised)
    if (path.size() <= root.size() + 1 || path.compare(0, root.size(), root) != 0 || path[root.size()] != '/') return false;
    rel = path.substr(root.size() + 1); return true;
}

bool readReal(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    f.seekg(0, std::ios::end); std::streamoff n = f.tellg(); if (n < 0) return false; f.seekg(0);
    out.resize((size_t)n); if (n) f.read(reinterpret_cast<char*>(out.data()), n);
    return (bool)f || f.eof();
}

struct View { const uint8_t* p = nullptr; size_t n = 0; std::string real; bool found = false; };

// finds a path: an Override file, a packaged resource, or an ordinary file
View lookup(const std::string& path0) {
    View v; std::string path = norm(path0), rel;
    { auto em = embedded().find(path); if (em != embedded().end()) { v.p = em->second.first; v.n = em->second.second; v.found = true; return v; } }
    if (M && relTo(M->root, path, rel)) {
        std::string lr = lower(rel);
        auto ov = M->overrides.find(lr.rfind("override/", 0) == 0 ? lr.substr(9) : lr);
        if (ov == M->overrides.end()) { size_t sl = lr.rfind('/'); ov = M->overrides.find(sl == std::string::npos ? lr : lr.substr(sl + 1)); }
        if (ov != M->overrides.end()) { v.real = ov->second; v.found = true; return v; }
        auto it = M->index.find(lr);
        if (it != M->index.end()) {
            const Entry& e = it->second; v.p = M->grps[(size_t)e.grp].base + e.off; v.n = e.len; v.found = true; return v;
        }
    }
    std::error_code ec; if (fs::is_regular_file(path, ec)) { v.real = path; v.found = true; }
    return v;
}

} // namespace

void addEmbedded(const std::string& path, const void* data, size_t size) { embedded()[norm(path)] = {static_cast<const uint8_t*>(data), size}; }

bool isGameDir(const std::string& dir) {
    std::error_code ec; return fs::is_regular_file(fs::path(dir) / "resource.rht", ec) && fs::is_directory(fs::path(dir) / "data", ec);
}
bool mounted() { return M != nullptr; }

bool mountGame(const std::string& gameDir, std::string& err) {
    if (!isGameDir(gameDir)) { err = gameDir + " is not a game folder (resource.rht and data/ expected)"; return false; }
    auto m = new Mount; m->root = norm(fs::absolute(gameDir).string());
    std::optional<Rht> rhtO; try { rhtO = Rht::load((fs::path(gameDir) / "resource.rht").string()); } catch (std::exception& e) { err = e.what(); delete m; return false; }
    const Rht& rht = *rhtO;
    std::map<std::string, int> gi;                                  // group -> index into grps
    for (const RhtEntry& e : rht.entries()) {
        if (e.group.empty() || e.name.empty()) continue;
        auto g = gi.find(e.group);
        if (g == gi.end()) {
            Map mp; fs::path gp = fs::path(gameDir) / "data" / (e.group + ".grp"); std::error_code ec;
            if (fs::is_regular_file(gp, ec)) {
                int fd = ::open(gp.c_str(), O_RDONLY); struct stat st;
                if (fd >= 0 && fstat(fd, &st) == 0 && st.st_size >= 12) {
                    void* p = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                    if (p != MAP_FAILED) { mp.base = (const uint8_t*)p; mp.size = (size_t)st.st_size; }
                }
                if (fd >= 0) ::close(fd);
            }
            gi[e.group] = mp.base ? (int)m->grps.size() : -1; if (mp.base) m->grps.push_back(mp);
            g = gi.find(e.group);
        }
        if (g->second < 0) continue;                                // a group without an archive (the empty template groups)
        const Map& mp = m->grps[(size_t)g->second]; auto rd32 = [&](size_t i) { int32_t x; std::memcpy(&x, mp.base + i, 4); return x; };
        int32_t n = rd32(8); if (e.index < 0 || e.index >= n || 12 + (size_t)n * 8 > mp.size) continue;
        int32_t off = rd32(12 + (size_t)e.index * 8), len = rd32(16 + (size_t)e.index * 8);
        if (off < 0 || len < 0 || (size_t)off + (size_t)len > mp.size) continue;
        Entry en; en.grp = g->second; en.off = (uint32_t)off; en.len = (uint32_t)len; en.group = e.group; en.file = e.name + "." + lumpExt(e.type);
        for (char& c : en.file) if (c == '/' || c == '\\') c = '_';                 // (the extractor does the same)
        std::string key = lower(en.group + "/" + en.file);
        if (m->index.find(key) == m->index.end()) m->groups[en.group].push_back(en.file);
        m->index[key] = en;
    }
    std::error_code ec; fs::path ov = fs::path(gameDir) / "Override";               // loose files that replace packaged ones
    if (fs::is_directory(ov, ec)) for (auto& f : fs::recursive_directory_iterator(ov, ec)) if (f.is_regular_file(ec)) {
        std::string rel = f.path().lexically_relative(ov).generic_string(), real = norm(f.path().string());
        m->overrides[lower(rel)] = real; m->overrides[lower(f.path().filename().string())] = real; m->overrideFiles.push_back(rel);
    }
    delete M; M = m; return true;
}

// ---- Stream
Stream::Stream(const std::string& path) : std::istream(&buf_) {
    View v = lookup(path);
    if (v.found && v.real.empty()) { buf_.set(reinterpret_cast<const char*>(v.p), v.n); len_ = v.n; ok_ = true; }
    else if (v.found && readReal(v.real, own_)) { buf_.set(reinterpret_cast<const char*>(own_.data()), own_.size()); len_ = own_.size(); ok_ = true; }
    if (!ok_) setstate(std::ios::failbit | std::ios::badbit);
}
Stream::Buf::pos_type Stream::Buf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) {
    char* b = eback(); char* e = egptr(); char* p = dir == std::ios_base::beg ? b + off : dir == std::ios_base::cur ? gptr() + off : e + off;
    if (p < b || p > e) return pos_type(off_type(-1));
    setg(b, p, e); return pos_type(p - b);
}

// ---- whole files
bool exists(const std::string& path) { return lookup(path).found; }
bool readBytes(const std::string& path, std::vector<uint8_t>& out) {
    View v = lookup(path); if (!v.found) return false;
    if (v.real.empty()) { out.assign(v.p, v.p + v.n); return true; }
    return readReal(v.real, out);
}
std::string readText(const std::string& path) {
    std::vector<uint8_t> b; if (!readBytes(path, b)) return "";
    return std::string(b.begin(), b.end());
}

// ---- directories
std::vector<std::string> list(const std::string& dir0) {
    std::vector<std::string> out; std::string dir = norm(dir0), rel;
    if (M && (dir == M->root || relTo(M->root, dir, rel))) {
        if (dir == M->root) { for (auto& g : M->groups) out.push_back(g.first); }
        else {
            std::string lr = lower(rel);
            for (auto& g : M->groups) if (lower(g.first) == lr) { out = g.second; break; }
            if (lr == "override") for (const std::string& f : M->overrideFiles) if (f.find('/') == std::string::npos) out.push_back(f);
            if (!out.empty()) return out;
        }
    }
    std::error_code ec; if (fs::is_directory(dir, ec)) for (auto& e : fs::directory_iterator(dir, ec)) out.push_back(e.path().filename().string());
    return out;
}
std::vector<std::string> walk(const std::string& dir0) {
    std::vector<std::string> out; std::string dir = norm(dir0), rel;
    if (M && (dir == M->root || relTo(M->root, dir, rel))) {
        std::string lr = lower(rel);
        if (dir == M->root) {
            for (auto& g : M->groups) for (const std::string& f : g.second) out.push_back(M->root + "/" + g.first + "/" + f);
            for (const std::string& f : M->overrideFiles) out.push_back(M->root + "/Override/" + f);
            return out;
        }
        for (auto& g : M->groups) if (lower(g.first) == lr) { for (const std::string& f : g.second) out.push_back(dir + "/" + f); return out; }
        if (lr == "override") { for (const std::string& f : M->overrideFiles) out.push_back(dir + "/" + f); return out; }
    }
    std::error_code ec; if (fs::is_directory(dir, ec))
        for (auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) if (e.is_regular_file(ec)) out.push_back(e.path().generic_string());
    return out;
}

} // namespace vfs
} // namespace vb

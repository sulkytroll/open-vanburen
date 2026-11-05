#pragma once
#include <cstdint>
#include <istream>
#include <streambuf>
#include <string>
#include <vector>

namespace vb {
namespace vfs {

// The virtual file system. The loaders keep asking for plain paths ("<root>/Maps/00_03_Tutorial_Junktown.map");
// a mounted game folder answers them from its packaged files instead of an extracted copy:
//   "<gameDir>/<Group>/<name>.<ext>"  ->  the resource <name> of <Group>.grp (resource.rht names it, its type gives the extension)
//   "<gameDir>/Override/<file>"       ->  a real file that replaces a packaged resource of the same name (looked up first)
// Anything else under or outside a mount (English.stf, Fonts/, an extracted-assets directory) is an ordinary file.
// Lookups ignore case. The archives are memory-mapped, so nothing is read until a resource is asked for.

// Mounts the game folder (resource.rht + data/*.grp). Returns false (and sets err) when it is not one.
bool mountGame(const std::string& gameDir, std::string& err);
bool mounted();
bool isGameDir(const std::string& dir);                  // resource.rht and data/ are there

// The same bytes as an istream (std::getline, seekg and read work) over a mapped or loaded buffer; bad() when the file is not found.
class Stream : public std::istream {
public:
    explicit Stream(const std::string& path);
    bool ok() const { return ok_; }
    size_t size() const { return len_; }
private:
    struct Buf : std::streambuf {
        void set(const char* b, size_t n) { setg(const_cast<char*>(b), const_cast<char*>(b), const_cast<char*>(b) + n); }
        pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override;
        pos_type seekpos(pos_type pos, std::ios_base::openmode m) override { return seekoff(off_type(pos), std::ios_base::beg, m); }
    } buf_;
    std::vector<uint8_t> own_;
    size_t len_ = 0; bool ok_ = false;
};

// A file compiled into the binary, served at a made-up path (e.g. "embedded:/menu_buttons.tga"); it wins over everything else.
void addEmbedded(const std::string& path, const void* data, size_t size);

bool exists(const std::string& path);                    // a file (packaged resource or ordinary)
bool readBytes(const std::string& path, std::vector<uint8_t>& out);
std::string readText(const std::string& path);           // "" if missing
// File names (not paths) directly inside a directory; a mounted game folder lists its groups as directories and a group's resources as files.
std::vector<std::string> list(const std::string& dir);
// Every file path below a directory, recursively ("<dir>/<sub>/<file>"); under a mount only the packaged resources and Override.
std::vector<std::string> walk(const std::string& dir);

} // namespace vfs
} // namespace vb

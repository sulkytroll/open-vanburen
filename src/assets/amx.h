#pragma once
// Pawn/"Small" abstract machine (Van Buren `.scr` scripts, AMX file+amx version 6, debug info, cell = 32 bit, uncompacted code).
// Layout and opcode numbering: AMX_FORMAT_FINDINGS.md. A script is loaded once (AmxScript); each entity that has the script gets its own AmxVM
// (private data segment: globals, heap, stack) and calls its public functions (OnCreate, OnUpdate, ...). Natives are host functions bound by name.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vb {

struct AmxEntry { uint32_t address = 0; std::string name; };

class AmxScript {
public:
    static std::shared_ptr<AmxScript> load(const std::string& path, std::string& err);
    std::string path;
    std::vector<AmxEntry> publics, natives, pubvars;   // pubvars: public variables (`p_...`), address = byte offset in the data segment
    std::vector<int32_t> code;           // code cells (byte address = 4 * index)
    std::vector<uint8_t> data;           // initial data segment (file `dat` .. `hea`)
    uint32_t hea = 0, stp = 0;           // heap start / total data size incl. stack (bytes)
    int findPublic(const std::string& name) const { for (size_t i = 0; i < publics.size(); i++) if (publics[i].name == name) return (int)i; return -1; }
};

enum class AmxError { None = 0, Exit, Assert, StackErr, Bounds, MemAccess, InvalidInstr, StackLow, HeapLow, Callback, Native, Divide, Sleep, InvalidOp, NoPublic, Limit };

class AmxVM {
public:
    // A native receives the argument cells (args[0..n-1]) and the VM (for string/array access); its return value goes to PRI.
    using Native = std::function<int32_t(AmxVM&, const int32_t* args, int n)>;
    explicit AmxVM(std::shared_ptr<AmxScript> s);
    void bind(const std::string& name, Native fn);                      // by name; unbound natives return `unboundResult` (and are counted in `unbound`)
    // Run public function `name` with cell arguments; returns false on a VM error (see error()). `ret` receives PRI.
    bool call(const std::string& name, const std::vector<int32_t>& args, int32_t& ret, uint64_t maxInstructions = 5000000);
    AmxError error() const { return err_; }
    std::string errorText() const;
    // memory helpers for natives
    bool cellAt(int32_t addr, int32_t& v) const;
    bool setCell(int32_t addr, int32_t v);
    std::string getString(int32_t addr, size_t maxLen = 512) const;      // packed or unpacked
    int32_t allocString(const std::string& s);                           // copies onto the heap (unpacked, NUL terminated); returns its address
    int32_t heapMark() const { return hea_; }                            // temporary heap data for a host call: take the mark, allocate, call, release
    void heapRelease(int32_t mark) { if (mark >= (int32_t)script->hea && mark <= hea_) hea_ = mark; }
    int32_t allocArray(int cells);                                       // zeroed cells on the heap; returns the address (0 when out of room)
    int32_t global(const std::string& name) const;                       // address of a named global from the debug symbols, -1 if unknown
    std::shared_ptr<AmxScript> script;
    std::map<std::string, int> unbound;                                  // native name -> number of calls that hit no binding
    int32_t unboundResult = 0;
    void* user = nullptr;                                                // host back-pointer (the owning entity)
    std::function<void(int32_t cip, int32_t op, int32_t pri, int32_t alt, int32_t stk, int32_t frm)> trace;   // optional per-instruction hook (debugging)
    uint64_t instructions = 0;                                           // executed by the last call()
private:
    std::vector<uint8_t> mem_;
    std::vector<Native> bound_;
    std::vector<uint8_t> hasBound_;
    int32_t hea_ = 0, stk_ = 0;
    AmxError err_ = AmxError::None;
    bool run(int32_t entry, int32_t& ret, uint64_t maxInstr);
};

} // namespace vb

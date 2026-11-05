// Loads every shipped .scr, runs its public functions in the VM with all natives unbound (they return 0) and reports VM errors and which natives were hit.
#include "assets/amx.h"
#include "assets/stf.h"
#include <cstdio>
#include <filesystem>
#include <map>
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "../assets/Scripts";
    int ok = 0, bad = 0, runs = 0, runErrors = 0; std::map<std::string, int> natHit, errKinds;
    for (auto& e : fs::directory_iterator(root)) {
        if (e.path().extension() != ".scr") continue;
        std::string err; auto s = vb::AmxScript::load(e.path().string(), err);
        if (!s) { std::printf("LOAD FAIL %s: %s\n", e.path().filename().c_str(), err.c_str()); bad++; continue; }
        ok++;
        vb::AmxVM vm(s);
        for (const auto& p : s->publics) {
            std::vector<int32_t> args(4, 0); int32_t ret = 0; runs++;
            if (!vm.call(p.name, args, ret, 200000)) {
                runErrors++; errKinds[vm.errorText()]++;
                if (std::getenv("VERBOSE")) std::printf("  %s::%s -> %s after %llu instructions\n", e.path().filename().c_str(), p.name.c_str(), vm.errorText().c_str(), (unsigned long long)vm.instructions);
            }
        }
        for (auto& u : vm.unbound) natHit[u.first] += u.second;
    }
    std::printf("scripts ok=%d fail=%d, public calls %d, errors %d\n", ok, bad, runs, runErrors);
    for (auto& k : errKinds) std::printf("  error %-26s %d\n", k.first.c_str(), k.second);
    {   // string table
        vb::StringTable st; std::string e; fs::path stf = argc > 2 ? fs::path(argv[2]) : fs::path(root).parent_path().parent_path() / "English.stf";       // usage: vb_amx_test [Scripts dir] [English.stf]
        if (st.load(stf.string(), e)) std::printf("English.stf: %zu strings, #1 '%s', #2 '%s', last '%s'\n", st.size(), st.text(1).c_str(), st.text(2).c_str(), st.text((uint32_t)st.size()).c_str());
        else std::printf("English.stf: %s\n", e.c_str());
    }
    std::printf("natives reached with stub bindings: %zu\n", natHit.size());
}

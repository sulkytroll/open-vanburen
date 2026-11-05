#include "amx.h"
#include "binreader.h"
#include <cstring>

namespace vb {

enum Op {   // numbering verified on all 83 shipped scripts (amx_parse.py): standard Pawn list with PUSH_R after PUSH_ALT and SWAP_PRI SWAP_ALT PUSHADDR NOP at the end
    NONE, LOAD_PRI, LOAD_ALT, LOAD_S_PRI, LOAD_S_ALT, LREF_PRI, LREF_ALT, LREF_S_PRI, LREF_S_ALT, LOAD_I, LODB_I, CONST_PRI, CONST_ALT, ADDR_PRI, ADDR_ALT,
    STOR_PRI, STOR_ALT, STOR_S_PRI, STOR_S_ALT, SREF_PRI, SREF_ALT, SREF_S_PRI, SREF_S_ALT, STOR_I, STRB_I, LIDX, LIDX_B, IDXADDR, IDXADDR_B, ALIGN_PRI, ALIGN_ALT, LCTRL, SCTRL,
    MOVE_PRI, MOVE_ALT, XCHG, PUSH_PRI, PUSH_ALT, PUSH_R, PUSH_C, PUSH, PUSH_S, POP_PRI, POP_ALT, STACK, HEAP, PROC, RET, RETN, CALL, CALL_PRI, JUMP, JREL, JZER, JNZ, JEQ, JNEQ,
    JLESS, JLEQ, JGRTR, JGEQ, JSLESS, JSLEQ, JSGRTR, JSGEQ, SHL, SHR, SSHR, SHL_C_PRI, SHL_C_ALT, SHR_C_PRI, SHR_C_ALT, SMUL, SDIV, SDIV_ALT, UMUL, UDIV, UDIV_ALT, ADD, SUB, SUB_ALT,
    AND, OR, XOR, NOT, NEG, INVERT, ADD_C, SMUL_C, ZERO_PRI, ZERO_ALT, ZERO, ZERO_S, SIGN_PRI, SIGN_ALT, EQ, NEQ, LESS, LEQ, GRTR, GEQ, SLESS, SLEQ, SGRTR, SGEQ, EQ_C_PRI, EQ_C_ALT,
    INC_PRI, INC_ALT, INC, INC_S, INC_I, DEC_PRI, DEC_ALT, DEC, DEC_S, DEC_I, MOVS, CMPS, FILL, HALT, BOUNDS, SYSREQ_PRI, SYSREQ_C, FILE, LINE, SYMBOL, SRANGE, JUMP_PRI, SWITCH, CASETBL,
    SWAP_PRI, SWAP_ALT, PUSHADDR, NOP
};

std::shared_ptr<AmxScript> AmxScript::load(const std::string& path, std::string& err) {
    std::vector<uint8_t> d;
    try { d = readFileBytes(path); } catch (std::exception& e) { err = e.what(); return nullptr; }
    if (d.size() < 52) { err = "AMX: too small"; return nullptr; }
    auto i32 = [&](size_t o) { int32_t v; memcpy(&v, &d[o], 4); return v; };
    uint16_t magic; memcpy(&magic, &d[4], 2);
    int16_t defsize; memcpy(&defsize, &d[10], 2);
    if (magic != 0xf1e0 || d[6] != 6 || d[7] != 6 || defsize < 8 || defsize > 256) { err = "AMX: unsupported header"; return nullptr; }
    int32_t cod = i32(12), dat = i32(16), hea = i32(20), stp = i32(24), publics = i32(32), natives = i32(36), libraries = i32(40), pubvarOff = i32(44), tagsOff = i32(48);
    if (cod < 52 || dat < cod || hea < dat || stp < hea || (size_t)hea > d.size() || publics < 52 || natives < publics || libraries < natives || (size_t)libraries > d.size()) { err = "AMX: bad section offsets"; return nullptr; }
    auto s = std::make_shared<AmxScript>(); s->path = path;
    auto table = [&](int32_t a, int32_t b, std::vector<AmxEntry>& out) {
        for (int32_t o = a; o + defsize <= b; o += defsize) {
            AmxEntry e; memcpy(&e.address, &d[o], 4);
            size_t n = 0; while (n < (size_t)defsize - 4 && d[o + 4 + n]) n++;
            e.name.assign((const char*)&d[o + 4], n); out.push_back(e);
        }
    };
    table(publics, natives, s->publics); table(natives, libraries, s->natives);
    if (pubvarOff >= libraries && tagsOff >= pubvarOff && (size_t)tagsOff <= d.size()) table(pubvarOff, tagsOff, s->pubvars);
    s->code.resize((size_t)(dat - cod) / 4); memcpy(s->code.data(), &d[cod], s->code.size() * 4);
    s->data.assign(d.begin() + dat, d.begin() + hea);
    s->hea = (uint32_t)(hea - dat); s->stp = (uint32_t)stp;
    return s;
}

AmxVM::AmxVM(std::shared_ptr<AmxScript> s) : script(std::move(s)) {
    mem_.assign(script->stp + 16, 0);
    memcpy(mem_.data(), script->data.data(), script->data.size());
    hea_ = (int32_t)script->hea;
    bound_.resize(script->natives.size()); hasBound_.assign(script->natives.size(), 0);
}
void AmxVM::bind(const std::string& name, Native fn) {
    for (size_t i = 0; i < script->natives.size(); i++) if (script->natives[i].name == name) { bound_[i] = fn; hasBound_[i] = 1; }
}
bool AmxVM::cellAt(int32_t a, int32_t& v) const { if (a < 0 || (uint32_t)a + 4 > script->stp || (a & 3)) return false; memcpy(&v, &mem_[(size_t)a], 4); return true; }
bool AmxVM::setCell(int32_t a, int32_t v) { if (a < 0 || (uint32_t)a + 4 > script->stp || (a & 3)) return false; memcpy(&mem_[(size_t)a], &v, 4); return true; }
std::string AmxVM::getString(int32_t addr, size_t maxLen) const {
    std::string out; int32_t first;
    if (!cellAt(addr, first)) return out;
    if ((uint32_t)first > 0xff) {                                           // packed: 4 chars per cell, first char in the most significant byte
        for (int32_t a = addr; out.size() < maxLen; a += 4) { int32_t c; if (!cellAt(a, c)) break; for (int k = 3; k >= 0; k--) { char ch = (char)((c >> (8 * k)) & 0xff); if (!ch) return out; out.push_back(ch); } }
    } else for (int32_t a = addr; out.size() < maxLen; a += 4) { int32_t c; if (!cellAt(a, c) || !c) break; out.push_back((char)c); }
    return out;
}
int32_t AmxVM::allocArray(int cells) {
    int32_t addr = hea_, need = cells * 4;
    if (cells <= 0 || (uint32_t)(hea_ + need) + 1024 > script->stp) return 0;
    for (int i = 0; i < cells; i++) setCell(addr + i * 4, 0);
    hea_ += need; return addr;
}
int32_t AmxVM::allocString(const std::string& s) {
    int32_t addr = hea_; int32_t need = (int32_t)(s.size() + 1) * 4;
    if ((uint32_t)(hea_ + need) + 1024 > script->stp) return 0;
    for (size_t i = 0; i < s.size(); i++) setCell(addr + (int32_t)i * 4, (unsigned char)s[i]);
    setCell(addr + (int32_t)s.size() * 4, 0); hea_ += need; return addr;
}
int32_t AmxVM::global(const std::string& name) const {
    // symbols live in the code stream as SYMBOL records: [op, size, addr, tag, ?, name...] -- scan them
    const auto& c = script->code;
    for (size_t i = 0; i + 4 < c.size();) {
        if (c[i] == SYMBOL) {
            int32_t sz = c[i + 1]; if (sz < 12 || i + 2 + (size_t)sz / 4 > c.size()) break;
            const char* nm = (const char*)&c[i + 4]; size_t maxn = (size_t)(sz - 8);
            if (strncmp(nm, name.c_str(), maxn) == 0 && name.size() <= maxn && (name.size() == maxn || nm[name.size()] == 0) && ((c[i + 3] >> 8) & 0xff) == 1 /* global */) return c[i + 2];
            i += 2 + (size_t)sz / 4;
        } else if (c[i] == FILE) i += 2 + (size_t)c[i + 1] / 4;
        else break;
    }
    return -1;
}
std::string AmxVM::errorText() const {
    static const char* t[] = {"none", "exit", "assertion", "stack error", "array bounds", "invalid memory access", "invalid instruction", "stack low", "heap low", "callback", "native", "divide by zero", "sleep", "invalid operand", "no such public", "instruction limit"};
    return t[(int)err_];
}

bool AmxVM::call(const std::string& name, const std::vector<int32_t>& args, int32_t& ret, uint64_t maxInstr) {
    err_ = AmxError::None; ret = 0; instructions = 0;
    int p = script->findPublic(name); if (p < 0) { err_ = AmxError::NoPublic; return false; }
    int32_t stk = (int32_t)script->stp;
    auto push = [&](int32_t v) { stk -= 4; setCell(stk, v); };
    for (size_t i = args.size(); i-- > 0;) push(args[i]);
    push((int32_t)args.size() * 4);
    push(0);                                                                 // return address 0: code[0] is HALT 0
    // the stack pointer is local to run(); hand it over through the memory top
    stk_ = stk;
    return run((int32_t)script->publics[(size_t)p].address, ret, maxInstr);
}

bool AmxVM::run(int32_t entry, int32_t& ret, uint64_t maxInstr) {
    const std::vector<int32_t>& code = script->code;
    int32_t pri = 0, alt = 0, frm = stk_, stk = stk_, cip = entry;
    auto fail = [&](AmxError e) { err_ = e; ret = pri; stk_ = stk; return false; };
#define RD(a, v) if (!cellAt((a), (v))) return fail(AmxError::MemAccess)
#define WR(a, v) if (!setCell((a), (v))) return fail(AmxError::MemAccess)
#define PUSH(v) do { stk -= 4; if (stk < hea_ + 64) return fail(AmxError::StackLow); WR(stk, (v)); } while (0)
#define POP(v) do { RD(stk, (v)); stk += 4; } while (0)
    for (;;) {
        if (++instructions > maxInstr) return fail(AmxError::Limit);
        if (cip < 0 || (size_t)cip / 4 >= code.size() || (cip & 3)) return fail(AmxError::InvalidInstr);
        const size_t ip = (size_t)cip / 4;
        auto opnd = [&](size_t n) -> int32_t { return ip + n < code.size() ? code[ip + n] : 0; };
        const int32_t o1 = opnd(1);
        if (trace) trace(cip, code[ip], pri, alt, stk, frm);
        int adv = 2;                                                           // cells consumed by the instruction (1 + operands); jumps override
        int32_t t = 0, u = 0;
        switch (code[ip]) {
        case LOAD_PRI: RD(o1, pri); break;
        case LOAD_ALT: RD(o1, alt); break;
        case LOAD_S_PRI: RD(frm + o1, pri); break;
        case LOAD_S_ALT: RD(frm + o1, alt); break;
        case LREF_PRI: RD(o1, t); RD(t, pri); break;
        case LREF_ALT: RD(o1, t); RD(t, alt); break;
        case LREF_S_PRI: RD(frm + o1, t); RD(t, pri); break;
        case LREF_S_ALT: RD(frm + o1, t); RD(t, alt); break;
        case LOAD_I: RD(pri, t); pri = t; adv = 1; break;
        case LODB_I: {
            if (pri < 0 || (uint32_t)pri + (uint32_t)o1 > script->stp) return fail(AmxError::MemAccess);
            uint32_t v = 0; memcpy(&v, &mem_[(size_t)pri], (size_t)o1 > 4 ? 4 : (size_t)o1); pri = (int32_t)v; break; }
        case CONST_PRI: pri = o1; break;
        case CONST_ALT: alt = o1; break;
        case ADDR_PRI: pri = frm + o1; break;
        case ADDR_ALT: alt = frm + o1; break;
        case STOR_PRI: WR(o1, pri); break;
        case STOR_ALT: WR(o1, alt); break;
        case STOR_S_PRI: WR(frm + o1, pri); break;
        case STOR_S_ALT: WR(frm + o1, alt); break;
        case SREF_PRI: RD(o1, t); WR(t, pri); break;
        case SREF_ALT: RD(o1, t); WR(t, alt); break;
        case SREF_S_PRI: RD(frm + o1, t); WR(t, pri); break;
        case SREF_S_ALT: RD(frm + o1, t); WR(t, alt); break;
        case STOR_I: WR(alt, pri); adv = 1; break;
        case STRB_I: {
            if (alt < 0 || (uint32_t)alt + (uint32_t)o1 > script->stp) return fail(AmxError::MemAccess);
            memcpy(&mem_[(size_t)alt], &pri, (size_t)o1 > 4 ? 4 : (size_t)o1); break; }
        case LIDX: RD(alt + pri * 4, t); pri = t; adv = 1; break;
        case LIDX_B: RD(alt + (int32_t)((uint32_t)pri << o1), t); pri = t; break;
        case IDXADDR: pri = alt + pri * 4; adv = 1; break;
        case IDXADDR_B: pri = alt + (int32_t)((uint32_t)pri << o1); break;
        case ALIGN_PRI: if ((uint32_t)o1 < 4) pri ^= (4 - o1); break;
        case ALIGN_ALT: if ((uint32_t)o1 < 4) alt ^= (4 - o1); break;
        case LCTRL:
            switch (o1) { case 0: pri = 0; break; case 1: pri = 0; break; case 2: pri = hea_; break; case 3: pri = (int32_t)script->stp; break; case 4: pri = stk; break; case 5: pri = frm; break; case 6: pri = cip; break; default: pri = 0; }
            break;
        case SCTRL:
            switch (o1) { case 2: hea_ = pri; break; case 4: stk = pri; break; case 5: frm = pri; break; case 6: cip = pri; adv = 0; break; default: break; }
            if (o1 == 6) continue;
            break;
        case MOVE_PRI: pri = alt; adv = 1; break;
        case MOVE_ALT: alt = pri; adv = 1; break;
        case XCHG: t = pri; pri = alt; alt = t; adv = 1; break;
        case PUSH_PRI: PUSH(pri); adv = 1; break;
        case PUSH_ALT: PUSH(alt); adv = 1; break;
        case PUSH_R: for (int32_t k = 0; k < o1; k++) PUSH(pri); break;
        case PUSH_C: PUSH(o1); break;
        case PUSH: RD(o1, t); PUSH(t); break;
        case PUSH_S: RD(frm + o1, t); PUSH(t); break;
        case PUSHADDR: PUSH(frm + o1); break;
        case POP_PRI: POP(pri); adv = 1; break;
        case POP_ALT: POP(alt); adv = 1; break;
        case STACK: alt = stk; stk += o1; if (stk < hea_ + 64) return fail(AmxError::StackLow); if (stk > (int32_t)script->stp) return fail(AmxError::StackErr); break;
        case HEAP: alt = hea_; hea_ += o1; if (hea_ < (int32_t)script->hea || hea_ + 64 > stk) return fail(AmxError::HeapLow); break;
        case PROC: PUSH(frm); frm = stk; adv = 1; break;
        case RET: POP(frm); POP(t); cip = t; continue;
        case RETN: POP(frm); POP(t); POP(u); stk += u; cip = t; continue;
        case CALL: PUSH(cip + 8); cip = o1; continue;
        case CALL_PRI: PUSH(cip + 4); cip = pri; continue;
        case JUMP: cip = o1; continue;
        case JREL: cip += 8 + o1; continue;                                       // relative to the cell after the operand (unused in shipped scripts)
        case JZER: if (pri == 0) { cip = o1; continue; } break;
        case JNZ: if (pri != 0) { cip = o1; continue; } break;
        case JEQ: if (pri == alt) { cip = o1; continue; } break;
        case JNEQ: if (pri != alt) { cip = o1; continue; } break;
        case JLESS: if ((uint32_t)pri < (uint32_t)alt) { cip = o1; continue; } break;
        case JLEQ: if ((uint32_t)pri <= (uint32_t)alt) { cip = o1; continue; } break;
        case JGRTR: if ((uint32_t)pri > (uint32_t)alt) { cip = o1; continue; } break;
        case JGEQ: if ((uint32_t)pri >= (uint32_t)alt) { cip = o1; continue; } break;
        case JSLESS: if (pri < alt) { cip = o1; continue; } break;
        case JSLEQ: if (pri <= alt) { cip = o1; continue; } break;
        case JSGRTR: if (pri > alt) { cip = o1; continue; } break;
        case JSGEQ: if (pri >= alt) { cip = o1; continue; } break;
        case SHL: pri = (int32_t)((uint32_t)pri << (alt & 31)); adv = 1; break;
        case SHR: pri = (int32_t)((uint32_t)pri >> (alt & 31)); adv = 1; break;
        case SSHR: pri >>= (alt & 31); adv = 1; break;
        case SHL_C_PRI: pri = (int32_t)((uint32_t)pri << (o1 & 31)); break;
        case SHL_C_ALT: alt = (int32_t)((uint32_t)alt << (o1 & 31)); break;
        case SHR_C_PRI: pri = (int32_t)((uint32_t)pri >> (o1 & 31)); break;
        case SHR_C_ALT: alt = (int32_t)((uint32_t)alt >> (o1 & 31)); break;
        case SMUL: pri = (int32_t)((int64_t)pri * alt); adv = 1; break;
        case SDIV: case SDIV_ALT: {
            int32_t num = code[ip] == SDIV ? pri : alt, den = code[ip] == SDIV ? alt : pri;
            if (den == 0) return fail(AmxError::Divide);
            int32_t q = num / den, r = num % den;
            if (r != 0 && ((r ^ den) < 0)) { q -= 1; r += den; }                // floored division, remainder has the divisor's sign
            pri = q; alt = r; adv = 1; break; }
        case UMUL: pri = (int32_t)((uint32_t)pri * (uint32_t)alt); adv = 1; break;
        case UDIV: case UDIV_ALT: {
            uint32_t num = code[ip] == UDIV ? (uint32_t)pri : (uint32_t)alt, den = code[ip] == UDIV ? (uint32_t)alt : (uint32_t)pri;
            if (den == 0) return fail(AmxError::Divide);
            pri = (int32_t)(num / den); alt = (int32_t)(num % den); adv = 1; break; }
        case ADD: pri += alt; adv = 1; break;
        case SUB: pri -= alt; adv = 1; break;
        case SUB_ALT: pri = alt - pri; adv = 1; break;
        case AND: pri &= alt; adv = 1; break;
        case OR: pri |= alt; adv = 1; break;
        case XOR: pri ^= alt; adv = 1; break;
        case NOT: pri = !pri; adv = 1; break;
        case NEG: pri = (int32_t)(0u - (uint32_t)pri); adv = 1; break;
        case INVERT: pri = ~pri; adv = 1; break;
        case ADD_C: pri += o1; break;
        case SMUL_C: pri = (int32_t)((int64_t)pri * o1); break;
        case ZERO_PRI: pri = 0; adv = 1; break;
        case ZERO_ALT: alt = 0; adv = 1; break;
        case ZERO: WR(o1, 0); break;
        case ZERO_S: WR(frm + o1, 0); break;
        case SIGN_PRI: if ((pri & 0xff) >= 0x80) pri |= (int32_t)0xffffff00; adv = 1; break;
        case SIGN_ALT: if ((alt & 0xff) >= 0x80) alt |= (int32_t)0xffffff00; adv = 1; break;
        case EQ: pri = pri == alt; adv = 1; break;
        case NEQ: pri = pri != alt; adv = 1; break;
        case LESS: pri = (uint32_t)pri < (uint32_t)alt; adv = 1; break;
        case LEQ: pri = (uint32_t)pri <= (uint32_t)alt; adv = 1; break;
        case GRTR: pri = (uint32_t)pri > (uint32_t)alt; adv = 1; break;
        case GEQ: pri = (uint32_t)pri >= (uint32_t)alt; adv = 1; break;
        case SLESS: pri = pri < alt; adv = 1; break;
        case SLEQ: pri = pri <= alt; adv = 1; break;
        case SGRTR: pri = pri > alt; adv = 1; break;
        case SGEQ: pri = pri >= alt; adv = 1; break;
        case EQ_C_PRI: pri = pri == o1; break;
        case EQ_C_ALT: pri = alt == o1; break;
        case INC_PRI: pri++; adv = 1; break;
        case INC_ALT: alt++; adv = 1; break;
        case INC: RD(o1, t); WR(o1, t + 1); break;
        case INC_S: RD(frm + o1, t); WR(frm + o1, t + 1); break;
        case INC_I: RD(pri, t); WR(pri, t + 1); adv = 1; break;
        case DEC_PRI: pri--; adv = 1; break;
        case DEC_ALT: alt--; adv = 1; break;
        case DEC: RD(o1, t); WR(o1, t - 1); break;
        case DEC_S: RD(frm + o1, t); WR(frm + o1, t - 1); break;
        case DEC_I: RD(pri, t); WR(pri, t - 1); adv = 1; break;
        case MOVS:
            if (pri < 0 || alt < 0 || (uint32_t)pri + (uint32_t)o1 > script->stp || (uint32_t)alt + (uint32_t)o1 > script->stp) return fail(AmxError::MemAccess);
            memmove(&mem_[(size_t)alt], &mem_[(size_t)pri], (size_t)o1); break;
        case CMPS: {
            if (pri < 0 || alt < 0 || (uint32_t)pri + (uint32_t)o1 > script->stp || (uint32_t)alt + (uint32_t)o1 > script->stp) return fail(AmxError::MemAccess);
            int32_t d = 0; for (int32_t k = 0; k < o1 && !d; k++) d = (int32_t)mem_[(size_t)pri + (size_t)k] - (int32_t)mem_[(size_t)alt + (size_t)k];
            pri = d; break; }
        case FILL: for (int32_t k = 0; k < o1; k += 4) WR(alt + k, pri); break;
        case HALT: ret = pri; stk_ = stk; if (o1 == 0) return true; err_ = AmxError::Exit; return false;
        case BOUNDS: if ((uint32_t)pri > (uint32_t)o1) return fail(AmxError::Bounds); break;
        case SYSREQ_PRI: case SYSREQ_C: {
            int32_t n = code[ip] == SYSREQ_PRI ? pri : o1;
            adv = code[ip] == SYSREQ_PRI ? 1 : 2;
            if (n < 0 || (size_t)n >= script->natives.size()) return fail(AmxError::Callback);
            int32_t nb; RD(stk, nb); int cnt = nb / 4; std::vector<int32_t> a((size_t)(cnt > 0 ? cnt : 0));
            for (int k = 0; k < cnt; k++) RD(stk + 4 + 4 * k, a[(size_t)k]);
            stk_ = stk;
            if (hasBound_[(size_t)n]) pri = bound_[(size_t)n](*this, a.data(), cnt);
            else { unbound[script->natives[(size_t)n].name]++; pri = unboundResult; }
            break; }
        case FILE: adv = 2 + o1 / 4; break;
        case LINE: adv = 3; break;
        case SYMBOL: adv = 2 + o1 / 4; break;
        case SRANGE: adv = 3; break;
        case JUMP_PRI: cip = pri; continue;
        case SWITCH: {
            size_t tb = (size_t)o1 / 4; if (tb + 3 > code.size() || code[tb] != CASETBL) return fail(AmxError::InvalidInstr);
            int32_t num = code[tb + 1], target = code[tb + 2];
            if (tb + 3 + (size_t)num * 2 > code.size()) return fail(AmxError::InvalidInstr);
            for (int32_t k = 0; k < num; k++) if (code[tb + 3 + (size_t)k * 2] == pri) { target = code[tb + 4 + (size_t)k * 2]; break; }
            cip = target; continue; }
        case SWAP_PRI: RD(stk, t); WR(stk, pri); pri = t; adv = 1; break;
        case SWAP_ALT: RD(stk, t); WR(stk, alt); alt = t; adv = 1; break;
        case NOP: adv = 1; break;
        default: return fail(AmxError::InvalidInstr);
        }
        cip += adv * 4;
    }
#undef RD
#undef WR
#undef PUSH
#undef POP
}

} // namespace vb

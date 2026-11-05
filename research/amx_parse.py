#!/usr/bin/env python3
"""Pawn/'Small' AMX (Van Buren .scr) parser + disassembler + structural verifier.
Header (file/amx version 6, flags 2 = debug info, defsize 40): i32 size, u16 magic 0xF1E0, u8 file_ver, u8 amx_ver, i16 flags, i16 defsize,
i32 cod, dat, hea, stp, cip, publics, natives, libraries, pubvars, tags. Public/native entries: u32 address + char[36] name."""
import struct,sys,glob,collections
OPS = """NONE LOAD_PRI LOAD_ALT LOAD_S_PRI LOAD_S_ALT LREF_PRI LREF_ALT LREF_S_PRI LREF_S_ALT LOAD_I LODB_I CONST_PRI CONST_ALT ADDR_PRI ADDR_ALT
STOR_PRI STOR_ALT STOR_S_PRI STOR_S_ALT SREF_PRI SREF_ALT SREF_S_PRI SREF_S_ALT STOR_I STRB_I LIDX LIDX_B IDXADDR IDXADDR_B ALIGN_PRI ALIGN_ALT LCTRL SCTRL
MOVE_PRI MOVE_ALT XCHG PUSH_PRI PUSH_ALT PUSH_R PUSH_C PUSH PUSH_S POP_PRI POP_ALT STACK HEAP PROC RET RETN CALL CALL_PRI JUMP JREL JZER JNZ JEQ JNEQ JLESS JLEQ JGRTR JGEQ
JSLESS JSLEQ JSGRTR JSGEQ SHL SHR SSHR SHL_C_PRI SHL_C_ALT SHR_C_PRI SHR_C_ALT SMUL SDIV SDIV_ALT UMUL UDIV UDIV_ALT ADD SUB SUB_ALT AND OR XOR NOT NEG INVERT
ADD_C SMUL_C ZERO_PRI ZERO_ALT ZERO ZERO_S SIGN_PRI SIGN_ALT EQ NEQ LESS LEQ GRTR GEQ SLESS SLEQ SGRTR SGEQ EQ_C_PRI EQ_C_ALT INC_PRI INC_ALT INC INC_S INC_I
DEC_PRI DEC_ALT DEC DEC_S DEC_I MOVS CMPS FILL HALT BOUNDS SYSREQ_PRI SYSREQ_C FILE LINE SYMBOL SRANGE JUMP_PRI SWITCH CASETBL SWAP_PRI SWAP_ALT PUSHADDR NOP""".split()
# operand cells per opcode (variable ones handled specially: CASETBL, FILE, SYMBOL)
NOPR = {o:0 for o in OPS}
for o in "LOAD_PRI LOAD_ALT LOAD_S_PRI LOAD_S_ALT LREF_PRI LREF_ALT LREF_S_PRI LREF_S_ALT LODB_I CONST_PRI CONST_ALT ADDR_PRI ADDR_ALT STOR_PRI STOR_ALT STOR_S_PRI STOR_S_ALT SREF_PRI SREF_ALT SREF_S_PRI SREF_S_ALT STRB_I LIDX_B IDXADDR_B ALIGN_PRI ALIGN_ALT LCTRL SCTRL PUSH_R PUSH_C PUSH PUSH_S PUSHADDR STACK HEAP CALL JUMP JREL JZER JNZ JEQ JNEQ JLESS JLEQ JGRTR JGEQ JSLESS JSLEQ JSGRTR JSGEQ SHL_C_PRI SHL_C_ALT SHR_C_PRI SHR_C_ALT ADD_C SMUL_C ZERO ZERO_S EQ_C_PRI EQ_C_ALT INC INC_S DEC DEC_S MOVS CMPS FILL HALT BOUNDS SYSREQ_C LINE SRANGE SWITCH".split(): NOPR[o]=1
NOPR['SRANGE']=2; NOPR['LINE']=2
JUMPS={'JUMP','JZER','JNZ','JEQ','JNEQ','JLESS','JLEQ','JGRTR','JGEQ','JSLESS','JSLEQ','JSGRTR','JSGEQ'}
class Amx:
    def __init__(s,path):
        d=s.d=open(path,'rb').read()
        (s.size,s.magic,s.fv,s.av,s.flags,s.defsize,s.cod,s.dat,s.hea,s.stp,s.cip,s.publics,s.natives,s.libraries,s.pubvars,s.tags)=struct.unpack_from('<IHBBhhiiiiiiiiii',d,0)
        assert s.magic==0xf1e0
        def table(a,b):
            out=[]
            for o in range(a,b,s.defsize):
                addr=struct.unpack_from('<I',d,o)[0]; nm=d[o+4:o+s.defsize].split(b'\0')[0].decode('latin1'); out.append((addr,nm))
            return out
        s.pub=table(s.publics,s.natives); s.nat=table(s.natives,s.libraries)
        s.code=struct.unpack_from('<%di'%((s.dat-s.cod)//4),d,s.cod)      # cells (uncompacted)
        s.data=d[s.dat:s.hea]
    def disasm(s):
        """-> list of (byte_addr, opname, operands) ; raises on an unknown opcode"""
        out=[]; i=0; c=s.code
        while i<len(c):
            op=c[i]
            if not 0<=op<len(OPS): raise ValueError('bad opcode %d at cell %d'%(op,i))
            name=OPS[op]; n=NOPR[name]
            if name=='CASETBL':
                cnt=c[i+1]; ops=list(c[i+1:i+3+2*cnt]); out.append((i*4,name,ops)); i+=3+2*cnt; continue
            if name in('FILE','SYMBOL'):
                sz=c[i+1]; out.append((i*4,name,[sz])); i+=2+sz//4; continue          # opcode, size (bytes), then `size` bytes of record
            out.append((i*4,name,list(c[i+1:i+1+n]))); i+=1+n
        return out
def verify(a):
    ins=a.disasm(); starts={x[0] for x in ins}; bad=[]
    procs={x[0] for x in ins if x[1]=='PROC'}
    for addr,nm in a.pub:
        if addr not in procs: bad.append(('public not at PROC',nm,addr))
    for addr,name,ops in ins:
        if name=='SYSREQ_C' and not 0<=ops[0]<len(a.nat): bad.append(('native index out of range',addr,ops[0]))
        if name=='SWITCH' and ops[0] not in {x[0] for x in ins if x[1]=='CASETBL'}: bad.append(('switch target not CASETBL',addr,ops[0]))
        if name=='CASETBL':
            cnt=ops[0]; tg=[ops[3+2*k] for k in range(cnt)]+[ops[1]]
            for t in tg:
                if t not in starts: bad.append(('case target not an instruction',addr,t))
        if name=='CALL' and ops[0] not in procs: bad.append(('call target not PROC',addr,ops[0]))
        if name in JUMPS and ops[0] not in starts: bad.append(('jump target not an instruction',addr,name,ops[0]))
    return ins,bad
if __name__=='__main__':
    tot=0; bads=0; opc=collections.Counter()
    for f in sorted(glob.glob('../assets/Scripts/*.scr')):
        a=Amx(f)
        try: ins,bad=verify(a)
        except Exception as e: print('FAIL',f.split('/')[-1],e); bads+=1; continue
        tot+=len(ins); bads+=len(bad)
        for x in ins: opc[x[1]]+=1
        if bad: print(f.split('/')[-1],bad[:3])
    print('instructions',tot,'violations',bads); print(opc.most_common())

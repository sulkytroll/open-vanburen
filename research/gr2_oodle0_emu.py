#!/usr/bin/env python3
"""Oracle for Granny Oodle0 (section compression type 1): runs the REAL decompressor from granny2.dll (FUN_1001d060 at 0x1001d060)
inside a Unicorn x86 emulator. Used to verify the native C++ port (src/assets/oodle0.cpp) and to dump decompressed sections.
Needs `pip install unicorn` (kept in a venv; not a build dependency)."""
import struct,sys
from unicorn import *
from unicorn.x86_const import *
BASE=0x10000000; STACK=0x7f000000; HEAP=0x20000000; INBUF=0x30000000; OUTBUF=0x40000000; STUB=0x50000000
class Emu:
    def __init__(s,dll='granny2.dll'):
        d=open(dll,'rb').read(); pe=struct.unpack_from('<I',d,0x3c)[0]
        ns=struct.unpack_from('<H',d,pe+6)[0]; osz=struct.unpack_from('<H',d,pe+20)[0]
        soi=struct.unpack_from('<I',d,pe+24+56)[0]
        u=s.u=Uc(UC_ARCH_X86,UC_MODE_32)
        u.mem_map(BASE,(soi+0xfff)&~0xfff)
        u.mem_write(BASE,d[:struct.unpack_from('<I',d,pe+24+60)[0]])
        for i in range(ns):
            o=pe+24+osz+i*40; vs,va,rs,rp=struct.unpack_from('<IIII',d,o+8)
            u.mem_write(BASE+va,d[rp:rp+min(rs,vs)])
        u.mem_map(STACK,0x100000); u.mem_map(HEAP,0x4000000); u.mem_map(INBUF,0x2000000); u.mem_map(OUTBUF,0x2000000); u.mem_map(STUB,0x1000)
        u.mem_write(STUB,b'\xc3'*16)           # stub bodies: 'ret'
        u.mem_write(0x10033620,struct.pack('<II',STUB,STUB+1))   # allocator / free hooks used by the decompressor
        s.heap=HEAP
        u.hook_add(UC_HOOK_CODE,s._hook,begin=STUB,end=STUB+16)
    def _hook(s,u,addr,size,_):
        esp=u.reg_read(UC_X86_REG_ESP)
        if addr==STUB:        # alloc(file,line,align,size)
            sz=struct.unpack('<I',u.mem_read(esp+16,4))[0]
            p=(s.heap+15)&~15; s.heap=p+sz+64; u.reg_write(UC_X86_REG_EAX,p)
        elif addr==STUB+1: pass
    def decompress(s,comp,stop0,stop1,stop2):
        u=s.u; s.heap=HEAP
        u.mem_write(INBUF,bytes(comp)+b'\0'*64)
        u.mem_write(OUTBUF,b'\0'*(stop2+16))
        esp=STACK+0x80000
        args=[len(comp),INBUF,stop0,stop1,stop2,OUTBUF]
        esp-=4*(len(args)+1)
        u.mem_write(esp,struct.pack('<7I',STUB+8,*args))     # return address -> 'ret' stub, then we stop there
        u.reg_write(UC_X86_REG_ESP,esp)
        u.hook_add(UC_HOOK_CODE,lambda uc,a,sz,_: uc.emu_stop(),begin=STUB+8,end=STUB+8)
        u.emu_start(0x1001d060,STUB+8,timeout=60*UC_SECOND_SCALE,count=0)
        return bytes(u.mem_read(OUTBUF,stop2))
if __name__=='__main__':
    sys.path.insert(0,'.')
    from gr2_parse import Gr2
    g=Gr2(sys.argv[1]); e=Emu()
    for i,sec in enumerate(g.secs):
        if sec['comp']==1 and sec['size']:
            out=e.decompress(g.d[sec['off']:sec['off']+sec['size']],sec['f16'],sec['f8'],sec['exp'])
            print(i,len(out),out[:48].hex(' '))

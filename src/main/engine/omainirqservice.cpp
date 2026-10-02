// Cycle/bus behavior referenced from MAME m68000 (BSD-3-Clause,
// Olivier Galibert). This is an original bounded native service port;
// the general-purpose MAME CPU core is not included.
#include "engine/omainirqservice.hpp"
#include <stdexcept>

namespace {
void word(uint32_t& d,uint16_t v) { d=(d&0xffff0000)|v; }
void byte(uint32_t& d,uint8_t v) { d=(d&0xffffff00)|v; }
}
unsigned OMainIrqService::completion_tail(uint16_t op,uint16_t source) {
    const unsigned family=op>>12,mode=(op>>3)&7,size=(op>>6)&3;
    if(family==14 && size!=3) {
        unsigned count=op&0x20 ? source&63 : (op>>9)&7;
        if(!(op&0x20) && !count) count=8;
        return (size==2 ? 4 : 2)+2*count;
    }
    // MAME's roml3/rcal3: register-source long ALU ends with four/two
    // internal clocks; memory-source long ALU ends with two clocks.
    if((family==8 || family==9 || family==11 || family==12 || family==13) && size==2 && !(op&0x100))
        return mode<2 ? (family==11 ? 2 : 4) : 2;
    // ADDA/SUBA/CMPA word ends with four clocks; long memory source with
    // two. CMPA uses its own rcal3 two-clock ending.
    if((family==9 || family==11 || family==13) && size==3)
        return family==11 ? 2 : (!(op&0x100) || mode<2 ? 4 : 2);
    // Immediate BTST on Dn finishes its Z flag two clocks after prefetch.
    if((op&0xfff8)==0x0800) return 2;
    // EXG's register exchange completes two clocks after its prefetch.
    if((op&0xf1f8)==0xc140 || (op&0xf1f8)==0xc148 || (op&0xf1f8)==0xc188) return 2;
    return 0;
}
void OMainIrqService::idle(unsigned cycles) { ns+=CYCLE_NS*cycles; }
uint16_t OMainIrqService::read16(uint32_t a) { clock(ns);auto v=bus->read16(a);idle(4);return v; }
uint32_t OMainIrqService::read32(uint32_t a) { const auto high=read16(a);return (uint32_t(high)<<16)|read16(a+2); }
uint8_t OMainIrqService::read8(uint32_t a) { clock(ns);auto v=bus->read8(a);idle(4);return v; }
void OMainIrqService::write16(uint32_t a,uint16_t v) { clock(ns);bus->write16(a,v);idle(4); }
void OMainIrqService::write8(uint32_t a,uint8_t v) { clock(ns);bus->write8(a,v);idle(4); }

OMainIrqService::Result OMainIrqService::run(MainBus& b,const Context& c,const Clock& advance) {
    if((c.level!=2 && c.level!=6) || c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1))
        throw std::runtime_error("unsupported 68000 service context");
    bus=&b;clock=advance;ns=c.boundary_ns;pc=c.interrupted_pc;
    auto r=c.registers;uint32_t sp=c.supervisor_sp;
    // 68000 exception entry: six internal clocks, PC low; CPU-space IACK
    // uses the ten-clock E phase (VPA), one after-delay clock, four bus
    // clocks, then four internal clocks. Source: MAME m68000 state_interrupt
    // and vpa_sync/vpa_after, independent of this call's captured timestamps.
    idle(6);write16(sp-2,c.return_pc);
    const uint64_t cycles=ns/CYCLE_NS,mod=cycles%10;
    const uint64_t sync=cycles-mod+(mod<7 ? 10 : 20);
    idle(static_cast<unsigned>(sync-cycles)+1+4+4);
    sp-=6;write16(sp,c.sr);write16(sp+2,c.return_pc>>16);
    const uint32_t vector=(24+c.level)*4;
    const uint32_t entry=read32(vector);
    if(entry!=(c.level==2 ? 0x1461c : 0x1466c))
        throw std::runtime_error("unexpected arcade IRQ vector");
    idle(10); // two handler prefetch bus phases separated by two clocks

    // $01461C/$01466C MOVE.W D0,abs.L: two extension/prefetch phases,
    // data write at +8 and final prefetch, 16 clocks total.
    pc=entry;idle(8);write16(0xe8012,r.d[0]);idle(4);
    if(c.level==2) {
        // $014622 MOVEM.L: mask/next prefetch, reversed predecrement longs,
        // final prefetch. The low word is written before the high word.
        pc=0x14622;idle(4);
        for(uint32_t v:{r.a[1],r.a[0],r.d[2],r.d[1],r.d[0]}) {
            write16(sp-2,v);write16(sp-4,v>>16);sp-=4;
        }
        idle(4);
        // $014626-$014632: absolute.L ADC read, LEA, indirect word read,
        // indexed byte store (two EA clocks plus extension/prefetch).
        pc=0x14626;idle(8);byte(r.d[0],read8(0x130001));idle(4);
        pc=0x1462c;r.a[0]=0xffff8890;idle(8);
        pc=0x14630;word(r.d[1],read16(r.a[0]));idle(4);
        pc=0x14632;idle(6);write8(r.a[0]+2+int16_t(r.d[1]),r.d[0]);idle(4);
        // $014636-$014642: ADDQ/CMPI, word BCS (10 taken/12 untaken),
        // optional MOVEQ, then indirect store. Word overflow is preserved.
        pc=0x14636;word(r.d[1],r.d[1]+1);idle(4);
        pc=0x14638;idle(8);
        pc=0x1463c;
        if(uint16_t(r.d[1])<6) idle(10);
        else { idle(12);pc=0x14640;r.d[1]=0;idle(4); }
        pc=0x14642;write16(r.a[0],r.d[1]);idle(4);
        // $014644-$014650: LSL #2 (6+2*n), byte read/modify/write at
        // absolute.W. Prefetch separates each operand read and data write.
        pc=0x14644;word(r.d[1],r.d[1]<<2);idle(10);
        pc=0x14646;idle(8);auto shadow=read8(0xffff8b6f);idle(4);
        write8(0xffff8b6f,shadow&0xe3);
        pc=0x1464c;idle(4);shadow=read8(0xffff8b6f);idle(4);
        write8(0xffff8b6f,shadow|r.d[1]);
        pc=0x14650;idle(4);byte(r.d[2],read8(0xffff8b6f));idle(4);
        pc=0x14654;idle(8);write8(0x140005,r.d[2]);idle(4);
        pc=0x1465a;idle(8);write8(0x130001,r.d[1]);idle(4);
        // $014660 JSR abs.L: target prefetch, high/low return-PC writes,
        // final target prefetch. The callee remains the actual $00B7F8.
        pc=0x14660;idle(8);sp-=4;write16(sp,1);write16(sp+2,0x4666);idle(4);
        auto tst=[&](uint32_t p,uint32_t a) { pc=p;idle(4);auto v=read8(a);idle(4);return v; };
        auto branch=[&](uint32_t p,bool taken) { pc=p;idle(taken ? 10 : 12);return taken; };
        const bool enabled=tst(0xb7f8,0xffff8902);
        uint32_t ret;
        if(branch(0xb7fc,!enabled)) {
            // $00B836/$00B83A CLR performs a real operand read on 68000.
            pc=0xb836;idle(4);read16(0xffff890a);idle(4);write16(0xffff890a,0);
            pc=0xb83a;idle(4);read8(0xffff8906);idle(4);write8(0xffff8906,0);
            ret=0xb83e;
        } else {
            pc=0xb800;idle(4);const uint16_t cadence=read16(0xffff8900)+1;idle(4);write16(0xffff8900,cadence);
            pc=0xb804;idle(8);const bool odd=read8(0xffff8901)&1;idle(4);
            if(!branch(0xb80a,!odd)) {
                const bool queued=tst(0xb80e,0xffff8906);
                if(!branch(0xb812,!queued)) {
                    pc=0xb816;r.a[0]=0xffff890c;idle(8);
                    pc=0xb81a;idle(4);word(r.d[1],read16(0xffff890a));idle(4);
                    pc=0xb81e;idle(6);const auto sound=read8(r.a[0]+int16_t(r.d[1]));idle(4);
                    write8(0xe8017,sound);idle(8);
                    pc=0xb826;word(r.d[1],r.d[1]+1);idle(4);
                    pc=0xb828;word(r.d[1],r.d[1]&0x3f);idle(8);
                    pc=0xb82c;idle(4);write16(0xffff890a,r.d[1]);idle(4);
                    pc=0xb830;idle(4);const auto count=read8(0xffff8906);idle(4);write8(0xffff8906,count-1);
                }
            }
            ret=0xb834;
        }
        // $00B834/$00B83E RTS: two stack and two prefetch phases.
        pc=ret;const uint32_t returnpc=read32(sp);sp+=4;idle(8);
        if(returnpc!=0x14666) throw std::runtime_error("corrupt service JSR stack");
        // $014666 MOVEM.L: first prefetch, 10 register words plus the
        // documented final dummy read, final prefetch (52 clocks).
        pc=0x14666;idle(4);
        for(auto* reg:{&r.d[0],&r.d[1],&r.d[2],&r.a[0],&r.a[1]}) {
            *reg=read32(sp);sp+=4;
        }
        read16(sp);idle(4);
    }
    // $01466A/$014672 RTE: SR/PC reads, two return prefetches, 20 clocks.
    pc=c.level==2 ? 0x1466a : 0x14672;
    const uint16_t sr=read16(sp);
    const uint32_t returnpc=read32(sp+2);
    sp+=6;idle(8);clock(ns);
    return {r,returnpc,sp,sr,ns};
}

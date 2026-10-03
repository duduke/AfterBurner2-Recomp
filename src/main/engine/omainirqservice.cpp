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

OMainIrqService::Result OMainIrqService::run(MainBus& b,const Context& c,const Clock& advance,const Boundary& boundary,const LeafObserver& observer) {
    if((c.level!=2 && c.level!=4 && c.level!=6) || c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1))
        throw std::runtime_error("unsupported 68000 service context");
    bus=&b;clock=advance;observe_leaf=observer;ns=c.boundary_ns;pc=c.interrupted_pc;
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
    if(entry!=(c.level==2 ? 0x1461c : c.level==4 ? 0x14674 : 0x1466c))
        throw std::runtime_error("unexpected arcade IRQ vector");
    idle(10); // two handler prefetch bus phases separated by two clocks

    if(c.level==4) return irq4(r,sp,boundary);

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

OMainIrqService::Result OMainIrqService::irq4(OMainObjects::Registers r,uint32_t sp,const Boundary& boundary) {
    // $14674/$14798: immediate byte bit RMW, extensions, operand,
    // prefetch and write (24 clocks for absolute.L, 20 for absolute.W).
    auto bit=[&](uint32_t p,uint32_t a,unsigned mask,bool set,bool absolute_long) {
        pc=p;idle(absolute_long?12:8);auto old=read8(a);idle(4);
        write8(a,set ? old|mask : old&~mask);return bool(old&mask);
    };
    auto branch=[&](uint32_t p,bool taken) {pc=p;idle(taken?10:12);};
    auto save=[&](uint32_t p) {
        pc=p;idle(4);
        for(int i=6;i>=0;--i) {write16(sp-2,r.a[i]);write16(sp-4,r.a[i]>>16);sp-=4;}
        for(int i=7;i>=0;--i) {write16(sp-2,r.d[i]);write16(sp-4,r.d[i]>>16);sp-=4;}
        idle(4);
    };
    auto call=[&](uint32_t p,uint32_t target,bool absolute_long=false) {
        pc=p;idle(absolute_long?8:2);sp-=4;
        const uint32_t ret=p+(absolute_long?6:4);
        write16(sp,ret>>16);write16(sp+2,ret);idle(absolute_long?4:8);
        if(target==0x1495c || target==0x148be || target==0x148b8 || target==0x147e8 || target==0xb7c2 || target==0xb7d2)
            { if(frame_leaf(target,r,sp)!=ret)throw std::runtime_error("corrupt IRQ4 leaf return stack"); }
        else {
            if(!boundary)throw std::runtime_error("IRQ4 callee timing boundary required");
            const unsigned cycles=boundary(target,r,sp,ns);
            idle(cycles);
        }
        sp+=4;
    };
    auto lamps=[&](uint32_t p) {
        pc=p;idle(4);byte(r.d[0],read8(0xffff8b6d));idle(4);
        pc=p+4;idle(8);write8(0x140007,r.d[0]);idle(4);
    };
    auto delay=[&](uint32_t p) {
        pc=p;word(r.d[0],0x39);idle(8);
        // $14730/$14786 DBF: 57 taken decrements, then terminal $FFFF.
        pc=p+4;for(unsigned i=0;i<57;++i) {word(r.d[0],r.d[0]-1);idle(10);}
        word(r.d[0],r.d[0]-1);idle(14);
    };
    auto draw=[&](uint32_t p) {pc=p;idle(12);write8(0x110001,0xff);idle(4);};
    bit(0x14674,0x29c0d6,1,true,true);
    pc=0x1467c;idle(8);auto previous=read16(0x2a3c00);idle(4);write16(0x2a3c00,previous-1);
    const bool logic=int16_t(previous)<=0;branch(0x14682,!logic);
    if(logic) {
        save(0x14686);
        pc=0x1468a;idle(4);read16(0xffff8816);idle(4);write16(0xffff8816,0);
        // $1468E..$146C4: preserve both memory MOVE.L bus orders. The
        // predecrement D0 store writes low first, unlike MOVE (A0)+,(A0).
        const uint32_t addresses[]={0x2a3c04,0x2a3c14,0x2a3c1c,0x2a3c0c};
        for(unsigned i=0;i<4;++i) {
            pc=0x1468e + 14*i;r.a[0]=addresses[i];idle(12);
            pc+=6;idle(4);r.d[0]=read32(r.a[0]+4);idle(4);
            pc+=4;auto first=read32(r.a[0]);r.a[0]+=4;
            write16(r.a[0],first>>16);write16(r.a[0]+2,first);idle(4);
            pc+=2;idle(4);write16(r.a[0]-2,r.d[0]);write16(r.a[0]-4,r.d[0]>>16);r.a[0]-=4;
        }
        call(0x146c6,0x14a86);call(0x146ca,0x13f92);
        const uint32_t bitpc[]={0x146ce,0x146dc,0x146ea,0x146f8};
        const uint32_t addr[]={0xffff913d,0xffff913d,0xffff913e,0xffff9140};
        const uint32_t target[]={0x13bcc,0x13bb6,0x13bfa,0x1329e};
        for(unsigned i=0;i<4;++i) {
            bool requested=bit(bitpc[i],addr[i],i?1:2,false,false);
            branch(bitpc[i]+6,!requested);if(requested)call(bitpc[i]+10,target[i]);
        }
        call(0x14706,0x16c8a,true);call(0x1470c,0x18444,true);call(0x14712,0x1495c);
        lamps(0x14716);call(0x14720,0x148be);call(0x14724,0x17146);call(0x14728,0x13e5c);
        delay(0x1472c);
        pc=0x14734;idle(8);bool visible=read8(0xffff8b6f)&2;idle(4);
        branch(0x1473a,!visible);
        if(visible) {call(0x1473e,0xb7d2,true);draw(0x14744);branch(0x1474c,true);}
    } else {
        save(0x14750);pc=0x14754;idle(8);write16(0xffff8816,0x100);idle(4);
        call(0x1475a,0x148be);call(0x1475e,0x1495c);lamps(0x14762);
        call(0x1476c,0x14a86);call(0x14770,0x13f92);call(0x14774,0x18444,true);
        call(0x1477a,0x17146);call(0x1477e,0x13e5c);delay(0x14782);
        call(0x1478a,0xb7c2,true);draw(0x14790);
    }
    bit(0x14798,0x29c0d6,1,false,true);
    pc=0x147a0;idle(4);auto service=int8_t(read8(0xffff8050));idle(4);
    branch(0x147a4,service<0);
    if(service>=0)branch(0x147a8,service!=0);
    if(!service) {
        call(0x147ac,0x148b8);call(0x147b0,0x14990);call(0x147b4,0xc6f8,true);
        call(0x147ba,0xe2da);call(0x147be,0x147e8);
    }
    pc=service<0?0x147c8:0x147c2;idle(4);
    for(auto& v:r.d) {v=read32(sp);sp+=4;}
    for(auto& v:r.a) {v=read32(sp);sp+=4;}
    read16(sp);idle(4);
    if(service<0) {
        // $147CC..$147E2 discards the old exception frame and pushes
        // service entry $1A130/SR $0700. Preserve D0's actual two pops.
        pc=0x147cc;r.d[0]=read32(sp);sp+=4;idle(4);
        pc=0x147ce;word(r.d[0],read16(sp));sp+=2;idle(4);
        pc=0x147d0;idle(8);read8(0xcdd05);idle(4);write8(0xcdd05,0);
        pc=0x147d6;idle(8);write8(0xffff8050,1);idle(4);
        pc=0x147dc;idle(8);sp-=4;write16(sp,1);write16(sp+2,0xa130);idle(4);
        pc=0x147e2;idle(8);sp-=2;write16(sp,0x700);
    }
    pc=service<0?0x147e6:0x147c6;const auto sr=read16(sp);const auto ret=read32(sp+2);sp+=6;idle(8);clock(ns);
    return {r,ret,sp,sr,ns};
}

uint32_t OMainIrqService::frame_leaf(uint32_t entry,OMainObjects::Registers& r,uint32_t sp) {
    if(observe_leaf)observe_leaf(true,entry,r,ns);
    uint32_t retpc;
    if(entry==0xb7c2 || entry==0xb7d2) {
        // $B7C2/$B7D2..$B7F6: pointer/count loads, four MOVE.L per
        // DBF iteration, including the count+1 terminator descriptor.
        pc=entry;idle(8);r.a[0]=read32(0x2a3c14);idle(4);
        if(entry==0xb7d2) {pc=0xb7d8;r.a[0]+=0xc00;idle(8);pc=0xb7dc;}
        else pc=0xb7c8;
        idle(8);r.a[1]=read32(0x2a3c1c);idle(4);
        if(entry==0xb7c2){pc=0xb7ce;idle(10);}
        pc=0xb7e2;word(r.d[0],read16(r.a[1]));idle(4);
        pc=0xb7e4;r.a[1]=0x100000;idle(12);
        const unsigned count=uint16_t(r.d[0]);
        for(unsigned i=0;i<=count;++i) {
            for(unsigned j=0;j<4;++j) {
                pc=0xb7ea+2*j;const auto v=read32(r.a[0]);r.a[0]+=4;
                write16(r.a[1],v>>16);write16(r.a[1]+2,v);r.a[1]+=4;idle(4);
            }
            pc=0xb7f2;word(r.d[0],r.d[0]-1);idle(i==count?14:10);
        }
        retpc=0xb7f6;
    } else if(entry==0x1495c) {
        // $1495C/$14966 memory-to-memory MOVE.B, then NOT/MOVE.
        pc=0x1495c;idle(8);auto old=read8(0x29c074);idle(4);write8(0x29c075,old);idle(8);
        pc=0x14966;idle(8);byte(r.d[0],read8(0x150001));idle(4);
        pc=0x1496c;byte(r.d[0],~r.d[0]);idle(4);
        pc=0x1496e;idle(8);write8(0x29c074,r.d[0]);idle(4);retpc=0x14974;
    } else if(entry==0x148be) {
        auto step=[&](uint32_t p) {
            pc=p;idle(4);const auto count=uint16_t(read16(r.a[0]+4)+1);idle(4);write16(r.a[0]+4,count);
            pc=p+4;idle(4);read16(r.a[0]+4);idle(4);
            pc=p+8;idle(count<=uint16_t(r.d[0])?10:12);
            if(count>uint16_t(r.d[0])) {
                pc=p+12;idle(8);write16(r.a[0]+4,0);idle(4);
                pc=p+18;auto value=read32(r.a[0])+1;idle(4);write16(r.a[0]+2,value);write16(r.a[0],value>>16);
            }
        };
        pc=0x148be;r.a[0]=0xffff8170;idle(8);
        pc=0x148c2;word(r.d[0],0x3b);idle(8);step(0x148c6);
        pc=0x148da;idle(4);const auto enabled=read16(0xffff880a);idle(4);
        pc=0x148de;idle(enabled?12:10);
        if(enabled) {pc=0x148e2;r.a[0]=0xffff8818;idle(8);step(0x148e6);}
        retpc=0x148fa;
    } else if(entry==0x148b8) {
        pc=0x148b8;idle(4);auto value=read32(0xffff8812)+1;idle(4);
        write16(0xffff8814,value);write16(0xffff8812,value>>16);retpc=0x148bc;
    } else {
        // $147E8..$14816 ordinary/service-button edge; register BTST has
        // its two-clock completion after prefetch.
        pc=0x147e8;idle(4);const auto service=read8(0xffff8050);idle(4);
        pc=0x147ec;idle(service?10:12);
        if(!service) {
            pc=0x147f0;idle(8);byte(r.d[0],read8(0x150001));idle(4);
            pc=0x147f6;word(r.d[0],~r.d[0]);idle(4);
            pc=0x147f8;idle(4);byte(r.d[1],read8(0xffff8051));idle(4);
            pc=0x147fc;idle(4);write8(0xffff8051,r.d[0]);idle(4);
            pc=0x14800;idle(10);pc=0x14804;idle((r.d[0]&2)?10:12);
            if(!(r.d[0]&2)) {
                pc=0x14808;idle(10);pc=0x1480c;idle((r.d[1]&2)?12:10);
                if(r.d[1]&2) {pc=0x14810;idle(8);write8(0xffff8050,0xff);idle(4);}
            }
        }
        retpc=0x14816;
    }
    pc=retpc;const auto ret=read32(sp);idle(8);
    if(observe_leaf)observe_leaf(false,entry,r,ns);
    return ret;
}

OMainIrqService::Result OMainIrqService::logic_step(MainBus& b,const Context& c,const Clock& advance) {
    if(c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1) || c.interrupted_pc!=0x14856)
        throw std::runtime_error("unsupported logic-wait context");
    bus=&b;clock=advance;ns=c.boundary_ns;auto r=c.registers;auto sp=c.supervisor_sp;
    // $14856 absolute.L TST, $1485C short BGE. Flags come from the TST,
    // so V=C=0 and nonnegative words take the branch back to $14856.
    pc=0x14856;idle(8);const auto value=read16(0x2a3c00);idle(4);
    pc=0x1485c;
    if(int16_t(value)>=0) {
        idle(10);clock(ns);return {r,0x14856,sp,uint16_t((c.sr&0xfff0)|(value?0:4)),ns};
    }
    idle(8); // Short conditional branch untaken, unlike the word form.
    // $1485E absolute.L immediate compare; $14866 word BEQ.
    pc=0x1485e;idle(12);const auto compared=read16(0x2a3c00);idle(4);
    pc=0x14866;idle(compared==0xffff?10:12);
    unsigned x=c.sr&16;
    if(compared!=0xffff) {
        pc=0x1486a;idle(4);const auto missed=uint16_t(read16(0xffff8ba4)+1);idle(4);write16(0xffff8ba4,missed);x=missed==0?16:0;
    }
    // $1486E re-arm, $14876 NOP, $14878 RTS.
    pc=0x1486e;idle(12);write16(0x2a3c00,1);idle(4);
    pc=0x14876;idle(4);pc=0x14878;const auto ret=read32(sp);sp+=4;idle(8);clock(ns);
    // MOVE.W #1 clears NZVC and leaves X from CMPI or ADDQ.
    return {r,ret,sp,uint16_t((c.sr&0xffe0)|x),ns};
}

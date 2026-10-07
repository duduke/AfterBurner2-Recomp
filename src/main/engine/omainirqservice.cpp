// Cycle/bus behavior referenced from MAME m68000 (BSD-3-Clause,
// Olivier Galibert). This is an original bounded native service port;
// the general-purpose MAME CPU core is not included.
#include "engine/omainirqservice.hpp"
#include <stdexcept>
#include "hwmath/m68000_completion.hpp"

namespace {
void word(uint32_t& d,uint16_t v) { d=(d&0xffff0000)|v; }
void byte(uint32_t& d,uint8_t v) { d=(d&0xffffff00)|v; }
}
unsigned OMainIrqService::completion_tail(uint16_t op,uint16_t source) {
    return m68000_completion_tail(op,source);
}
void OMainIrqService::idle(unsigned cycles) { ns+=CYCLE_NS*cycles; }
uint16_t OMainIrqService::read16(uint32_t a) { clock(ns);auto v=bus->read16(a);idle(4);return v; }
uint32_t OMainIrqService::read32(uint32_t a) { const auto high=read16(a);return (uint32_t(high)<<16)|read16(a+2); }
uint8_t OMainIrqService::read8(uint32_t a) { clock(ns);auto v=bus->read8(a);idle(4);return v; }
void OMainIrqService::write16(uint32_t a,uint16_t v) { clock(ns);bus->write16(a,v);idle(4); }
void OMainIrqService::write8(uint32_t a,uint8_t v) { clock(ns);bus->write8(a,v);idle(4); }

OMainIrqService::Result OMainIrqService::enter(MainBus& b,const Context& c,const Clock& advance) {
    if((c.level!=2 && c.level!=4 && c.level!=6) || c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1))
        throw std::runtime_error("unsupported 68000 entry context");
    bus=&b;clock=advance;ns=c.boundary_ns;pc=c.interrupted_pc;
    uint32_t sp=c.supervisor_sp;
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

    clock(ns);
    return {c.registers,entry,sp,uint16_t((c.sr&0x38ff)|0x2000|(c.level<<8)),ns};
}

OMainIrqService::Result OMainIrqService::run(MainBus& b,const Context& c,const Clock& advance,const Boundary& boundary,const LeafObserver& observer) {
    if((c.level!=2 && c.level!=4 && c.level!=6) || c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1))
        throw std::runtime_error("unsupported 68000 service context");
    return finish(b,enter(b,c,advance),advance,boundary,observer);
}

OMainIrqService::Result OMainIrqService::finish(MainBus& b,const Result& initial,const Clock& advance,const Boundary& boundary,const LeafObserver& observer) {
    const unsigned level=(initial.sr>>8)&7;
    const uint32_t target=level==2?0x1461c:level==4?0x14674:level==6?0x1466c:0;
    if(!target || initial.pc!=target || !(initial.sr&0x2000) || initial.ns%CYCLE_NS || (initial.sp&1))
        throw std::runtime_error("unsupported entered IRQ context");
    bus=&b;clock=advance;ns=initial.ns;pc=initial.pc;
    observe_leaf=observer;
    auto r=initial.registers;uint32_t sp=initial.sp;
    const uint32_t entry=initial.pc;

    if(level==4) return irq4(r,sp,boundary);

    // $01461C/$01466C MOVE.W D0,abs.L: two extension/prefetch phases,
    // data write at +8 and final prefetch, 16 clocks total.
    pc=entry;idle(8);write16(0xe8012,r.d[0]);idle(4);
    if(level==2) {
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
    pc=level==2 ? 0x1466a : 0x14672;
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
        if(target==0x1495c || target==0x148be || target==0x148b8 || target==0x147e8 || target==0xb7c2 || target==0xb7d2 || target==0x13bb6 || target==0x13bcc || target==0x13bfa || target==0x1329e)
            { if(frame_leaf(target,r,sp)!=ret)throw std::runtime_error("corrupt IRQ4 leaf return stack"); }
        else if(target==0x14a86 || target==0x13f92 || target==0x16c8a || target==0x18444 || target==0x13e5c) {
            if(frame_complex(target,r,sp)!=ret)throw std::runtime_error("corrupt IRQ4 callee return stack");
        }
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
    } else if(entry==0x13bb6) {
        // $13BB6..$13BCA: full road bank, 32 postincrement longs.
        pc=0x13bb6;r.a[0]=0x123800;idle(12);
        pc=0x13bbc;idle(4);r.a[1]=read32(0xffff9142);idle(4);
        pc=0x13bc0;word(r.d[0],0x1f);idle(8);
        for(unsigned i=0;i<32;++i) {
            pc=0x13bc4;const auto v=read32(r.a[1]);r.a[1]+=4;
            write16(r.a[0],v>>16);write16(r.a[0]+2,v);r.a[0]+=4;idle(4);
            pc=0x13bc6;word(r.d[0],r.d[0]-1);idle(i==31?14:10);
        }
        retpc=0x13bca;
    } else if(entry==0x13bcc) {
        // $13BCC..$13BEE: byte selectors, word masks/shifts, signed ADDA.
        pc=0x13bcc;r.a[1]=0x30cf4;idle(12);
        pc=0x13bd2;idle(4);byte(r.d[0],read8(0xffff9115));idle(4);
        pc=0x13bd6;word(r.d[0],r.d[0]&0xff);idle(8);
        pc=0x13bda;word(r.d[0],r.d[0]<<4);idle(14);
        pc=0x13bdc;r.a[1]+=int16_t(r.d[0]);idle(8);
        pc=0x13bde;r.a[0]=0x123800;idle(12);
        pc=0x13be4;idle(4);byte(r.d[0],read8(0xffff9114));idle(4);
        pc=0x13be8;word(r.d[0],r.d[0]&7);idle(8);
        pc=0x13bec;word(r.d[0],r.d[0]<<4);idle(14);
        pc=0x13bee;r.a[0]+=int16_t(r.d[0]);idle(8);
        // $13BF0..$13BF6: MOVE.L reads high/low, writes high/low,
        // then prefetches. ROM colors are runtime operands, not transcripts.
        for(unsigned i=0;i<4;++i) {
            pc=0x13bf0+2*i;const auto v=read32(r.a[1]);r.a[1]+=4;
            write16(r.a[0],v>>16);write16(r.a[0]+2,v);r.a[0]+=4;idle(4);
        }
        retpc=0x13bf8;
    } else if(entry==0x13bfa) {
        // $13BFA..$13C24: doubled word stage index, indexed LEA (12
        // clocks), one road color, then the repeated road bank.
        pc=0x13bfa;idle(8);word(r.d[1],read16(0x29c060));idle(4);
        pc=0x13c00;word(r.d[1],r.d[1]+r.d[1]);idle(4);
        pc=0x13c02;r.a[0]=0x12387c;idle(12);
        pc=0x13c08;r.a[1]=0x30d54;idle(12);
        pc=0x13c0e;r.a[1]+=int16_t(r.d[1]);idle(12);
        pc=0x13c12;const auto color=read16(r.a[1]);write16(r.a[0],color);r.a[0]+=2;idle(4);
        pc=0x13c14;r.a[0]=0x123dc0;idle(12);
        pc=0x13c1a;r.a[1]=0x30d54;idle(12);
        pc=0x13c20;r.a[1]+=int16_t(r.d[1]);idle(12);
        pc=0x13c24;word(r.d[0],7);idle(8);
        for(unsigned i=0;i<8;++i) {
            // $13C28..$13C36: eight separate (A1) reads per DBF,
            // even though the source address is unchanged.
            for(unsigned j=0;j<8;++j) {
                pc=0x13c28+2*j;const auto v=read16(r.a[1]);write16(r.a[0],v);r.a[0]+=2;idle(4);
            }
            pc=0x13c38;word(r.d[0],r.d[0]-1);idle(i==7?14:10);
        }
        retpc=0x13c3c;
    } else if(entry==0x1329e) {
        // $1329E..$132B0: subtract in word width, zero exits before
        // unsigned BCS selects the alternate palette for differences 1..5.
        pc=0x1329e;idle(8);word(r.d[0],read16(0x29c09c));idle(4);
        pc=0x132a4;word(r.d[0],r.d[0]-0x11);idle(8);
        pc=0x132a8;const bool empty=uint16_t(r.d[0])==0;idle(empty?10:12);
        retpc=0x132e8;
        if(!empty) {
            pc=0x132ac;idle(8);
            pc=0x132b0;const bool alternate=uint16_t(r.d[0])<6;idle(alternate?10:12);
            // $132B4..$132D4 / $132EA..$1330A: phase shift/mask,
            // signed ADDA.W and optional destination in fade RAM.
            const uint32_t base=alternate?0x132ea:0x132b4;
            pc=base;idle(4);word(r.d[0],read16(0xffff9146));idle(4);
            pc=base+4;word(r.d[0],r.d[0]<<(alternate?1:3));idle(alternate?8:12);
            pc=base+6;word(r.d[0],r.d[0]&(alternate?0x1e0:0x3e0));idle(8);
            pc=base+10;r.a[0]=alternate?0x35492:0x35192;idle(12);
            pc=base+16;r.a[0]+=int16_t(r.d[0]);idle(8);
            pc=base+18;r.a[1]=0x121e82;idle(12);
            pc=base+24;idle(8);const auto fading=read8(0xcdd05);idle(4);
            pc=base+30;idle(fading?8:10);
            if(fading){pc=base+32;r.a[1]=0xcdab0;idle(12);}
            // $132DA..$132E6 / $13310..$1331C: seven runtime ROM
            // longs, high/low source reads and destination writes.
            const uint32_t moves=alternate?0x13310:0x132da;
            for(unsigned i=0;i<7;++i) {
                pc=moves+2*i;const auto v=read32(r.a[0]);r.a[0]+=4;
                write16(r.a[1],v>>16);write16(r.a[1]+2,v);r.a[1]+=4;idle(4);
            }
            retpc=alternate?0x1331e:0x132e8;
        }
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

OMainIrqService::Instruction OMainIrqService::caller_step(MainBus& b,const Context& c,const Clock& advance,const Poll& poll) {
    if(c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1) || !poll || (c.sr&0xc000))
        throw std::runtime_error("unsupported caller instruction context");
    bus=&b;clock=advance;ns=c.boundary_ns;pc=c.interrupted_pc;
    auto r=c.registers;auto sp=c.supervisor_sp;auto sr=c.sr;
    uint32_t next=pc;unsigned selected=0;uint64_t selection=0;
    auto latch=[&] { selection=ns;clock(ns);auto level=poll(ns);
        if(level!=0&&level!=2&&level!=4&&level!=6)throw std::runtime_error("unsupported caller IPL");
        selected=eligible(level,sr)?level:0; };
    auto final=[&](uint32_t address) { latch();read16(address); };
    auto nz=[&](uint16_t value) { sr=(sr&0xfff0)|(value?0:4)|(value&0x8000?8:0); };
    auto arithmetic=[&](uint16_t old,uint16_t value,bool subtract,bool x) {
        const bool carry=subtract?old<1:old==0xffff;
        const bool overflow=subtract?old==0x8000:old==0x7fff;
        sr=(sr&0xfff0)|(value?0:4)|(value&0x8000?8:0)|(overflow?2:0)|(carry?1:0);
        if(x)sr=(sr&~16)|(carry?16:0);
    };
    auto branch=[&](bool take,uint32_t target,unsigned length) {
        next=take?target:pc+length;idle(2);
        if(take) {read16(next);final(next+2);}
        else {idle(2);if(length==4)read16(pc+4);final(next+2);}
    };
    switch(pc) {
    // $1458A/$14590: main loop's road pointer and BSR wait caller.
    case 0x1458a:r.a[6]=0xe4000;read16(pc+4);read16(pc+6);final(pc+8);next=pc+6;break;
    case 0x14590:
        idle(2);sp-=4;write16(sp,1);write16(sp+2,0x4594);
        read16(0x14856);final(0x14858);next=0x14856;break;
    // $14594..$145B8: live 254-record dispatcher, handler stays a boundary.
    case 0x14594: {
        read16(pc+4);read16(pc+6);auto value=read16(0x1c176);
        read16(pc+8);nz(value);write16(0xffff8806,value);final(pc+10);next=pc+8;break;
    }
    case 0x1459c:r.a[5]=0xffff9fc0;read16(pc+4);final(pc+6);next=pc+4;break;
    case 0x145a0:r.a[5]+=64;read16(pc+4);final(pc+6);next=pc+4;break;
    case 0x145a4: {
        read16(pc+4);auto value=read8(r.a[5]);sr=(sr&~4)|(value&0x80?0:4);
        final(pc+6);next=pc+4;break;
    }
    case 0x145a8:branch(sr&4,0x145b2,4);break;
    case 0x145ac:read16(pc+4);r.a[0]=read32(r.a[5]+16);final(pc+6);next=pc+4;break;
    case 0x145b0:
        next=r.a[0];read16(next);sp-=4;write16(sp,1);write16(sp+2,0x45b2);final(next+2);break;
    case 0x145b2: {
        read16(pc+4);auto old=read16(0xffff8806);auto value=uint16_t(old-1);
        arithmetic(old,value,true,true);read16(pc+6);latch();write16(0xffff8806,value);next=pc+4;break;
    }
    case 0x145b6:branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x145a0,2);break;
    case 0x145b8:branch(true,0x1458a,2);break;
    // $14856..$14878: preserve flags and complete each instruction before IRQ.
    case 0x14856:read16(pc+4);read16(pc+6);nz(read16(0x2a3c00));final(pc+8);next=pc+6;break;
    case 0x1485c:branch(bool(sr&8)==bool(sr&2),0x14856,2);break;
    case 0x1485e: {
        read16(pc+4);read16(pc+6);read16(pc+8);auto old=read16(0x2a3c00);auto value=uint16_t(old-0xffff);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x8000?8:0)|((old&0x8000)==0&&value&0x8000?2:0)|(old<0xffff?1:0);
        final(pc+10);next=pc+8;break;
    }
    case 0x14866:branch(sr&4,0x1486e,4);break;
    case 0x1486a: {
        read16(pc+4);auto old=read16(0xffff8ba4);auto value=uint16_t(old+1);
        arithmetic(old,value,false,true);read16(pc+6);latch();write16(0xffff8ba4,value);next=pc+4;break;
    }
    case 0x1486e: {
        constexpr uint16_t armed=1;
        read16(pc+4);read16(pc+6);nz(armed);read16(pc+8);write16(0x2a3c00,armed);final(pc+10);next=pc+8;break;
    }
    case 0x14876:final(pc+4);next=pc+2;break;
    case 0x14878:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $B676..$B696: publication wait and main-owned count/pointer operands.
    case 0xb676:read16(pc+4);read16(pc+6);nz(read16(0x2a3c00));final(pc+8);next=pc+6;break;
    case 0xb67c:branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0xb676,2);break;
    case 0xb67e:read16(pc+4);read16(pc+6);r.a[0]=read32(0x2a3c0c);final(pc+8);next=pc+6;break;
    case 0xb684: {
        read16(pc+4);read16(pc+6);auto value=read16(0x2a3cf0);nz(value);
        write16(r.a[0],value);final(pc+8);next=pc+6;break;
    }
    case 0xb68a: {
        auto value=read16(r.a[0]);nz(value);read16(pc+4);write16(0x29c058,value);
        read16(pc+6);final(pc+8);next=pc+6;break;
    }
    case 0xb690:
        read16(pc+4);read16(pc+6);read16(0x2a3cf0);nz(0);read16(pc+8);
        latch();write16(0x2a3cf0,0);next=pc+6;break;
    case 0xb696:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    default:throw std::runtime_error("unported caller instruction");
    }
    clock(ns);return {{r,next,sp,sr,ns},selected,selection};
}

// Cycle/bus behavior referenced from MAME m68000 (BSD-3-Clause,
// Olivier Galibert). This is an original bounded native service port;
// the general-purpose MAME CPU core is not included.
#include "engine/omainirqservice.hpp"
#include <stdexcept>
#include <string>
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
    return enter_impl(b,c,advance,0,{},false);
}

OMainIrqService::Result OMainIrqService::enter_joint(MainBus& b,const Context& c,const Clock& advance,uint64_t bias,const Clock& acknowledge,const ReturnSelection& selection) {
    if(!eligible(c.level,c.sr))throw std::runtime_error("masked joint main IRQ entry");
    return enter_impl(b,c,advance,bias,acknowledge,true,selection);
}

OMainIrqService::Result OMainIrqService::enter_impl(MainBus& b,const Context& c,const Clock& advance,uint64_t bias,const Clock& acknowledge,bool program,const ReturnSelection& selection) {
    if((c.level!=2 && c.level!=4 && c.level!=6) || c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1))
        throw std::runtime_error("unsupported 68000 entry context");
    bus=&b;clock=advance;ns=c.boundary_ns;pc=c.interrupted_pc;
    uint32_t sp=c.supervisor_sp;
    // 68000 exception entry: six internal clocks, PC low; CPU-space IACK
    // uses the ten-clock E phase (VPA), one after-delay clock, four bus
    // clocks, then four internal clocks. Source: MAME m68000 state_interrupt
    // and vpa_sync/vpa_after, independent of this call's captured timestamps.
    idle(6);write16(sp-2,c.return_pc);
    const uint64_t cycles=ns/CYCLE_NS-bias,mod=cycles%10;
    const uint64_t sync=cycles-mod+(mod<7 ? 10 : 20);
    if(acknowledge)acknowledge(ns+(sync-cycles)*CYCLE_NS);
    idle(static_cast<unsigned>(sync-cycles)+1+4+4);
    sp-=6;write16(sp,c.sr);write16(sp+2,c.return_pc>>16);
    const uint32_t vector=(24+c.level)*4;
    const uint32_t entry=read32(vector);
    if(entry!=(c.level==2 ? 0x1461c : c.level==4 ? 0x14674 : 0x1466c))
        throw std::runtime_error("unexpected arcade IRQ vector");
    const auto sr=uint16_t((c.sr&0x38ff)|0x2000|(c.level<<8));
    if(program){read16(entry);idle(2);
        // state_interrupt_df mmrw3 selects after trap9's two clocks,
        // before the last prefetch; a higher IRQ can precede the handler.
        if(selection){clock(ns);selection(sr,ns);}read16(entry+2);}
    else idle(10); // legacy data-only oracle omits ROM prefetch observations

    clock(ns);
    return {c.registers,entry,sp,sr,ns};
}

OMainIrqService::Result OMainIrqService::run(MainBus& b,const Context& c,const Clock& advance,const Boundary& boundary,const LeafObserver& observer) {
    if((c.level!=2 && c.level!=4 && c.level!=6) || c.boundary_ns%CYCLE_NS || (c.supervisor_sp&1))
        throw std::runtime_error("unsupported 68000 service context");
    return finish(b,enter(b,c,advance),advance,boundary,observer);
}

OMainIrqService::Result OMainIrqService::finish(MainBus& b,const Result& initial,const Clock& advance,const Boundary& boundary,const LeafObserver& observer,const ReturnSelection& selection) {
    const unsigned level=(initial.sr>>8)&7;
    const uint32_t target=level==2?0x1461c:level==4?0x14674:level==6?0x1466c:0;
    if(!target || initial.pc!=target || !(initial.sr&0x2000) || initial.ns%CYCLE_NS || (initial.sp&1))
        throw std::runtime_error("unsupported entered IRQ context");
    bus=&b;clock=advance;ns=initial.ns;pc=initial.pc;
    observe_leaf=observer;
    auto r=initial.registers;uint32_t sp=initial.sp;
    const uint32_t entry=initial.pc;

    if(level==4) {
        return irq4(r,sp,boundary,selection);
    }

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
    // MAME rte: restore SR after the final stack read, prefetch the
    // return opcode, then latch IPL before the return-PC+2 prefetch.
    sp+=6;
    // ROM program prefetches are outside this service's data-bus transcript.
    // A RAM return target is mutable bus data and must be read, including
    // supervisor returns to the directed caller's real SR-writing code.
    if((returnpc&0x3fffff)>=0x80000)read16(returnpc);else idle(4);
    clock(ns);if(selection)selection(sr,ns);
    if(((returnpc+2)&0x3fffff)>=0x80000)read16(returnpc+2);else idle(4);
    clock(ns);
    return {r,returnpc,sp,sr,ns};
}

OMainIrqService::Result OMainIrqService::irq4(OMainObjects::Registers r,uint32_t sp,const Boundary&,const ReturnSelection& selection) {
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
        if(target==0x14990 || target==0xe2da || target==0x1495c || target==0x148be || target==0x148b8 || target==0x147e8 || target==0xb7c2 || target==0xb7d2 || target==0x13bb6 || target==0x13bcc || target==0x13bfa || target==0x1329e)
            { if(frame_leaf(target,r,sp)!=ret)throw std::runtime_error("corrupt IRQ4 leaf return stack"); }
        else if(target==0x17146 || target==0xc6f8 || target==0x14a86 || target==0x13f92 || target==0x16c8a || target==0x18444 || target==0x13e5c) {
            if(frame_complex(target,r,sp)!=ret)throw std::runtime_error("corrupt IRQ4 callee return stack");
        }
        else {
            throw std::runtime_error("unsupported IRQ4 callee");
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
    pc=service<0?0x147e6:0x147c6;const auto sr=read16(sp);const auto ret=read32(sp+2);sp+=6;
    // IRQ4 RTE has the same mutable RAM return-prefetch contract as IRQ2/6.
    if((ret&0x3fffff)>=0x80000)read16(ret);else idle(4);
    clock(ns);if(selection)selection(sr,ns);
    if(((ret+2)&0x3fffff)>=0x80000)read16(ret+2);else idle(4);clock(ns);
    return {r,ret,sp,sr,ns};
}

uint32_t OMainIrqService::frame_leaf(uint32_t entry,OMainObjects::Registers& r,uint32_t sp) {
    if(observe_leaf)observe_leaf(true,entry,r,ns);
    uint32_t retpc;
    if(entry==0x14990) {
        // $014990: RTS. Both stack words and the eight prefetch clocks
        // use the caller's computed clock; no game-flow callback executes.
        pc=0x14990;const auto ret=read32(sp);idle(8);
        if(observe_leaf)observe_leaf(false,entry,r,ns);
        return ret;
    } else if(entry==0xe2da) {
        // $00E2DA: RTS (the helper at $E2DC is a different entry).
        pc=0xe2da;const auto ret=read32(sp);idle(8);
        if(observe_leaf)observe_leaf(false,entry,r,ns);
        return ret;
    } else if(entry==0xb7c2 || entry==0xb7d2) {
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
        pc=0x148b8;idle(4);auto value=read32(0xffff8812)+1;callee_x=value==0;idle(4);
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

OMainIrqService::Instruction OMainIrqService::caller_step(MainBus& b,const Context& c,const Clock& advance,const Poll& poll,const Clock& reset) {
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
    auto addsub_byte=[&](uint8_t old,uint8_t source,bool subtract,bool extend) {
        const auto value=uint8_t(subtract?old-source:old+source);
        const bool carry=subtract?old<source:unsigned(old)+source>0xff;
        const bool overflow=((subtract?(old^source):~(old^source))&(old^value)&0x80)!=0;
        sr=(sr&0xfff0)|(carry?1:0)|(overflow?2:0)|(value?0:4)|(value&0x80?8:0);
        if(extend)sr=(sr&~16)|(carry?16:0);
        return value;
    };
    auto addsub_word=[&](uint16_t old,uint16_t source,bool subtract,bool extend) {
        const auto value=uint16_t(subtract?old-source:old+source);
        const bool carry=subtract?old<source:unsigned(old)+source>0xffff;
        const bool overflow=((subtract?(old^source):~(old^source))&(old^value)&0x8000)!=0;
        sr=(sr&0xfff0)|(carry?1:0)|(overflow?2:0)|(value?0:4)|(value&0x8000?8:0);
        if(extend)sr=(sr&~16)|(carry?16:0);
        return value;
    };
    auto addsub_long=[&](uint32_t old,uint32_t source,bool subtract,bool extend) {
        const auto value=uint32_t(subtract?old-source:old+source);
        const bool carry=subtract?old<source:uint64_t(old)+source>0xffffffff;
        const bool overflow=((subtract?(old^source):~(old^source))&(old^value)&0x80000000)!=0;
        sr=(sr&0xfff0)|(carry?1:0)|(overflow?2:0)|(value?0:4)|(value&0x80000000?8:0);
        if(extend)sr=(sr&~16)|(carry?16:0);
        return value;
    };
    auto asl_word=[&](unsigned reg,unsigned count) {
        auto value=uint16_t(r.d[reg]);read16(pc+4);bool carry=false,overflow=false;
        for(unsigned i=0;i<count;++i){carry=value&0x8000;const auto shifted=uint16_t(value<<1);
            overflow|=bool((value^shifted)&0x8000);value=shifted;idle(2);}
        word(r.d[reg],value);sr=(sr&(count?0xffe0:0xfff0))|(carry?0x11:0)|(overflow?2:0)|(value?0:4)|(value&0x8000?8:0);
        latch();idle(2);next=pc+2;
    };
    auto asr_long=[&](unsigned reg,unsigned count) {
        auto value=r.d[reg];read16(pc+4);bool carry=false;
        for(unsigned i=0;i<count;++i){carry=value&1;value=(value>>1)|(value&0x80000000);idle(2);}
        r.d[reg]=value;sr=(sr&(count?0xffe0:0xfff0))|(carry?0x11:0)|(value?0:4)|(value&0x80000000?8:0);
        idle(2);latch();idle(2);next=pc+2;
    };
    auto asr_word=[&](unsigned reg,unsigned count){
        auto value=uint16_t(r.d[reg]);read16(pc+4);bool carry=false;
        for(unsigned i=0;i<count;++i){carry=value&1;value=(value>>1)|(value&0x8000);idle(2);}
        word(r.d[reg],value);sr=(sr&(count?0xffe0:0xfff0))|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0);
        latch();idle(2);next=pc+2;
    };
    auto asl_long=[&](unsigned reg,unsigned count){
        auto value=r.d[reg];read16(pc+4);bool carry=false,overflow=false;
        for(unsigned i=0;i<count;++i){carry=value&0x80000000;const auto shifted=value<<1;
            overflow|=bool((value^shifted)&0x80000000);value=shifted;idle(2);}
        r.d[reg]=value;sr=(sr&(count?0xffe0:0xfff0))|(carry?0x11:0)|(overflow?2:0)|(value?0:4)|(value&0x80000000?8:0);
        idle(2);latch();idle(2);next=pc+2;
    };
    // Register shifts and decimal DIVU follow the MAME 68000 microcode.
    auto logical_shift=[&](unsigned reg,unsigned size,unsigned count,bool left,bool rotate){
        const uint32_t mask=size==4?0xffffffffu:size==1?0xffu:0xffffu,sign=size==4?0x80000000u:size==1?0x80u:0x8000u;
        auto value=r.d[reg]&mask;bool carry=false;read16(pc+4);
        for(unsigned i=0;i<count;++i){carry=left?bool(value&sign):bool(value&1);
            value=left?((value<<1)|(rotate&&carry?1u:0u))&mask:(value>>1)|(rotate&&carry?sign:0);idle(2);}
        if(size==4)r.d[reg]=value;else if(size==1)byte(r.d[reg],value);else word(r.d[reg],value);
        sr=(sr&(rotate||!count?0xfff0:0xffe0))|(carry?(rotate?1:17):0)|(value?0:4)|(value&sign?8:0);
        if(size==4)idle(2);latch();idle(2);next=pc+2;
    };
    auto multiply_unsigned=[&](unsigned reg,uint16_t operand,uint32_t target){
        unsigned ones=0;for(auto v=operand;v;v>>=1)ones+=v&1;
        const uint32_t value=uint32_t(uint16_t(r.d[reg]))*operand;
        read16(target+2);idle(32+2*ones);latch();idle(2);
        r.d[reg]=value;sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000u?8:0);next=target;
    };
    auto divide_unsigned=[&](unsigned reg,uint16_t divisor,uint32_t target){
        const auto dividend=r.d[reg];
        if(!divisor||(dividend>>16)>=divisor)throw std::runtime_error("unverified joint callee DIVU exception/overflow");
        unsigned cycles=38;uint32_t partial=dividend,high=uint32_t(divisor)<<16;
        for(unsigned i=0;i<15;++i){const auto old=partial;partial<<=1;
            if(old&0x80000000)partial-=high;else{cycles+=2;if(partial>=high){partial-=high;--cycles;}}}
        idle(cycles*2-4);r.d[reg]=((dividend%divisor)<<16)|(dividend/divisor);nz(dividend/divisor);
        final(target+2);next=target;
    };
    auto bcd_register=[&](unsigned src,unsigned dst,bool subtract){
        const unsigned a=r.d[src]&255,b=r.d[dst]&255,carry=bool(sr&16);unsigned result,raw;
        if(subtract){unsigned low=(b&15)-(a&15)-carry;raw=uint16_t(b-a-carry);result=raw;
            if(low&0x10)result-=6;if(raw&0x100)result-=0x60;
        }else{unsigned low=(a&15)+(b&15)+carry;raw=a+b+carry;result=raw;
            if(low>9)result+=6;if(result>0x9f)result+=0x60;
        }
        const bool c=result&0x300,z=(sr&4)&&!uint8_t(result),n=result&0x80;
        const bool v=subtract?!(result&0x80)&&(raw&0x80):(result&0x80)&&!(raw&0x80);
        read16(pc+4);latch();idle(2);byte(r.d[dst],result);
        sr=(sr&0xffe0)|(c?17:0)|(z?4:0)|(n?8:0)|(v?2:0);next=pc+2;
    };
    auto decimal_divu=[&](){
        const uint32_t dividend=r.d[0],divisor=uint16_t(r.d[1]);
        if(!divisor||(dividend>>16)>=divisor)throw std::runtime_error("unverified decimal DIVU exception/overflow");
        unsigned cycles=38;uint32_t partial=dividend,high=divisor<<16;
        for(unsigned i=0;i<15;++i){const auto old=partial;partial<<=1;
            if(old&0x80000000)partial-=high;else{cycles+=2;if(partial>=high){partial-=high;--cycles;}}}
        idle(cycles*2-4);r.d[0]=((dividend%divisor)<<16)|(dividend/divisor);nz(dividend/divisor);
        final(pc+4);next=pc+2;
    };
    auto branch=[&](bool take,uint32_t target,unsigned length) {
        next=take?target:pc+length;idle(2);
        if(take) {read16(next);final(next+2);}
        else {idle(2);if(length==4)read16(pc+4);final(next+2);}
    };
    switch(pc) {
    // $14840 RESET: rset1/rset2, 31 four-clock rset3/rset4 loops,
    // rset5 prefetch. Both callbacks pulse the sub CPU on the X-Board.
    case 0x14840:
        if(!(sr&0x2000)||!reset)throw std::runtime_error("missing supervisor RESET owner");
        idle(4);reset(ns);idle(124);reset(ns);latch();read16(pc+4);
        next=pc+2;break;
#include "engine/omainirqjointcalleeschedule.inc"
#include "engine/omainremainingschedule.inc"
#include "engine/omainproducerschedule.inc"
#include "engine/omainirqwrapperschedule.inc"
#include "engine/omaininputschedule.inc"
#include "engine/omainflightschedule.inc"
#include "engine/omaincompanionschedule.inc"
#include "engine/omainweaponschedule.inc"
#include "engine/omainhostcallerschedule.inc"
    // $D0DA: ADDQ.W #1,$8B5A, exposed by the directed producer's
    // restored-mask schedule. The following button call is timed by the input graph.
    case 0xd0da: {
        read16(pc+4);const auto old=read16(0xffff8b5a);
        const auto v=addsub_word(old,1,false,true);read16(pc+6);
        latch();write16(0xffff8b5a,v);next=pc+4;break;
    }
    // $B84C..$B884: sound-ring producer. Keep operand/prefetch ordering
    // and XNZVC through enqueue, disabled reset and stage-one suppression.
    case 0xb84c: {
        read16(pc+4);const auto v=read8(0xffff8902);
        sr=(sr&0xfff0)|(v?0:4)|(v&0x80?8:0);
        final(pc+6);next=pc+4;break;
    }
    case 0xb850:branch(sr&4,0xb87c,4);break;
    case 0xb854: {
        read16(pc+4);read16(pc+6);read16(pc+8);
        addsub_word(read16(0x29c09c),1,true,false);
        final(pc+10);next=pc+8;break;
    }
    case 0xb85c:branch(sr&4,0xb884,4);break;
    case 0xb860:read16(pc+4);word(r.d[1],read16(0xffff8908));nz(r.d[1]);final(pc+6);next=pc+4;break;
    case 0xb864:r.a[0]=0xffff890c;read16(pc+4);final(pc+6);next=pc+4;break;
    // $B868 indexed byte destination: two internal EA clocks precede
    // extension prefetch; the final prefetch follows the byte write.
    case 0xb868: {
        idle(2);read16(pc+4);const auto v=uint8_t(r.d[0]);
        sr=(sr&0xfff0)|(v?0:4)|(v&0x80?8:0);
        write8(r.a[0]+int16_t(r.d[1]),v);final(pc+6);next=pc+4;break;
    }
    case 0xb86c:word(r.d[1],addsub_word(r.d[1],1,false,true));final(pc+4);next=pc+2;break;
    case 0xb86e:read16(pc+4);word(r.d[1],r.d[1]&0x3f);nz(r.d[1]);final(pc+6);next=pc+4;break;
    case 0xb872:read16(pc+4);nz(r.d[1]);write16(0xffff8908,r.d[1]);final(pc+6);next=pc+4;break;
    case 0xb876: {
        read16(pc+4);const auto old=read8(0xffff8906),v=uint8_t(old+1);
        sr=(sr&0xffe0)|(v?0:4)|(v&0x80?8:0)|(old==0x7f?2:0)|(old==0xff?0x11:0);
        read16(pc+6);latch();write8(0xffff8906,v);next=pc+4;break;
    }
    case 0xb87c: {
        read16(pc+4);read16(0xffff8908);nz(0);read16(pc+6);
        latch();write16(0xffff8908,0);next=pc+4;break;
    }
    case 0xb880: {
        read16(pc+4);read8(0xffff8906);sr=(sr&0xfff0)|4;read16(pc+6);
        latch();write8(0xffff8906,0);next=pc+4;break;
    }
    case 0xb87a:case 0xb884:next=read32(sp);sp+=4;read16(next);final(next+2);break;
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
    // Returned object instructions seen after IRQ2/6 in the natural corpus.
    // $88C2 installs the live handler; long MOVE sets NZVC, preserves X.
    case 0x88c2:
        read16(pc+4);read16(pc+6);read16(pc+8);
        sr=(sr&0xfff0);write16(r.a[5]+16,0);write16(r.a[5]+18,0x8af2);
        final(pc+10);next=pc+8;break;
    // $8AF2/$7BB6/$14EE2/$C082 copy a long between displacement operands.
    case 0x8af2:case 0x7bb6:case 0x14ee2:case 0xc082:case 0xaaee: {
        read16(pc+4);const auto value=read32(r.a[5]+(pc==0x7bb6?-56:4));
        read16(pc+6);sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        write16(r.a[5]+8,value>>16);write16(r.a[5]+10,value);final(pc+8);next=pc+6;break;
    }
    // $7BBC..$7C10: compound follower copy, stage animation and publication.
    // Literal continuation of $7BB6; all operands come from the arcade bus.
    case 0x7bbc:case 0x7bc2:case 0x7bc8: {
        read16(pc+4);const unsigned size=pc==0x7bbc?1:2;
        const int source=pc==0x7bbc?-62:pc==0x7bc2?-60:-58;
        const unsigned dest=pc==0x7bbc?2:pc==0x7bc2?4:6;
        const auto value=size==1?read8(r.a[5]+source):read16(r.a[5]+source);
        read16(pc+6);
        sr=(sr&0xfff0)|(value?0:4)|(value&(size==1?0x80:0x8000)?8:0);
        if(size==1)write8(r.a[5]+dest,value);else write16(r.a[5]+dest,value);
        final(pc+8);next=pc+6;break;
    }
    case 0x7bce:case 0x7bda: {
        read16(pc+4);read16(pc+6);read16(pc+8);
        addsub_word(read16(0x29c09c),pc==0x7bce?7:1,true,false);
        final(pc+10);next=pc+8;break;
    }
    case 0x7bd6:case 0x7be2:branch(sr&4,0x7bee,4);break;
    case 0x7be6:case 0x7bee:
        r.a[0]=pc==0x7be6?0x2753e:0x2755e;
        read16(pc+4);read16(pc+6);final(pc+8);next=pc+6;break;
    case 0x7bec:branch(true,0x7bf4,2);break;
    case 0x7bf4: {
        read16(pc+4);const auto value=read8(r.a[5]+21);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        final(pc+6);next=pc+4;break;
    }
    case 0x7bf8:branch((sr&4)||bool(sr&8)!=bool(sr&2),0x7c0c,4);break;
    case 0x7bfc: {
        read16(pc+4);const auto old=read8(r.a[5]+21),value=uint8_t(old-4);
        sr=(sr&0xffe0)|(old<4?0x11:0)|(value?0:4)|(value&0x80?8:0)|
            (((old^4)&(old^value)&0x80)?2:0);
        read16(pc+6);latch();write8(r.a[5]+21,value);next=pc+4;break;
    }
    case 0x7c00:word(r.d[0],0);nz(0);final(pc+4);next=pc+2;break;
    case 0x7c02: {
        read16(pc+4);const auto value=read8(r.a[5]+21);byte(r.d[0],value);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        final(pc+6);next=pc+4;break;
    }
    case 0x7c06: {
        idle(2);read16(pc+4);const auto value=read32(r.a[0]+int16_t(r.d[0]));
        read16(pc+6);sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        write16(r.a[5]+12,value>>16);write16(r.a[5]+14,value);
        final(pc+8);next=pc+6;break;
    }
    case 0x7c0c:
        idle(2);sp-=4;write16(sp,0);write16(sp+2,0x7c10);
        read16(0xb628);final(0xb62a);next=0xb628;break;
    case 0x7c10:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $AAEE..$AC4A and $AE32..$AE4E: trail update, projection, append/release.
    // $0AAF4: MOVE.W ($0029C0AC).L,D1
    case 0xaaf4: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c0ac);
        word(r.d[1],value);
        nz(r.d[1]);
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0AAFA: SUB.W D1,36(A5)
    case 0xaafa: {
        read16(pc+4);
        const auto old=read16(r.a[5]+(36));
        const auto value=addsub_word(old,r.d[1],true,true);
        read16(pc+6);
        latch();
        write16(r.a[5]+(36),value);
        next=pc+4;
        break;
    }
    // $0AAFE: MOVE.W 36(A5),D7
    case 0xaafe: {
        read16(pc+4);
        const auto value=read16(r.a[5]+(36));
        word(r.d[7],value);
        nz(r.d[7]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB02: ASR.W #5,D7
    case 0xab02: {
        asr_word(7,5);
        break;
    }
    // $0AB04: MOVE.W D7,D1
    case 0xab04: {
        word(r.d[1],r.d[7]);
        nz(r.d[1]);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AB06: BLT $00AE32
    case 0xab06: {
        branch(bool(sr&8)!=bool(sr&2),0x00ae32,4);
        break;
    }
    // $0AB0A: ADDI.W #$20,D1
    case 0xab0a: {
        read16(pc+4);
        word(r.d[1],addsub_word(r.d[1],0x20,false,true));
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB0E: MOVE.W D1,4(A6)
    case 0xab0e: {
        read16(pc+4);
        nz(uint16_t(r.d[1]));
        write16(r.a[6]+(4),r.d[1]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB12: MOVE.L #$1FE0,16(A6)
    case 0xab12: {
        read16(pc+4);
        read16(pc+6);
        const auto value=uint32_t(0x1fe0);
        read16(pc+8);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        write16(r.a[6]+(16),value>>16);
        write16((r.a[6]+(16))+2,value);
        final(pc+10);
        next=pc+8;
        break;
    }
    // $0AB1A: MOVE.W 8(A6),D0
    case 0xab1a: {
        read16(pc+4);
        const auto value=read16(r.a[6]+(8));
        word(r.d[0],value);
        nz(r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB1E: MOVE.B D0,2(A5)
    case 0xab1e: {
        read16(pc+4);
        sr=(sr&0xfff0)|(uint8_t(r.d[0])?0:4)|(uint8_t(r.d[0])&0x80?8:0);
        write8(r.a[5]+(2),r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB22: CMPI.W #$7,50(A5)
    case 0xab22: {
        read16(pc+4);
        read16(pc+6);
        addsub_word(read16(r.a[5]+(50)),0x7,true,false);
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0AB28: BGT $00AB44
    case 0xab28: {
        branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x00ab44,4);
        break;
    }
    // $0AB2C: ADDQ.W #1,50(A5)
    case 0xab2c: {
        read16(pc+4);
        const auto old=read16(r.a[5]+(50)),value=addsub_word(old,1,false,true);
        read16(pc+6);
        latch();
        write16(r.a[5]+(50),value);
        next=pc+4;
        break;
    }
    // $0AB30: MOVE.W 50(A5),D1
    case 0xab30: {
        read16(pc+4);
        const auto value=read16(r.a[5]+(50));
        word(r.d[1],value);
        nz(r.d[1]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB34: ADDQ.W #8,D1
    case 0xab34: {
        word(r.d[1],addsub_word(r.d[1],8,false,true));
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AB36: MOVE.W D1,-16384(A6)
    case 0xab36: {
        read16(pc+4);
        nz(uint16_t(r.d[1]));
        write16(r.a[6]+(-16384),r.d[1]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB3A: MOVE.W D0,-16382(A6)
    case 0xab3a: {
        read16(pc+4);
        nz(uint16_t(r.d[0]));
        write16(r.a[6]+(-16382),r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB3E: MOVE.L -16380(A6),D0
    case 0xab3e: {
        read16(pc+4);
        const auto value=read32(r.a[6]+(-16380));
        r.d[0]=value;
        sr=(sr&0xfff0)|(r.d[0]?0:4)|(r.d[0]&0x80000000?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB42: ASR.W #4,D0
    case 0xab42: {
        asr_word(0,4);
        break;
    }
    // $0AB44: MOVE.B D0,1(A5)
    case 0xab44: {
        read16(pc+4);
        sr=(sr&0xfff0)|(uint8_t(r.d[0])?0:4)|(uint8_t(r.d[0])&0x80?8:0);
        write8(r.a[5]+(1),r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB48: MOVE.W D7,D6
    case 0xab48: {
        word(r.d[6],r.d[7]);
        nz(r.d[6]);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AB4A: ASR.W #2,D6
    case 0xab4a: {
        asr_word(6,2);
        break;
    }
    // $0AB4C: LEA ($00033E98).L,A0
    case 0xab4c: {
        r.a[0]=0x33e98;
        read16(pc+4);
        read16(pc+6);
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0AB52: MOVE.B 0(A0,D6.W),D6
    case 0xab52: {
        idle(2);
        read16(pc+4);
        const auto value=read8(r.a[0]+int16_t(r.d[6]));
        byte(r.d[6],value);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB56: EXT.W D6
    case 0xab56: {
        word(r.d[6],int8_t(r.d[6]));
        nz(r.d[6]);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AB58: MOVE.W D6,-16384(A6)
    case 0xab58: {
        read16(pc+4);
        nz(uint16_t(r.d[6]));
        write16(r.a[6]+(-16384),r.d[6]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB5C: MOVE.W ($0029C0C4).L,-16382(A6)
    case 0xab5c: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c0c4);
        read16(pc+8);
        nz(uint16_t(value));
        write16(r.a[6]+(-16382),value);
        final(pc+10);
        next=pc+8;
        break;
    }
    // $0AB64: MOVE.L -16380(A6),D6
    case 0xab64: {
        read16(pc+4);
        const auto value=read32(r.a[6]+(-16380));
        r.d[6]=value;
        sr=(sr&0xfff0)|(r.d[6]?0:4)|(r.d[6]&0x80000000?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB68: ASR.L #6,D6
    case 0xab68: {
        asr_long(6,6);
        break;
    }
    // $0AB6A: ADD.W D6,42(A5)
    case 0xab6a: {
        read16(pc+4);
        const auto old=read16(r.a[5]+(42));
        const auto value=addsub_word(old,r.d[6],false,true);
        read16(pc+6);
        latch();
        write16(r.a[5]+(42),value);
        next=pc+4;
        break;
    }
    // $0AB6E: MOVE.W ($0029C092).L,-16384(A6)
    case 0xab6e: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c092);
        read16(pc+8);
        nz(uint16_t(value));
        write16(r.a[6]+(-16384),value);
        final(pc+10);
        next=pc+8;
        break;
    }
    // $0AB76: MOVE.W 42(A5),-16382(A6)
    case 0xab76: {
        read16(pc+4);
        const auto value=read16(r.a[5]+(42));
        read16(pc+6);
        nz(uint16_t(value));
        write16(r.a[6]+(-16382),value);
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0AB7C: MOVE.L -16380(A6),D0
    case 0xab7c: {
        read16(pc+4);
        const auto value=read32(r.a[6]+(-16380));
        r.d[0]=value;
        sr=(sr&0xfff0)|(r.d[0]?0:4)|(r.d[0]&0x80000000?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB80: MOVE.W 44(A5),D6
    case 0xab80: {
        read16(pc+4);
        const auto value=read16(r.a[5]+(44));
        word(r.d[6],value);
        nz(r.d[6]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB84: SUB.W ($0029C080).L,D6
    case 0xab84: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c080);
        word(r.d[6],addsub_word(r.d[6],value,true,true));
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0AB8A: MOVE.W D6,-16384(A6)
    case 0xab8a: {
        read16(pc+4);
        nz(uint16_t(r.d[6]));
        write16(r.a[6]+(-16384),r.d[6]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB8E: MOVE.W ($0029C090).L,-16382(A6)
    case 0xab8e: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c090);
        read16(pc+8);
        nz(uint16_t(value));
        write16(r.a[6]+(-16382),value);
        final(pc+10);
        next=pc+8;
        break;
    }
    // $0AB96: MOVE.L -16380(A6),D6
    case 0xab96: {
        read16(pc+4);
        const auto value=read32(r.a[6]+(-16380));
        r.d[6]=value;
        sr=(sr&0xfff0)|(r.d[6]?0:4)|(r.d[6]&0x80000000?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AB9A: ADD.L D6,D0
    case 0xab9a: {
        const auto old=r.d[0],source=r.d[6];
        const bool subtract=false;
        const auto result=subtract?old-source:old+source;
        const bool carry=subtract?old<source:uint64_t(source)+old>0xffffffff;
        const bool overflow=((subtract?(old^source):~(old^source))&(old^result)&0x80000000)!=0;
        read16(pc+4);
        r.d[0]=result;
        sr=(sr&0xffe0)|(carry?0x11:0)|(overflow?2:0)|(result?0:4)|(result&0x80000000?8:0);
        idle(2);
        latch();
        idle(2);
        next=pc+2;
        break;
    }
    // $0AB9C: ASR.L #7,D0
    case 0xab9c: {
        asr_long(0,7);
        break;
    }
    // $0AB9E: ASL.L #5,D0
    case 0xab9e: {
        asl_long(0,5);
        break;
    }
    // $0ABA0: MOVE.W D7,D2
    case 0xaba0: {
        word(r.d[2],r.d[7]);
        nz(r.d[2]);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0ABA2: ADDI.W #$20,D2
    case 0xaba2: {
        read16(pc+4);
        word(r.d[2],addsub_word(r.d[2],0x20,false,true));
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABA6: MOVE.W D2,4(A6)
    case 0xaba6: {
        read16(pc+4);
        nz(uint16_t(r.d[2]));
        write16(r.a[6]+(4),r.d[2]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABAA: MOVE.L D0,16(A6)
    case 0xabaa: {
        read16(pc+4);
        sr=(sr&0xfff0)|(r.d[0]?0:4)|(r.d[0]&0x80000000?8:0);
        write16(r.a[6]+(16),r.d[0]>>16);
        write16((r.a[6]+(16))+2,r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABAE: MOVE.W 8(A6),D0
    case 0xabae: {
        read16(pc+4);
        const auto value=read16(r.a[6]+(8));
        word(r.d[0],value);
        nz(r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABB2: ADD.W ($0029C08A).L,D0
    case 0xabb2: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c08a);
        word(r.d[0],addsub_word(r.d[0],value,false,true));
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0ABB8: MOVE.W D0,4(A5)
    case 0xabb8: {
        read16(pc+4);
        nz(uint16_t(r.d[0]));
        write16(r.a[5]+(4),r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABBC: CLR.W D1
    case 0xabbc: {
        word(r.d[1],0);
        nz(0);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0ABBE: MOVE.B 1(A5),D1
    case 0xabbe: {
        read16(pc+4);
        const auto value=read8(r.a[5]+(1));
        byte(r.d[1],value);
        sr=(sr&0xfff0)|(uint8_t(r.d[1])?0:4)|(uint8_t(r.d[1])&0x80?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABC2: ASR.W #2,D1
    case 0xabc2: {
        asr_word(1,2);
        break;
    }
    // $0ABC4: ADDI.W #$A0,D1
    case 0xabc4: {
        read16(pc+4);
        word(r.d[1],addsub_word(r.d[1],0xa0,false,true));
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABC8: CMP.W D1,D0
    case 0xabc8: {
        addsub_word(r.d[0],r.d[1],true,false);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0ABCA: BGT $00AE32
    case 0xabca: {
        branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x00ae32,4);
        break;
    }
    // $0ABCE: NEG.W D1
    case 0xabce: {
        word(r.d[1],addsub_word(0,r.d[1],true,true));
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0ABD0: CMP.W D1,D0
    case 0xabd0: {
        addsub_word(r.d[0],r.d[1],true,false);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0ABD2: BLE $00AE32
    case 0xabd2: {
        branch((sr&4)||bool(sr&8)!=bool(sr&2),0x00ae32,4);
        break;
    }
    // $0ABD6: MOVE.W ($0029C090).L,-16384(A6)
    case 0xabd6: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c090);
        read16(pc+8);
        nz(uint16_t(value));
        write16(r.a[6]+(-16384),value);
        final(pc+10);
        next=pc+8;
        break;
    }
    // $0ABDE: MOVE.W 42(A5),-16382(A6)
    case 0xabde: {
        read16(pc+4);
        const auto value=read16(r.a[5]+(42));
        read16(pc+6);
        nz(uint16_t(value));
        write16(r.a[6]+(-16382),value);
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0ABE4: MOVE.L -16380(A6),D0
    case 0xabe4: {
        read16(pc+4);
        const auto value=read32(r.a[6]+(-16380));
        r.d[0]=value;
        sr=(sr&0xfff0)|(r.d[0]?0:4)|(r.d[0]&0x80000000?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABE8: MOVE.W 44(A5),D6
    case 0xabe8: {
        read16(pc+4);
        const auto value=read16(r.a[5]+(44));
        word(r.d[6],value);
        nz(r.d[6]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABEC: SUB.W ($0029C080).L,D6
    case 0xabec: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c080);
        word(r.d[6],addsub_word(r.d[6],value,true,true));
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0ABF2: MOVE.W D6,-16384(A6)
    case 0xabf2: {
        read16(pc+4);
        nz(uint16_t(r.d[6]));
        write16(r.a[6]+(-16384),r.d[6]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0ABF6: MOVE.W ($0029C092).L,-16382(A6)
    case 0xabf6: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c092);
        read16(pc+8);
        nz(uint16_t(value));
        write16(r.a[6]+(-16382),value);
        final(pc+10);
        next=pc+8;
        break;
    }
    // $0ABFE: MOVE.L -16380(A6),D6
    case 0xabfe: {
        read16(pc+4);
        const auto value=read32(r.a[6]+(-16380));
        r.d[6]=value;
        sr=(sr&0xfff0)|(r.d[6]?0:4)|(r.d[6]&0x80000000?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC02: SUB.L D6,D0
    case 0xac02: {
        const auto old=r.d[0],source=r.d[6];
        const bool subtract=true;
        const auto result=subtract?old-source:old+source;
        const bool carry=subtract?old<source:uint64_t(source)+old>0xffffffff;
        const bool overflow=((subtract?(old^source):~(old^source))&(old^result)&0x80000000)!=0;
        read16(pc+4);
        r.d[0]=result;
        sr=(sr&0xffe0)|(carry?0x11:0)|(overflow?2:0)|(result?0:4)|(result&0x80000000?8:0);
        idle(2);
        latch();
        idle(2);
        next=pc+2;
        break;
    }
    // $0AC04: ASR.L #7,D0
    case 0xac04: {
        asr_long(0,7);
        break;
    }
    // $0AC06: ASL.L #5,D0
    case 0xac06: {
        asl_long(0,5);
        break;
    }
    // $0AC08: MOVE.L D0,16(A6)
    case 0xac08: {
        read16(pc+4);
        sr=(sr&0xfff0)|(r.d[0]?0:4)|(r.d[0]&0x80000000?8:0);
        write16(r.a[6]+(16),r.d[0]>>16);
        write16((r.a[6]+(16))+2,r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC0C: MOVE.W 8(A6),D0
    case 0xac0c: {
        read16(pc+4);
        const auto value=read16(r.a[6]+(8));
        word(r.d[0],value);
        nz(r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC10: ADDI.W #$70,D0
    case 0xac10: {
        read16(pc+4);
        word(r.d[0],addsub_word(r.d[0],0x70,false,true));
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC14: SUB.W ($0029C088).L,D0
    case 0xac14: {
        read16(pc+4);
        read16(pc+6);
        const auto value=read16(0x29c088);
        word(r.d[0],addsub_word(r.d[0],value,true,true));
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0AC1A: MOVE.W D0,6(A5)
    case 0xac1a: {
        read16(pc+4);
        nz(uint16_t(r.d[0]));
        write16(r.a[5]+(6),r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC1E: CLR.W D1
    case 0xac1e: {
        word(r.d[1],0);
        nz(0);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AC20: MOVE.B 1(A5),D1
    case 0xac20: {
        read16(pc+4);
        const auto value=read8(r.a[5]+(1));
        byte(r.d[1],value);
        sr=(sr&0xfff0)|(uint8_t(r.d[1])?0:4)|(uint8_t(r.d[1])&0x80?8:0);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC24: ASR.W #1,D1
    case 0xac24: {
        asr_word(1,1);
        break;
    }
    // $0AC26: NEG.W D1
    case 0xac26: {
        word(r.d[1],addsub_word(0,r.d[1],true,true));
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AC28: CMP.W D1,D0
    case 0xac28: {
        addsub_word(r.d[0],r.d[1],true,false);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AC2A: BLE $00AC4A
    case 0xac2a: {
        branch((sr&4)||bool(sr&8)!=bool(sr&2),0x00ac4a,4);
        break;
    }
    // $0AC2E: NEG.W D1
    case 0xac2e: {
        word(r.d[1],addsub_word(0,r.d[1],true,true));
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AC30: ADDI.W #$E0,D1
    case 0xac30: {
        read16(pc+4);
        word(r.d[1],addsub_word(r.d[1],0xe0,false,true));
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC34: CMP.W D1,D0
    case 0xac34: {
        addsub_word(r.d[0],r.d[1],true,false);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AC36: BGT $00AE32
    case 0xac36: {
        branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x00ae32,4);
        break;
    }
    // $0AC3A: MOVE.W 50(A5),D0
    case 0xac3a: {
        read16(pc+4);
        const auto value=read16(r.a[5]+(50));
        word(r.d[0],value);
        nz(r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC3E: ADDI.W #$E8,D0
    case 0xac3e: {
        read16(pc+4);
        word(r.d[0],addsub_word(r.d[0],0xe8,false,true));
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC42: MOVE.B D0,3(A5)
    case 0xac42: {
        read16(pc+4);
        sr=(sr&0xfff0)|(uint8_t(r.d[0])?0:4)|(uint8_t(r.d[0])&0x80?8:0);
        write8(r.a[5]+(3),r.d[0]);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AC46: BSR $00B628
    case 0xac46: {
        idle(2);
        sp-=4;
        write16(sp,0);
        write16(sp+2,0xac4a);
        read16(0xb628);
        final(0xb62a);
        next=0xb628;
        break;
    }
    // $0AC4A: RTS
    case 0xac4a: {
        next=read32(sp);
        sp+=4;
        read16(next);
        final(next+2);
        break;
    }
    // $0AE32: CLR.W 36(A5)
    case 0xae32: {
        read16(pc+4);
        read16(r.a[5]+(36));
        nz(0);
        read16(pc+6);
        latch();
        write16(r.a[5]+(36),0);
        next=pc+4;
        break;
    }
    // $0AE36: CLR.B 1(A5)
    case 0xae36: {
        read16(pc+4);
        read8(r.a[5]+(1));
        nz(0);
        read16(pc+6);
        latch();
        write8(r.a[5]+(1),0);
        next=pc+4;
        break;
    }
    // $0AE3A: BCLR #7,(A5)
    case 0xae3a: {
        read16(pc+4);
        const auto value=read8(r.a[5]);
        sr=(sr&~4)|(value&128?0:4);
        read16(pc+6);
        latch();
        write8(r.a[5],value&~128);
        next=pc+4;
        break;
    }
    // $0AE3E: LEA ($8E7F).W,A0
    case 0xae3e: {
        r.a[0]=0xffff8e7f;
        read16(pc+4);
        final(pc+6);
        next=pc+4;
        break;
    }
    // $0AE42: ADDQ.B #1,(A0)
    case 0xae42: {
        const auto old=read8(r.a[0]),value=uint8_t(old+1);
        sr=(sr&0xffe0)|(old==0xff?0x11:0)|(old==0x7f?2:0)|(value?0:4)|(value&0x80?8:0);
        read16(pc+4);
        latch();
        write8(r.a[0],value);
        next=pc+2;
        break;
    }
    // $0AE44: CLR.W D0
    case 0xae44: {
        word(r.d[0],0);
        nz(0);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AE46: MOVE.B (A0),D0
    case 0xae46: {
        const auto value=read8(r.a[0]);
        byte(r.d[0],value);
        sr=(sr&0xfff0)|(uint8_t(r.d[0])?0:4)|(uint8_t(r.d[0])&0x80?8:0);
        final(pc+4);
        next=pc+2;
        break;
    }
    // $0AE48: MOVE.B 20(A5),0(A0,D0.W)
    case 0xae48: {
        read16(pc+4);
        const auto value=read8(r.a[5]+20);
        idle(2);
        read16(pc+6);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        write8(r.a[0]+int16_t(r.d[0]),value);
        final(pc+8);
        next=pc+6;
        break;
    }
    // $0AE4E: RTS
    case 0xae4e: {
        next=read32(sp);
        sp+=4;
        read16(next);
        final(next+2);
        break;
    }
    // $B698..$B7C0: sprite-RAM full/strided clear and publication waits.
    // Values and loop limits are literal scalar ROM operands; no ROM tables.
    case 0xb698:case 0xb6d6:case 0xb704:case 0xb714:case 0xb76a:case 0xb7b2:
        read16(pc+4);read16(0x14856);sp-=4;write16(sp,0);write16(sp+2,pc+6);
        final(0x14858);next=0x14856;break;
    case 0xb69e:case 0xb6dc:case 0xb70a:case 0xb71a:case 0xb770:case 0xb7b8:
        read16(pc+4);read16(pc+6);nz(read16(0x2a3c00));final(pc+8);next=pc+6;break;
    case 0xb6a4:case 0xb6e2:case 0xb710:case 0xb720:case 0xb776:case 0xb7be:
        branch(!(sr&4),pc-6,2);break;
    case 0xb6a6:case 0xb722:
        r.d[1]=0xc0000000;sr=(sr&0xfff0)|8;read16(pc+4);read16(pc+6);final(pc+8);next=pc+6;break;
    case 0xb6ac:r.d[2]=0;nz(0);final(pc+4);next=pc+2;break;
    case 0xb6ae:case 0xb6e4:case 0xb728:case 0xb778:
        r.a[0]=0x100000;read16(pc+4);read16(pc+6);final(pc+8);next=pc+6;break;
    case 0xb6b4:case 0xb6ea:case 0xb72e:case 0xb77e:
        word(r.d[0],pc==0xb6b4||pc==0xb6ea?0xff:0x1f);nz(r.d[0]);
        read16(pc+4);final(pc+6);next=pc+4;break;
    case 0xb6b8:case 0xb6ba:case 0xb6bc:case 0xb6be:
    case 0xb6ee:case 0xb6f0:case 0xb6f2:case 0xb6f4: {
        const auto value=r.d[pc==0xb6b8||pc==0xb6ee?1:2];
        write16(r.a[0],value>>16);write16(r.a[0]+2,value);r.a[0]+=4;
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);final(pc+4);next=pc+2;break;
    }
    case 0xb732:case 0xb734:case 0xb738:case 0xb73c:case 0xb740:case 0xb744:case 0xb748:case 0xb74c:
    case 0xb782:case 0xb784:case 0xb788:case 0xb78c:case 0xb790:case 0xb794:case 0xb798:case 0xb79c: {
        const auto base=pc<0xb782?0xb732u:0xb782u;
        const unsigned displacement=pc==base?0:((pc-base+2)/4)*16;
        if(displacement)read16(pc+4);
        write16(r.a[0]+displacement,r.d[1]>>16);write16(r.a[0]+displacement+2,r.d[1]);
        sr=(sr&0xfff0)|(r.d[1]?0:4)|(r.d[1]&0x80000000?8:0);
        final(pc+(displacement?6:4));next=pc+(displacement?4:2);break;
    }
    case 0xb750:case 0xb7a0:r.a[0]+=0x80;read16(pc+4);final(pc+6);next=pc+4;break;
    case 0xb6c0:case 0xb6f6:case 0xb754:case 0xb7a4: {
        const auto target=pc==0xb6c0?0xb6b8u:pc==0xb6f6?0xb6eeu:pc==0xb754?0xb732u:0xb782u;
        const auto old=uint16_t(r.d[0]);idle(2);read16(target);word(r.d[0],old-1);
        if(old){final(target+2);next=target;}
        else{read16(pc+4);final(pc+6);next=pc+4;}break;
    }
    case 0xb6c4:case 0xb6fa:case 0xb758:case 0xb7a8: {
        const uint32_t value=pc==0xb6c4||pc==0xb758?0x55555555u:0xaaaaaaaau;
        read16(pc+4);read16(pc+6);read16(pc+8);read16(pc+10);
        sr=(sr&0xfff0)|(value&0x80000000?8:0);
        write16(0x1007fc,value>>16);write16(0x1007fe,value);final(pc+12);next=pc+10;break;
    }
    case 0xb6ce:case 0xb762:
        read16(pc+4);read16(pc+6);read16(pc+8);nz(0xffff);
        write16(0x110000,0xffff);final(pc+10);next=pc+8;break;
    case 0xb712:case 0xb7c0:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $B66E installs the publication handler and falls into its checked wait.
    case 0xb66e:
        read16(pc+4);read16(pc+6);read16(pc+8);
        sr=(sr&0xfff0);write16(r.a[5]+16,0);write16(r.a[5]+18,0xb676);
        final(pc+10);next=pc+8;break;
    // $C082..$C098: copy previous position, test shared ready bit, append/return.
    // $C082 shares the literal displacement MOVE above.
    case 0xc088: {
        read16(pc+4);read16(pc+6);read16(pc+8);
        const auto value=read8(0x29c200);sr=(sr&~4)|(value&1?0:4);
        final(pc+10);next=pc+8;break;
    }
    case 0xc090:branch(sr&4,0xc098,4);break;
    case 0xc094:
        idle(2);sp-=4;write16(sp,0);write16(sp+2,0xc098);
        read16(0xb628);final(0xb62a);next=0xb628;break;
    case 0xc098:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $88CA..$8944: world initializer continuation; actual multiplier/divider buses.
    case 0x88ca:
        read16(pc+4);read8(r.a[5]+1);nz(0);read16(pc+6);
        latch();write8(r.a[5]+1,0);next=pc+4;break;
    case 0x88ce: {
        read16(pc+4);read16(pc+6);const auto value=read16(0x29c0c2);
        read16(pc+8);nz(value);write16(r.a[5]+36,value);final(pc+10);next=pc+8;break;
    }
    case 0x88d6:read16(pc+4);r.a[0]=read32(r.a[5]+54);final(pc+6);next=pc+4;break;
    case 0x88da:case 0x88dc: {
        const auto value=read32(r.a[0]);if(pc==0x88da)r.a[0]+=4;
        r.d[pc==0x88da?4:5]=value;sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        final(pc+4);next=pc+2;break;
    }
    case 0x88de:case 0x8916: {
        read16(pc+4);read16(pc+6);const auto value=read16(pc==0x88de?0x29c092:0x29c090);
        read16(pc+8);nz(value);write16(r.a[6]-0x4000,value);final(pc+10);next=pc+8;break;
    }
    case 0x88e6:case 0x891e: {
        read16(pc+4);const auto value=read16(r.a[5]+42);read16(pc+6);nz(value);
        write16(r.a[6]-0x3ffe,value);final(pc+8);next=pc+6;break;
    }
    case 0x88ec:case 0x8924: {
        read16(pc+4);r.d[0]=read32(r.a[6]-0x3ffc);
        sr=(sr&0xfff0)|(r.d[0]?0:4)|(r.d[0]&0x80000000?8:0);final(pc+6);next=pc+4;break;
    }
    case 0x88f0:case 0x8928: {
        const auto old=r.d[0],source=r.d[pc==0x88f0?4:5];const bool subtract=pc==0x8928;
        const auto value=subtract?old-source:old+source;
        const bool carry=subtract?old<source:uint64_t(source)+old>0xffffffff;
        const bool overflow=((subtract?(old^source):~(old^source))&(old^value)&0x80000000)!=0;
        read16(pc+4);r.d[0]=value;
        sr=(sr&0xffe0)|(carry?0x11:0)|(overflow?2:0)|(value?0:4)|(value&0x80000000?8:0);
        idle(2);latch();idle(2);next=pc+2;break;
    }
    case 0x88f2:case 0x892a:asr_long(0,7);break;
    case 0x88f4:case 0x892c:asl_long(0,5);break;
    case 0x88f6:read16(pc+4);word(r.d[2],read16(r.a[5]+36));nz(r.d[2]);final(pc+6);next=pc+4;break;
    case 0x88fa: {
        auto value=uint16_t(r.d[2]);read16(pc+4);bool carry=false;
        for(unsigned i=0;i<5;++i){carry=value&1;value=(value>>1)|(value&0x8000);idle(2);}
        word(r.d[2],value);sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0);
        latch();idle(2);next=pc+2;break;
    }
    case 0x88fc:case 0x8936: {
        read16(pc+4);const unsigned reg=pc==0x88fc?2:0;
        word(r.d[reg],addsub_word(r.d[reg],pc==0x88fc?0x20:0x70,false,true));
        final(pc+6);next=pc+4;break;
    }
    case 0x8900:case 0x892e: {
        read16(pc+4);const auto value=r.d[0];sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        const auto address=r.a[6]+(pc==0x8900?0:16);
        write16(address,value>>16);write16(address+2,value);final(pc+6);next=pc+4;break;
    }
    case 0x8904:read16(pc+4);nz(r.d[2]);write16(r.a[6]+20,r.d[2]);final(pc+6);next=pc+4;break;
    case 0x8908:case 0x8932:
        read16(pc+4);word(r.d[0],read16(r.a[6]+8));nz(r.d[0]);final(pc+6);next=pc+4;break;
    case 0x890c:case 0x893a: {
        read16(pc+4);read16(pc+6);const auto source=read16(pc==0x890c?0x29c08e:0x29c08c);
        word(r.d[0],addsub_word(r.d[0],source,pc==0x893a,true));final(pc+8);next=pc+6;break;
    }
    case 0x8912:case 0x8940:
        read16(pc+4);nz(r.d[0]);write16(r.a[5]+(pc==0x8912?4:6),r.d[0]);final(pc+6);next=pc+4;break;
    case 0x8944:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $8AF8 MOVE.W abs.L,D1: extension/next-opcode fetches, data,
    // final prefetch. Preserve the high D1 word and X; replace NZVC.
    case 0x8af8: {
        read16(pc+4);read16(pc+6);const auto value=read16(0x29c0aa);
        word(r.d[1],value);nz(value);final(pc+8);next=pc+6;break;
    }
    // $B62E CMPI.W #$C0,D0: two prefetches, eight clocks; D0 and X
    // are unchanged. SUB flags include unsigned borrow and signed overflow.
    case 0xb62e: {
        read16(pc+4);const auto old=uint16_t(r.d[0]);
        constexpr uint16_t limit=0xc0;
        const auto value=uint16_t(old-limit);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x8000?8:0)|
           (((old^limit)&(old^value)&0x8000)?2:0)|(old<limit?1:0);
        final(pc+6);next=pc+4;break;
    }
    // $8AFE SUB.W D1,36(A5): extension, operand, final prefetch,
    // write. The IRQ selection is before the write; all XNZVC are replaced.
    case 0x8afe: {
        read16(pc+4);const auto old=read16(r.a[5]+36);
        const auto source=uint16_t(r.d[1]),value=uint16_t(old-source);
        const bool borrow=old<source;
        sr=(sr&0xffe0)|(borrow?0x11:0)|(value?0:4)|(value&0x8000?8:0)|
           (((old^source)&(old^value)&0x8000)?2:0);
        read16(pc+6);latch();write16(r.a[5]+36,value);next=pc+4;break;
    }
    // $B632 BGE.W $B654: signed N==V, flags/registers unchanged.
    // Ten clocks taken, twelve untaken, each with two prefetch phases.
    case 0xb632:branch(bool(sr&8)==bool(sr&2),0xb654,4);break;
    // $8B02 MOVE.W 36(A5),D1.
    case 0x8b02: {
        read16(pc+4);const auto value=read16(r.a[5]+36);
        word(r.d[1],value);nz(value);final(pc+6);next=pc+4;break;
    }
    // $B636 ASL.W #4,D0. MAME srrw2 prefetch precedes the
    // four two-clock shifts and nbcr3's final two internal clocks.
    case 0xb636: {
        auto value=uint16_t(r.d[0]);nz(value);read16(pc+4);
        bool carry=false,overflow=false;
        for(unsigned i=0;i<4;++i) {
            carry=value&0x8000;const auto shifted=uint16_t(value<<1);
            overflow |= bool((value^shifted)&0x8000);value=shifted;idle(2);
        }
        word(r.d[0],value);
        sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0)|(overflow?2:0);
        latch();idle(2);next=pc+2;break;
    }
    // $8B06 ASR.W #5,D1; srrw2 prefetch, five shifts, nbcr3.
    case 0x8b06: {
        auto value=uint16_t(r.d[1]);nz(value);read16(pc+4);bool carry=false;
        for(unsigned i=0;i<5;++i) {carry=value&1;value=(value>>1)|(value&0x8000);idle(2);}
        word(r.d[1],value);sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0);
        latch();idle(2);next=pc+2;break;
    }
    // $8B08 BLT.W $9556; signed N!=V.
    case 0x8b08:branch(bool(sr&8)!=bool(sr&2),0x9556,4);break;
    // $8B0C ADDI.W #$20,D1, two prefetches, XNZVC addition.
    case 0x8b0c: {
        read16(pc+4);const auto old=uint16_t(r.d[1]),value=uint16_t(old+0x20);
        const bool carry=unsigned(old)+0x20>0xffff;
        word(r.d[1],value);sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0)|
            ((~(old^0x20)&(old^value)&0x8000)?2:0);
        final(pc+6);next=pc+4;break;
    }
    // $8B10 perspective numerator, $8B18 divisor, $8B1C quotient.
    case 0x8b10:
        read16(pc+4);read16(pc+6);read16(pc+8);
        write16(r.a[6],0);write16(r.a[6]+2,0x7fe0);nz(0x7fe0);
        final(pc+10);next=pc+8;break;
    case 0x8b18:read16(pc+4);nz(r.d[1]);write16(r.a[6]+20,r.d[1]);final(pc+6);next=pc+4;break;
    case 0x8b1c: {
        read16(pc+4);const auto value=read16(r.a[6]+8);word(r.d[0],value);nz(value);final(pc+6);next=pc+4;break;
    }
    // $8B20/$8B22 MOVE.W D0,D7/D1, one prefetch, upper word retained.
    case 0x8b20:case 0x8b22:word(r.d[pc==0x8b20?7:1],r.d[0]);nz(r.d[0]);final(pc+4);next=pc+2;break;
    // $8B24/#3 and $8B2E/D0 arithmetic right word shifts.
    case 0x8b24:case 0x8b2e: {
        const unsigned reg=pc==0x8b24?0:1,count=pc==0x8b24?3:r.d[0]&63;
        auto value=uint16_t(r.d[reg]);nz(value);read16(pc+4);bool carry=false;
        for(unsigned i=0;i<count;++i){carry=value&1;value=(value>>1)|(value&0x8000);idle(2);}
        word(r.d[reg],value);sr=(sr&0xfff0)|(carry?1:0)|(value?0:4)|(value&0x8000?8:0);
        if(count)sr=(sr&~16)|(carry?16:0);
        latch();idle(2);next=pc+2;break;
    }
    // $8B26 MOVE.B D0,2(A5); $8B2A MOVE.B 21(A5),D0.
    case 0x8b26: {
        read16(pc+4);const auto value=uint8_t(r.d[0]);sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        write8(r.a[5]+2,value);final(pc+6);next=pc+4;break;
    }
    case 0x8b2a: {
        read16(pc+4);const auto value=read8(r.a[5]+21);byte(r.d[0],value);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);final(pc+6);next=pc+4;break;
    }
    // $8B30/$8B34 pack the object's scale word, with RMW bus order.
    case 0x8b30: {
        read16(pc+4);const auto value=uint16_t(read16(r.a[5])&0xfc00);nz(value);
        read16(pc+6);latch();write16(r.a[5],value);next=pc+4;break;
    }
    case 0x8b34: {
        const auto value=uint16_t(read16(r.a[5])|r.d[1]);nz(value);
        read16(pc+4);latch();write16(r.a[5],value);next=pc+2;break;
    }
    // $8B36 runtime offset pointer; $8B3A/$8B3C the two offset longs.
    case 0x8b36:read16(pc+4);r.a[0]=read32(r.a[5]+54);final(pc+6);next=pc+4;break;
    case 0x8b3a:case 0x8b3c: {
        const auto value=read32(r.a[0]);if(pc==0x8b3a)r.a[0]+=4;
        r.d[pc==0x8b3a?4:5]=value;sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        final(pc+4);next=pc+2;break;
    }
    // $8B3E MOVE.W D7,D6; $8B40 ASR.W #4,D6.
    case 0x8b3e:word(r.d[6],r.d[7]);nz(r.d[7]);final(pc+4);next=pc+2;break;
    case 0x8b40: {
        auto value=uint16_t(r.d[6]);nz(value);read16(pc+4);bool carry=false;
        for(unsigned i=0;i<4;++i){carry=value&1;value=(value>>1)|(value&0x8000);idle(2);}
        word(r.d[6],value);sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0);
        latch();idle(2);next=pc+2;break;
    }
    // $8B42 ROM sway-table address; $8B48 indexed byte, $8B4C EXT.W.
    case 0x8b42:r.a[0]=0x33ed8;read16(pc+4);read16(pc+6);final(pc+8);next=pc+6;break;
    case 0x8b48: {
        idle(2);read16(pc+4);const auto value=read8(r.a[0]+int16_t(r.d[6]));byte(r.d[6],value);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);final(pc+6);next=pc+4;break;
    }
    case 0x8b4c:word(r.d[6],int8_t(r.d[6]));nz(r.d[6]);final(pc+4);next=pc+2;break;
    // $8B4E/$8B52 program the multiplier; $8B5A load its product.
    case 0x8b4e:read16(pc+4);nz(r.d[6]);write16(r.a[6]-0x4000,r.d[6]);final(pc+6);next=pc+4;break;
    case 0x8b52: {
        read16(pc+4);read16(pc+6);const auto value=read16(0x29c0c6);
        read16(pc+8);nz(value);write16(r.a[6]-0x3ffe,value);final(pc+10);next=pc+8;break;
    }
    case 0x8b5a: {
        read16(pc+4);const auto value=read32(r.a[6]-0x3ffc);r.d[6]=value;
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);final(pc+6);next=pc+4;break;
    }
    // $8B5E ASR.L #6,D6: two extra long ALU clocks before nbcr3.
    case 0x8b5e: {
        auto value=r.d[6];sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        read16(pc+4);bool carry=false;
        for(unsigned i=0;i<6;++i){carry=value&1;value=(value>>1)|(value&0x80000000);idle(2);}
        r.d[6]=value;sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x80000000?8:0);
        idle(2);latch();idle(2);next=pc+2;break;
    }
    // $8B60 ADD.W D6,42(A5), extension/read/prefetch/write.
    case 0x8b60: {
        read16(pc+4);const auto old=read16(r.a[5]+42),source=uint16_t(r.d[6]),value=uint16_t(old+source);
        const bool carry=unsigned(old)+source>0xffff;
        sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0)|
            ((~(old^source)&(old^value)&0x8000)?2:0);
        read16(pc+6);latch();write16(r.a[5]+42,value);next=pc+4;break;
    }
    // $8B64/$8BBC horizontal/vertical multiplier input.
    case 0x8b64:case 0x8bbc: {
        read16(pc+4);read16(pc+6);const auto value=read16(pc==0x8b64?0x29c092:0x29c090);
        read16(pc+8);nz(value);write16(r.a[6]-0x4000,value);final(pc+10);next=pc+8;break;
    }
    // $8B6C/$8BC4 second multiplier word; $8B72/$8B82/$8BCA/$8BDA products.
    case 0x8b6c:case 0x8bc4: {
        read16(pc+4);const auto value=read16(r.a[5]+42);read16(pc+6);nz(value);
        write16(r.a[6]-0x3ffe,value);final(pc+8);next=pc+6;break;
    }
    case 0x8b72:case 0x8b82:case 0x8bca:case 0x8bda: {
        read16(pc+4);r.d[0]=read32(r.a[6]-0x3ffc);
        sr=(sr&0xfff0)|(r.d[0]?0:4)|(r.d[0]&0x80000000?8:0);final(pc+6);next=pc+4;break;
    }
    // $8B76 ADD.L D4,D0 / $8BCE SUB.L D5,D0; roml3 internal completion.
    case 0x8b76:case 0x8bce: {
        const auto old=r.d[0],source=r.d[pc==0x8b76?4:5];const bool subtract=pc==0x8bce;
        const auto value=subtract?old-source:old+source;
        const bool carry=subtract?old<source:uint64_t(old)+source>0xffffffff;
        const bool overflow=((subtract?(old^source):~(old^source))&(old^value)&0x80000000)!=0;
        read16(pc+4);r.d[0]=value;
        sr=(sr&0xffe0)|(carry?0x11:0)|(overflow?2:0)|(value?0:4)|(value&0x80000000?8:0);
        idle(2);latch();idle(2);next=pc+2;break;
    }
    // $8B78/$8BD0 #7, $8B86/$8BDE #8, $8B88/$8BE0 #2 ASR.L D0.
    case 0x8b78:case 0x8bd0:asr_long(0,7);break;
    case 0x8b86:case 0x8bde:asr_long(0,8);break;
    case 0x8b88:case 0x8be0:asr_long(0,2);break;
    // $8B7A/$8BD2 and $8B7E/$8BD6 multiplier operands from D0/D7.
    case 0x8b7a:case 0x8bd2:case 0x8b7e:case 0x8bd6: {
        const bool second=pc==0x8b7e||pc==0x8bd6;const auto value=uint16_t(r.d[second?7:0]);
        read16(pc+4);nz(value);write16(r.a[6]-(second?0x3ffe:0x4000),value);final(pc+6);next=pc+4;break;
    }
    // $8B8A ADD.W shared X offset / $8BE6 SUB.W shared Y offset.
    case 0x8b8a:case 0x8be6: {
        read16(pc+4);read16(pc+6);const auto source=read16(pc==0x8b8a?0x29c08e:0x29c08c);
        word(r.d[0],addsub_word(r.d[0],source,pc==0x8be6,true));final(pc+8);next=pc+6;break;
    }
    // $8B90/$8BEC store projected X/Y; $8B94/$8BF0 runtime table addresses.
    case 0x8b90:case 0x8bec:read16(pc+4);nz(r.d[0]);write16(r.a[5]+(pc==0x8b90?4:6),r.d[0]);final(pc+6);next=pc+4;break;
    case 0x8b94:case 0x8bf0:r.a[pc==0x8b94?1:0]=pc==0x8b94?0x2a3800:0x29c0a2;read16(pc+4);read16(pc+6);final(pc+8);next=pc+6;break;
    // $8B9A/$8BF6 clear index words; $8B9C/$8BF8 load index bytes.
    case 0x8b9a:case 0x8bf6:word(r.d[pc==0x8b9a?1:0],0);nz(0);final(pc+4);next=pc+2;break;
    case 0x8b9c:case 0x8bf8: {
        const unsigned reg=pc==0x8b9c?1:0;read16(pc+4);const auto value=read8(r.a[5]+(reg?20:59));
        byte(r.d[reg],value);sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);final(pc+6);next=pc+4;break;
    }
    // $8BA0/$8BA2 ADD.W D1,D1; $8BA4/$8BFC indexed runtime words.
    case 0x8ba0:case 0x8ba2:word(r.d[1],addsub_word(r.d[1],r.d[1],false,true));final(pc+4);next=pc+2;break;
    case 0x8ba4:case 0x8bfc: {
        idle(2);read16(pc+4);const auto address=pc==0x8ba4?r.a[1]+int16_t(r.d[1]):r.a[0]+int16_t(r.d[0]);
        const auto value=read16(address);word(r.d[1],value);nz(value);final(pc+6);next=pc+4;break;
    }
    // $8BA8 ASR.W #1,D1; $8BAA/$8BE2 add viewport offsets.
    case 0x8ba8: {
        auto value=uint16_t(r.d[1]);nz(value);read16(pc+4);const bool carry=value&1;
        value=(value>>1)|(value&0x8000);idle(2);word(r.d[1],value);
        sr=(sr&0xffe0)|(carry?0x11:0)|(value?0:4)|(value&0x8000?8:0);latch();idle(2);next=pc+2;break;
    }
    case 0x8baa:case 0x8be2: {
        const unsigned reg=pc==0x8baa?1:0;read16(pc+4);
        word(r.d[reg],addsub_word(r.d[reg],reg?0xa0:0x70,false,true));final(pc+6);next=pc+4;break;
    }
    // $8BAE/$8BB6 CMP.W D1,D0; $8BB4 NEG.W D1; signed clip branches.
    case 0x8bae:case 0x8bb6:addsub_word(r.d[0],r.d[1],true,false);final(pc+4);next=pc+2;break;
    case 0x8bb4:word(r.d[1],addsub_word(0,r.d[1],true,true));final(pc+4);next=pc+2;break;
    case 0x8bb0:branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x9556,4);break;
    case 0x8bb8:branch((sr&4)||bool(sr&8)!=bool(sr&2),0x9556,4);break;
    // $8C00/$8C04 indexed animation payload from RAM-owned pointer tables.
    case 0x8c00:read16(pc+4);r.a[1]=read32(r.a[5]+60);final(pc+6);next=pc+4;break;
    case 0x8c04: {
        idle(2);read16(pc+4);const auto value=read32(r.a[1]+int16_t(r.d[1]));read16(pc+6);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        write16(r.a[5]+12,value>>16);write16(r.a[5]+14,value);final(pc+8);next=pc+6;break;
    }
    // $8C0A/$8C0E/$8C14 shared flip flags and $8C16 BSR publication.
    case 0x8c0a: {
        read16(pc+4);const auto value=uint8_t(read8(r.a[5])&0xcf);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);read16(pc+6);latch();write8(r.a[5],value);next=pc+4;break;
    }
    case 0x8c0e: {
        read16(pc+4);read16(pc+6);const auto value=read8(0x29c0a0);byte(r.d[0],value);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);final(pc+8);next=pc+6;break;
    }
    case 0x8c14: {
        const auto value=uint8_t(read8(r.a[5])|r.d[0]);sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        read16(pc+4);latch();write8(r.a[5],value);next=pc+2;break;
    }
    case 0x8c16:idle(2);sp-=4;write16(sp,0);write16(sp+2,0x8c1a);read16(0xb628);final(0xb62a);next=0xb628;break;
    // $9556..$9572 release a clipped object and return its size slot.
    case 0x9556:read16(pc+4);read16(r.a[5]+36);nz(0);read16(pc+6);latch();write16(r.a[5]+36,0);next=pc+4;break;
    case 0x955a: {
        read16(pc+4);const auto old=read8(r.a[5]);sr=(sr&~4)|(old&0x40?0:4);
        read16(pc+6);latch();write8(r.a[5],old&~0x40);next=pc+4;break;
    }
    case 0x955e: {
        read16(pc+4);const auto value=uint16_t(read16(r.a[5])&0x7c00);nz(value);
        read16(pc+6);latch();write16(r.a[5],value);next=pc+4;break;
    }
    case 0x9562:r.a[0]=0xffff8d80;read16(pc+4);final(pc+6);next=pc+4;break;
    case 0x9566: {
        const auto old=read8(r.a[0]),value=uint8_t(old+1);sr=(sr&0xffe0)|(old==0xff?0x11:0)|(old==0x7f?2:0)|(value?0:4)|(value&0x80?8:0);
        read16(pc+4);latch();write8(r.a[0],value);next=pc+2;break;
    }
    case 0x9568:word(r.d[0],0);nz(0);final(pc+4);next=pc+2;break;
    case 0x956a: {
        const auto value=read8(r.a[0]);byte(r.d[0],value);sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        final(pc+4);next=pc+2;break;
    }
    case 0x956c: {
        read16(pc+4);const auto value=read8(r.a[5]+20);idle(2);read16(pc+6);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);write8(r.a[0]+int16_t(r.d[0]),value);final(pc+8);next=pc+6;break;
    }
    case 0x9572:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $8C1A return from the world handler after publication.
    case 0x8c1a:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $B638 MOVEA.L abs.L,A0; $B63E ADDA.W D0,A0; $B640 LEA (A5),A1.
    case 0xb638:read16(pc+4);read16(pc+6);r.a[0]=read32(0x2a3c04);final(pc+8);next=pc+6;break;
    case 0xb63e:r.a[0]+=int16_t(r.d[0]);read16(pc+4);idle(2);latch();idle(2);next=pc+2;break;
    case 0xb640:r.a[1]=r.a[5];final(pc+4);next=pc+2;break;
    // $B642/$B644/$B646/$B648 copy the four publication longs.
    case 0xb642:case 0xb644:case 0xb646:case 0xb648: {
        const auto value=read32(r.a[1]);r.a[1]+=4;
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80000000?8:0);
        write16(r.a[0],value>>16);write16(r.a[0]+2,value);
        if(pc!=0xb648)r.a[0]+=4;
        final(pc+4);next=pc+2;break;
    }
    // $B64A MOVE.B 20(A5),(A0).
    case 0xb64a: {
        read16(pc+4);const auto value=read8(r.a[5]+20);
        sr=(sr&0xfff0)|(value?0:4)|(value&0x80?8:0);
        write8(r.a[0],value);final(pc+6);next=pc+4;break;
    }
    // $B64E ADDQ.W #1,abs.L, prefetch selects before final write.
    case 0xb64e: {
        read16(pc+4);read16(pc+6);const auto old=read16(0x2a3cf0),value=uint16_t(old+1);
        arithmetic(old,value,false,true);read16(pc+8);latch();write16(0x2a3cf0,value);next=pc+6;break;
    }
    // $B654 producer return, stack and two target prefetch phases.
    case 0xb654:next=read32(sp);sp+=4;read16(next);final(next+2);break;
    // $B628 publication producer count load.
    case 0xb628: {
        read16(pc+4);read16(pc+6);const auto value=read16(0x2a3cf0);
        word(r.d[0],value);nz(value);final(pc+8);next=pc+6;break;
    }
    // $15BFE BSR.W into the verified enemy movement graph.
    case 0x15bfe:
        idle(2);sp-=4;write16(sp,1);write16(sp+2,0x5c02);
        read16(0x15750);final(0x15752);next=0x15750;break;
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
    default:throw std::runtime_error("unported caller instruction at PC "+std::to_string(pc));
    }
    clock(ns);return {{r,next,sp,sr,ns},selected,selection};
}

// $1466A/$14672/$147C6/$147E6: MAME rte_df (rtr1..rtr4), three stack
// reads followed by two return prefetches; restore SR before selecting IPL.
OMainIrqService::Instruction OMainIrqService::return_joint(MainBus& b,const Result& s,uint32_t usp,const Clock& advance,const Poll& poll) {
    switch(s.pc){case 0x1466a:case 0x14672:case 0x147c6:case 0x147e6:case 0x14842:break;
        default:throw std::runtime_error("unported joint main RTE instruction");}
    if(!(s.sr&0x2000)||s.ns%CYCLE_NS||(s.sp&1)||!poll)
        throw std::runtime_error("unsupported joint main RTE context");
    bus=&b;clock=advance;ns=s.ns;pc=s.pc;
    const auto sr=uint16_t(read16(s.sp)&0xa71f);const auto target=read32(s.sp+2);
    const auto sp=sr&0x2000?s.sp+6:usp;
    read16(target);const auto selection=ns;clock(ns);const auto level=poll(ns);
    if(level!=0&&level!=2&&level!=4&&level!=6)throw std::runtime_error("unsupported joint main RTE IPL");
    read16(target+2);clock(ns);
    return {{s.registers,target,sp,sr,ns},eligible(level,sr)?level:0,selection};
}

// $138BA TRAP #12: trap1..trap9 in m68000-sdf.cpp. Stack bank switch,
// low PC/SR/high PC writes, vector $B0 and both handler prefetches.
OMainIrqService::Instruction OMainIrqService::trap_joint(MainBus& b,const Result& s,uint32_t ssp,const Clock& advance,const Poll& poll) {
    if(s.pc!=0x138ba||!poll)throw std::runtime_error("unsupported joint main TRAP");
    bus=&b;clock=advance;ns=s.ns;pc=s.pc;
    const auto sp=(s.sr&0x2000?s.sp:ssp)-6;const auto ret=s.pc+2;
    const uint16_t sr=(s.sr&~0x8000)|0x2000;
    idle(4);write16(sp+4,ret);write16(sp,s.sr);write16(sp+2,ret>>16);
    const auto target=read32(0xb0);read16(target);idle(2);
    const auto selection=ns;clock(ns);const auto level=poll(ns);read16(target + 2);clock(ns);
    return {{s.registers,target,sp,sr,ns},eligible(level,sr)?level:0,selection};
}

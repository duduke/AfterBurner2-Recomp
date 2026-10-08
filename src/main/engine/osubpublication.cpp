#include "engine/osubpublication.hpp"
#include <stdexcept>

OSubPublication::State OSubPublication::step(SubBus& bus,State s,const Clock& clock,const Phase& phase,const Select& select) const {
    const auto pc=s.pc;
    if(s.ns%CYCLE_NS||s.sp&1)throw std::runtime_error("unsupported sub publication context");
    auto idle=[&](unsigned n){s.ns+=n*CYCLE_NS;};
    auto read=[&](uint32_t a,uint16_t mask=0xffff){clock(s.ns);auto v=mask==0xff00?uint16_t(bus.read8(a)<<8):bus.read16(a);phase(false,a&0xffffe,v&mask,mask,pc,s.ns);idle(4);return v;};
    auto write=[&](uint32_t a,uint16_t v,uint16_t mask){clock(s.ns);if(mask==0xffff)bus.write16(a,v);else bus.write8(a+(mask==255),mask==255?v:v>>8);phase(true,a&0xffffe,v&mask,mask,pc,s.ns);idle(4);};
    auto latch=[&](){if(select)select(s.ns,s.sr);};
    auto final=[&](uint32_t a){latch();return read(a);};
    auto nz=[&](uint16_t v){s.sr=(s.sr&0xfff0)|(v?0:4)|(v&0x8000?8:0);};
    auto branch=[&](bool take,uint32_t target){idle(2);if(take){read(target);final(target+2);s.pc=target;}else{idle(2);final(pc+4);s.pc=pc+2;}};
    switch(pc){
    // $000432/$00043E/$00044A/$00044E: BSR.W wait/sprite callers.
    // The callee frontiers stay external, but the return address is owned.
    case 0x432:case 0x43e:case 0x44a:case 0x44e:{
        const auto target=pc==0x432?0x556:pc==0x43e?0x5c4:pc==0x44a?0x5ee:0x640;
        idle(2);s.sp-=4;write(s.sp,(pc+4)>>16,0xffff);write(s.sp+2,pc+4,0xffff);
        read(target);final(target+2);s.pc=target;break;}
    // $000436/$000442: LSR.W $300/$302(A6), one-bit memory shift.
    case 0x436:case 0x442:{read(pc+4);auto a=s.a[6]+(pc==0x436?0x300:0x302);
        auto v=read(a);nz(v>>1);s.sr=(s.sr&~0x11)|(v&1?0x11:0);
        read(pc+6);latch();write(a,v>>1,0xffff);s.pc=pc+4;break;}
    // $00043A/$000446: BCC.W skips the optional sub callee.
    case 0x43a:case 0x446:{auto target=pc+8;idle(2);
        if(!(s.sr&1)){read(target);final(target+2);s.pc=target;}
        else{idle(2);read(pc+4);final(pc+6);s.pc=pc+4;}break;}

    // $000452/$000556: TST.W $7C00(A6).
    case 0x452:case 0x556:read(pc+4);nz(read(s.a[6]+0x7c00));final(pc+6);s.pc=pc+4;break;
    // $000456 BGT and $00055A BGE deliberately differ at zero.
    case 0x456:branch(!(s.sr&4)&&bool(s.sr&8)==bool(s.sr&2),0x452);break;
    case 0x55a:branch(bool(s.sr&8)==bool(s.sr&2),0x556);break;
    // $000458: BSET #1,$D6(A6), RMW final prefetch before write.
    case 0x458:{read(pc+4);read(pc+6);auto v=read(s.a[6]+0xd6,0xff00);s.sr=(s.sr&~4)|(v&0x200?0:4);read(pc+8);latch();write(s.a[6]+0xd6,v|0x200,0xff00);s.pc=pc+6;break;}
    // $00055C: RTS, high/low stack reads then two target prefetches.
    case 0x55c:{auto h=read(s.sp),l=read(s.sp+2);s.sp+=4;s.pc=(uint32_t(h)<<16)|l;read(s.pc);final(s.pc+2);break;}
    // $000640: BTST #0,$D6(A6), preserves every flag except Z.
    case 0x640:{read(pc+4);read(pc+6);auto v=read(s.a[6]+0xd6,0xff00);s.sr=(s.sr&~4)|(v&0x100?0:4);final(pc+8);s.pc=pc+6;break;}
    case 0x646:branch(!(s.sr&4),0x640);break;
    default:throw std::runtime_error("unported sub publication PC");
    }
    clock(s.ns);return s;
}

bool OSubPublication::main_signal(SubBus& b,uint32_t pc,uint16_t operand) {
    switch(pc){
    case 0x14674: // BSET #0,$29C0D6
        b.write8(0x29c0d6,(operand>>8)|1);return true;
    case 0x14798: // BCLR #0,$29C0D6
        b.write8(0x29c0d6,(operand>>8)&~1);return true;
    case 0x1467c: // SUBQ.W #1,$2A3C00
        b.write16(0x2a3c00,operand-1);return true;
    case 0x1486e: // MOVE.W #1,$2A3C00
        b.write16(0x2a3c00,1);return true;
    default:return false;
    }
}

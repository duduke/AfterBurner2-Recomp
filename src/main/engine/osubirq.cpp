// Original bounded port; phase ordering follows MAME m68000-sdf.cpp
// (BSD-3-Clause, Olivier Galibert). No ROM tables or CPU decoder included.
#include "engine/osubirq.hpp"
#include <stdexcept>

OSubIrq::State OSubIrq::reset(SubBus& bus,State s,uint32_t bus_pc,const Clock& clock,const Phase& phase) const {
    // MAME state_reset_df: rstp3..rstp6 (8 internal clocks), four vector
    // reads, target prefetch, two internal clocks, second target prefetch.
    if(s.ns%80)throw std::runtime_error("unaligned sub RESET clock");
    auto read=[&](uint32_t a){clock(s.ns);auto v=bus.read16(a);phase(false,a&0xffffe,v,0xffff,bus_pc,s.ns);s.ns+=4*80;return v;};
    s.ns+=8*80;s.sr=(s.sr&~0x8000)|0x2700;
    auto high=read(0);s.ssp=uint32_t(high)<<16;s.ssp|=read(2);s.sp=s.ssp;
    high=read(4);s.pc=uint32_t(high)<<16;s.pc|=read(6);
    read(s.pc);s.ns+=2*80;read(s.pc+2);clock(s.ns);return s;
}

OSubIrq::State OSubIrq::enter(SubBus& bus,State s,uint32_t bus_pc,const Clock& clock,const Phase& phase,const Clock& acknowledge) const {
    if(s.ns%80 || s.ssp&1 || ((s.sr>>8)&7)>=4)throw std::runtime_error("unsupported sub IRQ entry");
    const auto oldsr=s.sr;const auto ret=s.pc;
    if(!(oldsr&0x2000))s.usp=s.sp;
    auto idle=[&](unsigned cycles){s.ns+=cycles*80;};
    auto write=[&](uint32_t a,uint16_t v){clock(s.ns);bus.write16(a,v);phase(true,a&0xffffe,v,0xffff,bus_pc,s.ns);idle(4);};
    auto read=[&](uint32_t a){clock(s.ns);auto v=bus.read16(a);phase(false,a&0xffffe,v,0xffff,bus_pc,s.ns);idle(4);return v;};
    s.sp=s.ssp;s.sr=(oldsr&0x38ff)|0x2400;
    idle(6);write(s.sp-2,ret);
    // Autovector VPA synchronizes to the ten-cycle E clock.
    auto cycle=s.ns/80-s.cycle_bias,mod=cycle%10;
    const auto wait=unsigned(cycle-mod+(mod<7?10:20)-cycle);
    // MAME vpa_sync before_time can stop a CPU grant until the E-clock
    // boundary. The CPU-space read then costs four clocks plus vpa_after's
    // one clock, followed by four internal microcode clocks before trap4.
    if(acknowledge)acknowledge(s.ns+wait*80);
    idle(wait+9);
    s.sp-=6;write(s.sp,oldsr);write(s.sp+2,ret>>16);
    auto high=read(0x70);s.pc=(uint32_t(high)<<16)|read(0x72);
    if(s.pc!=0x55e)throw std::runtime_error("unexpected sub IRQ vector");
    read(s.pc);idle(2);read(s.pc+2);
    s.ssp=s.sp;clock(s.ns);return s;
}

OSubIrq::State OSubIrq::step(SubBus& bus,State s,const Clock& advance,const Phase& phase,const OSubProgress::Select& select,const Checkpoint& checkpoint) const {
    const Clock clock=[&](uint64_t ns){advance(ns);if(checkpoint){auto live=s;if(live.sr&0x2000)live.ssp=live.sp;else live.usp=live.sp;checkpoint(live);}};
    if(!startup_instruction(s.pc)&&s.pc!=0x420 && s.pc!=0x428 && (s.pc<0x55e || s.pc>0x592)){
        static_cast<OSubProgress::State&>(s)=OSubProgress{}.step(bus,s,advance,phase,select,[&](const OSubProgress::State& partial){if(checkpoint){auto live=s;static_cast<OSubProgress::State&>(live)=partial;if(live.sr&0x2000)live.ssp=live.sp;else live.usp=live.sp;checkpoint(live);}});
        if(s.sr&0x2000)s.ssp=s.sp;else s.usp=s.sp;
        return s;
    }
    if((s.pc>=0x55e && !startup_instruction(s.pc)&&!(s.sr&0x2000))||s.ns%80||s.sp&1)throw std::runtime_error("unsupported sub ISR state");
    const auto pc=s.pc;auto& sr=s.sr;auto& sp=s.sp;uint32_t next=pc;
    auto idle=[&](unsigned n){s.ns+=n*80;};
    auto read=[&](uint32_t a){clock(s.ns);auto v=bus.read16(a);phase(false,a&0xffffe,v,0xffff,pc,s.ns);idle(4);return v;};
    auto readbyte=[&](uint32_t a){clock(s.ns);auto v=bus.read8(a);phase(false,a&0xffffe,a&1?v:v<<8,a&1?0xff:0xff00,pc,s.ns);idle(4);return v;};
    auto write=[&](uint32_t a,uint16_t v){clock(s.ns);bus.write16(a,v);phase(true,a&0xffffe,v,0xffff,pc,s.ns);idle(4);};
    auto writebyte=[&](uint32_t a,uint8_t v){clock(s.ns);bus.write8(a,v);phase(true,a&0xffffe,a&1?v:v<<8,a&1?0xff:0xff00,pc,s.ns);idle(4);};
    auto latch=[&](){if(select)select(s.ns,sr);};
    auto final=[&](uint32_t a){latch();return read(a);};
    auto word=[](uint32_t& r,uint16_t v){r=(r&0xffff0000)|v;};
    auto nz=[&](uint16_t v){sr=(sr&0xfff0)|(v?0:4)|(v&0x8000?8:0);};
    auto branch=[&](bool take,uint32_t target,unsigned length){idle(2);if(take){read(target);final(target+2);next=target;}else{idle(2);if(length==4)read(pc+4);final(pc+length+2);next=pc+length;}};
    switch(pc){
    // Reset startup and TRAP #10's interrupt-mask service, literal ROM PCs.
    // Microcode: pea_adr32, move_w_*_aipd, trap_imm4 and rte in MAME.
    case 0x402:read(0x406);read(0x408);sp-=4;write(sp,0);write(sp+2,0x40e);final(0x40a);next=0x408;break;
    case 0x408:read(0x40c);nz(0x700);final(0x40e);sp-=2;write(sp,0x700);next=0x40c;break;
    case 0x40c:case 0x63e:{if(!(sr&0x2000))throw std::runtime_error("unverified sub startup RTE privilege exception");auto restored=read(sp);auto high=read(sp+2);next=(uint32_t(high)<<16)|read(sp+4);sp+=6;s.ssp=sp;sr=restored&0xa71f;if(!(sr&0x2000))sp=s.usp;read(next);final(next+2);break;}
    case 0x40e:s.a[6]=0x9c000;read(0x412);read(0x414);final(0x416);next=0x414;break;
    case 0x414:sp=s.a[6]+0x7f80;read(0x418);final(0x41a);next=0x418;break;
    case 0x418:s.d[0]=3;nz(3);final(0x41c);next=0x41a;break;
    case 0x41a:{const auto saved=sr;if(!(sr&0x2000)){s.usp=sp;sp=s.ssp;}sr=(sr&~0x8000)|0x2000;idle(4);
        write(sp-2,0x41c);write(sp-6,saved);write(sp-4,0);sp-=6;
        auto high=read(0xa8);next=(uint32_t(high)<<16)|read(0xaa);read(next);idle(2);final(next+2);break;}
    case 0x41c:read(0x420);readbyte(s.a[6]+0x321);sr=(sr&0xfff0)|4;read(0x422);latch();writebyte(s.a[6]+0x321,0);next=0x420;break;
    case 0x42a:read(0x42e);read(s.a[6]+0x70);read(s.a[6]+0x72);sr=(sr&0xfff0)|4;
        read(0x430);write(s.a[6]+0x72,0);latch();write(s.a[6]+0x70,0);next=0x42e;break;
    case 0x42e:idle(2);sp-=4;write(sp,0);write(sp+2,0x432);read(0x53c);final(0x53e);next=0x53c;break;
    case 0x62e:case 0x636:{const auto reg=pc==0x62e?0:1;read(pc+4);auto v=uint16_t(s.d[reg]&(reg?0xf8ff:7));word(s.d[reg],v);nz(v);final(pc+6);next=pc+4;break;}
    case 0x632:{read(0x636);auto v=uint16_t(s.d[0]);bool carry=false,overflow=false;
        for(unsigned i=0;i<8;++i){auto old=v;carry=v&0x8000;v=uint16_t(v<<1);overflow|=bool((old^v)&0x8000);idle(2);}
        word(s.d[0],v);sr=(sr&0xffe0)|(carry?0x11:0)|(overflow?2:0)|(v?0:4)|(v&0x8000?8:0);latch();idle(2);next=0x634;break;}
    case 0x634:{auto v=read(sp);sp+=2;word(s.d[1],v);nz(v);final(0x638);next=0x636;break;}
    case 0x63a:{auto v=uint16_t(s.d[1]|s.d[0]);word(s.d[1],v);nz(v);final(0x63e);next=0x63c;break;}
    case 0x63c:{auto v=uint16_t(s.d[1]);nz(v);final(0x640);sp-=2;write(sp,v);next=0x63e;break;}
    // $420 CMPI.L #$55555555,112(A6); $428 BNE $420: initial user wait.
    case 0x420:{read(0x424);read(0x426);read(0x428);auto high=read(s.a[6]+112);auto old=(uint32_t(high)<<16)|read(s.a[6]+114);const uint32_t operand=0x55555555,value=old-operand;bool carry=old<operand,overflow=((old^operand)&(old^value)&0x80000000)!=0;sr=(sr&0xfff0)|(carry?1:0)|(overflow?2:0)|(value?0:4)|(value&0x80000000?8:0);final(0x42a);next=0x428;break;}
    case 0x428:branch(!(sr&4),0x420,2);break;
    // $55E MOVEM.L A6..A0/D7..D0,-(A7): low/high predecrement writes.
    case 0x55e:
        read(0x562);
        for(int i=6;i>=0;--i){write(sp-2,s.a[i]);write(sp-4,s.a[i]>>16);sp-=4;}
        for(int i=7;i>=0;--i){write(sp-2,s.d[i]);write(sp-4,s.d[i]>>16);sp-=4;}
        final(0x564);next=0x562;break;
    // $562 LEA ($9C000).L,A6.
    case 0x562:s.a[6]=0x9c000;read(0x566);read(0x568);final(0x56a);next=0x568;break;
    // $568 BTST #0,0(A6), $56E BNE $568.
    case 0x568:{read(0x56c);read(0x56e);auto v=readbyte(s.a[6]);sr=(sr&~4)|(v&1?0:4);final(0x570);next=0x56e;break;}
    case 0x56e:branch(!(sr&4),0x568,2);break;
    // $570 TST.B 73(A6), $574 BEQ $586.
    case 0x570:{read(0x574);auto v=readbyte(s.a[6]+73);sr=(sr&0xfff0)|(v?0:4)|(v&0x80?8:0);final(0x576);next=0x574;break;}
    case 0x574:branch(sr&4,0x586,4);break;
    // $578 SUBQ.B #1,73(A6), $57C BGT $586.
    case 0x578:{read(0x57c);auto old=readbyte(s.a[6]+73);auto v=uint8_t(old-1);bool carry=old==0,overflow=old==0x80;sr=(sr&0xffe0)|(carry?0x11:0)|(overflow?2:0)|(v?0:4)|(v&0x80?8:0);read(0x57e);latch();writebyte(s.a[6]+73,v);next=0x57c;break;}
    case 0x57c:branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x586,4);break;
    // $580 BSET #0,72(A6).
    case 0x580:{read(0x584);read(0x586);auto v=readbyte(s.a[6]+72);sr=(sr&~4)|(v&1?0:4);read(0x588);latch();writebyte(s.a[6]+72,v|1);next=0x586;break;}
    // $586 MOVE.W #$39,D0; $58A DBF D0,$58A.
    case 0x586:read(0x58a);word(s.d[0],0x39);sr=(sr&0xfff0);final(0x58c);next=0x58a;break;
    case 0x58a:{idle(2);read(0x58a);auto count=uint16_t(s.d[0]-1);word(s.d[0],count);if(count!=0xffff){final(0x58c);next=0x58a;}else{read(0x58e);final(0x590);next=0x58e;}break;}
    // $58E MOVEM.L (A7)+,D0..D7/A0..A6; includes discarded final data read.
    case 0x58e:
        read(0x592);
        for(auto& d:s.d){auto high=read(sp);d=(uint32_t(high)<<16)|read(sp+2);sp+=4;}
        for(auto& a:s.a){auto high=read(sp);a=(uint32_t(high)<<16)|read(sp+2);sp+=4;}
        read(sp);final(0x594);next=0x592;break;
    // $592 RTE: restore SR before return prefetch and select active stack bank.
    case 0x592:{auto restored=read(sp);auto high=read(sp+2);next=(uint32_t(high)<<16)|read(sp+4);sp+=6;s.ssp=sp;sr=restored&0xa71f;if(!(sr&0x2000))sp=s.usp;read(next);final(next+2);break;}
    default:throw std::runtime_error("unported sub ISR instruction");
    }
    s.pc=next;if(sr&0x2000)s.ssp=sp;else s.usp=sp;clock(s.ns);return s;
}

OSubIrq::Instruction OSubIrq::instruction(SubBus& bus,State s,const Clock& clock,const Phase& phase,const Poll& poll,const Checkpoint& checkpoint) const {
    Instruction result;bool selected=false;
    result.state=step(bus,s,clock,phase,[&](uint64_t ns,uint16_t sr){
        selected=true;result.poll_ns=ns;auto level=poll(ns);
        if(level!=0&&level!=4)throw std::runtime_error("unsupported sub IPL");
        result.irq=level>((sr>>8)&7)?level:0;
    },checkpoint);
    if(!selected)throw std::runtime_error("sub instruction has no interrupt selection");
    return result;
}

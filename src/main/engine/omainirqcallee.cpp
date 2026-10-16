// Original bounded instruction translations; bus phases follow MAME's
// m68000 microcode implementation (BSD-3-Clause, Olivier Galibert).
// This file has no opcode decoder. Only the address-labelled instructions
// in omainirqcallee.inc can execute; ROM tables are read through MainBus.
#include "engine/omainirqservice.hpp"
#include <stdexcept>
#include <string>

uint32_t OMainIrqService::frame_complex(uint32_t entry,OMainObjects::Registers& r,uint32_t stack) {
    if(observe_leaf)observe_leaf(true,entry,r,ns);
    uint32_t sp=stack,returned=0;unsigned depth=0;
    // Roots establish NZVC before consuming it. Coin BCD also consumes X,
    // computed by the immediately preceding $148B8 counter increment and
    // retained across the RTS-only $14990. RTE restores the saved SR.
    bool z=false,n=false,v=false,c=false,x=callee_x;
    // D/A registers; I immediate; M/P/N indirect/postincrement/predecrement;
    // O displacement; X signed word index; W/L absolute; C/J PC relative.
    enum Mode {D,A,I,M,P,N,O,X,XL,W,L,C,J};
    struct EA {Mode mode;unsigned reg;int64_t value=0;unsigned index=0;};
    enum Operation {ADD,SUB,CMP,AND,OR,EOR,BTST,BCLR,BSET,BCHG,LSL,LSR,ASL,ASR,ROR,ROL};
    enum Condition {T,F,EQ,NE,CS,CC,PL,MI,GE,LT,GT,LE,HI};
    auto mask=[](unsigned size)->uint32_t{return size==1?0xff:size==2?0xffff:0xffffffff;};
    auto reg=[&](EA e)->uint32_t& {if(e.mode==D)return r.d[e.reg];if(e.reg==7)return sp;return r.a[e.reg];};
    auto flags=[&](uint32_t x,unsigned size){x&=mask(size);z=x==0;n=x&(1u<<(size*8-1));v=c=false;};
    auto extension=[](EA e)->unsigned {
        switch(e.mode){case W:case O:case C:return 4;case X:case XL:case J:return 6;case L:return 8;case N:return 2;default:return 0;}
    };
    auto address=[&](EA e,unsigned size) {
        uint32_t a;
        if(e.mode==W||e.mode==L||e.mode==C||e.mode==J)a=e.value;
        else {a=e.reg==7?sp:r.a[e.reg];if(e.mode==N)a-=size==1&&e.reg==7?2:size;}
        if(e.mode==O||e.mode==X||e.mode==XL)a+=e.value;
        if(e.mode==X||e.mode==J)a+=int16_t(r.d[e.index]);
        if(e.mode==XL)a+=r.d[e.index];
        return a;
    };
    auto increment=[&](EA e,unsigned size){if(e.mode==P||e.mode==N){auto& a=e.reg==7?sp:r.a[e.reg];unsigned step=size==1&&e.reg==7?2:size;a+=e.mode==P?step:-step;}};
    auto load=[&](EA e,unsigned size,bool extensions=true) {
        if(e.mode==D||e.mode==A)return reg(e)&mask(size);
        if(e.mode==I){if(extensions)idle(size==4?8:4);return uint32_t(e.value)&mask(size);}
        if(extensions)idle(extension(e));
        const auto a=address(e,size);
        uint32_t value=size==1?read8(a):size==2?read16(a):read32(a);
        increment(e,size);return value;
    };
    auto store=[&](EA e,unsigned size,uint32_t value,bool extensions=true) {
        if(e.mode==D||e.mode==A){auto& d=reg(e);d=(d&~mask(size))|(value&mask(size));return;}
        if(extensions)idle(extension(e));const auto a=address(e,size);
        if(size==1)write8(a,value);else if(size==2)write16(a,value);
        else if(e.mode==N){write16(a+2,value);write16(a,value>>16);}
        else {write16(a,value>>16);write16(a+2,value);}
        increment(e,size);
    };
    auto move=[&](unsigned size,EA src,EA dst,bool movea) {
        const auto value=load(src,size);
        if(dst.mode==N){
            idle(4);store(dst,size,value,false);flags(value,size);return;
        }
        if(movea){reg(dst)=size==2?uint32_t(int16_t(value)):value;idle(4);return;}
        // Memory-to-absolute-long MOVE has one extension phase before its
        // writes and two prefetch phases afterwards. Register sources have
        // both extension phases before their first write.
        bool split=dst.mode==L && src.mode!=D&&src.mode!=A&&src.mode!=I;
        if(split)idle(4);
        store(dst,size,value,!split);flags(value,size);idle(split?8:4);
    };
    auto moveq=[&](EA src,unsigned dst){r.d[dst]=uint32_t(src.value);flags(r.d[dst],4);idle(4);};
    auto lea=[&](EA src,unsigned dst){r.a[dst]=address(src,2);idle(src.mode==M?4:src.mode==X||src.mode==XL||src.mode==J?12:src.mode==L?12:8);};
    auto adda=[&](unsigned size,EA src,unsigned dst,bool subtract){auto value=load(src,size);auto& a=dst==7?sp:r.a[dst];uint32_t delta=size==2?uint32_t(int16_t(value)):value;a+=subtract?-delta:delta;idle(size==2||src.mode==D||src.mode==A||src.mode==I?8:6);};
    auto alu=[&](Operation op,unsigned size,EA src,EA dst,bool immediate,bool quick) {
        uint32_t a;
        if(quick)a=uint32_t(src.value);else a=load(src,size);
        if(dst.mode==A){reg(dst)+=op==SUB?-a:a;idle(8);return;}
        const bool mem=dst.mode!=D;
        // A read/modify/write EA is advanced only after the write.
        EA target=dst;if(mem&&(dst.mode==P||dst.mode==N))target.mode=M;
        if(mem){idle(extension(dst));target={L,0,address(dst,size)};}
        const auto b=load(target,size,false);const uint32_t sign=1u<<(size*8-1),m=mask(size);
        uint32_t result=0;
        if(op==ADD)result=b+a;else if(op==SUB||op==CMP)result=b-a;
        else if(op==AND)result=b&a;else if(op==OR)result=b|a;else result=b^a;
        flags(result,size);
        if(op==ADD){c=uint64_t(b)+a>m;v=(~(b^a)&(b^result)&sign)!=0;}
        if(op==SUB||op==CMP){c=b<a;v=((b^a)&(b^result)&sign)!=0;}
        if(op==ADD||op==SUB)x=c;
        if(mem){idle(4);if(op!=CMP){store(target,size,result,false);increment(dst,size);}}
        else {if(op!=CMP)store(dst,size,result);idle(size==4?(immediate?(op==CMP?6:8):quick?8:src.mode==D||src.mode==A?8:6):4);}
    };
    auto unary=[&](bool clear,unsigned size,EA dst){
        const bool mem=dst.mode!=D;EA target=dst;
        if(mem){idle(extension(dst));target={L,0,address(dst,size)};}
        const auto old=load(target,size,false);flags(clear?0:old,size);idle(clear&&size==4&&!mem?6:4);
        if(clear){store(target,size,0,false);if(mem)increment(dst,size);}
    };
    auto bit=[&](Operation op,EA source,EA dst){
        const unsigned count=(source.mode==D?r.d[source.reg]:uint32_t(source.value))%(dst.mode==D?32:8),size=dst.mode==D?4:1;
        idle(source.mode==D?0:4);EA target=dst;if(dst.mode!=D){idle(extension(dst));target={L,0,address(dst,1)};}
        const uint32_t old=load(target,size,false),m=1u<<count;z=!(old&m);
        uint32_t next=op==BCLR?old&~m:op==BSET?old|m:old^m;
        if(dst.mode==D){if(op!=BTST)store(dst,4,next);idle(op==BTST?6:op==BCLR?(count>=16?10:8):count>=16?8:6);}
        else {idle(4);if(op!=BTST)store(target,1,next,false);increment(dst,1);}
    };
    auto shift=[&](Operation op,unsigned size,EA source,unsigned dst){
        unsigned count=source.mode==D?r.d[source.reg]&63:uint32_t(source.value);uint32_t shifted=r.d[dst]&mask(size);bool carry=false,overflow=false;
        for(unsigned i=0;i<count;++i){auto before=shifted;
            if(op==LSL||op==ASL||op==ROL){carry=shifted>>(size*8-1);shifted=((shifted<<1)|(op==ROL?uint32_t(carry):0))&mask(size);overflow|=((before^shifted)&(1u<<(size*8-1)))!=0;}
            else {carry=shifted&1;if(op==ASR)shifted=(shifted>>1)|(shifted&(1u<<(size*8-1)));else if(op==ROR)shifted=(shifted>>1)|(uint32_t(carry)<<(size*8-1));else shifted>>=1;}
        }
        store({D,dst},size,shifted);flags(shifted,size);c=carry;if(count&&op!=ROR&&op!=ROL)x=carry;v=op==ASL&&overflow;idle((size==4?8:6)+2*count);
    };
    auto swap=[&](unsigned dst){auto x=r.d[dst];r.d[dst]=(x<<16)|(x>>16);flags(r.d[dst],4);idle(4);};
    auto mulu=[&](EA src,unsigned dst){const uint32_t a=load(src,2),b=uint16_t(r.d[dst]);unsigned ones=0;for(auto x=a;x;x>>=1)ones+=x&1;r.d[dst]=a*b;flags(r.d[dst],4);idle(38+2*ones);};
    auto divu=[&](EA src,unsigned dst){const uint32_t divisor=load(src,2),dividend=r.d[dst];if(!divisor)throw std::runtime_error("unexpected decimal division by zero");
        if((dividend>>16)>=divisor){v=true;c=false;idle(10);return;}
        unsigned cycles=38;uint32_t partial=dividend,high=divisor<<16;
        for(unsigned i=0;i<15;++i){const auto old=partial;partial<<=1;if(old&0x80000000)partial-=high;else {cycles+=2;if(partial>=high){partial-=high;--cycles;}}}
        r.d[dst]=((dividend%divisor)<<16)|(dividend/divisor);flags(dividend/divisor,2);idle(cycles*2);
    };
    auto condition=[&](Condition cc){switch(cc){case T:return true;case F:return false;case EQ:return z;case NE:return !z;case CS:return c;case CC:return !c;case PL:return !n;case MI:return n;case GE:return n==v;case LT:return n!=v;case GT:return !z&&n==v;case LE:return z||n!=v;case HI:return !c&&!z;}return false;};
    auto branch=[&](Condition cc,uint32_t target,bool wide){bool taken=condition(cc);idle(taken?10:wide?12:8);if(taken)pc=target;return taken;};
    auto db=[&](Condition cc,unsigned d,uint32_t target){if(condition(cc)){idle(12);return false;}auto x=uint16_t(r.d[d]-1);r.d[d]=(r.d[d]&0xffff0000)|x;idle(x==0xffff?14:10);if(x!=0xffff)pc=target;return x!=0xffff;};
    auto call=[&](EA target,uint32_t retpc,bool bsr){auto next=address(target,2);idle(bsr?2:target.mode==L?8:6);sp-=4;write16(sp,retpc>>16);write16(sp+2,retpc);idle(bsr?8:4);++depth;pc=next;};
    auto jump=[&](EA target){auto next=address(target,2);idle(target.mode==J||target.mode==X||target.mode==XL?14:target.mode==C?10:extension(target)+4);pc=next;};
    auto ret=[&](){returned=read32(sp);sp+=4;idle(8);if(depth){--depth;pc=returned;return false;}if(sp!=stack+4)throw std::runtime_error("unbalanced IRQ4 callee stack");callee_x=x;if(observe_leaf)observe_leaf(false,entry,r,ns);return true;};
    auto movem=[&](bool save){idle(4);if(save){write16(sp-2,r.a[4]);write16(sp-4,r.a[4]>>16);sp-=4;idle(4);}else {r.a[4]=read32(sp);sp+=4;read16(sp);idle(4);}};
    auto negate=[&](bool logical,unsigned size,EA dst) {
        auto old=load(dst,size);uint32_t value=logical?~old:uint32_t(0-old);
        flags(value,size);if(!logical){c=old!=0;v=old==(1u<<(8*size-1));x=c;}
        store(dst,size,value);idle(size==4?6:4);
    };
    auto movem_mask=[&](bool save,unsigned mask) {
        idle(4);
        if(save){
            for(int i=15;i>=0;--i)if(mask&(1u<<i)){
                auto value=i>=8?r.a[i-8]:r.d[i];write16(sp-2,value);write16(sp-4,value>>16);sp-=4;
            }
            idle(4);
        }else{
            for(unsigned i=0;i<16;++i)if(mask&(1u<<i)){auto value=read32(sp);sp+=4;if(i>=8)r.a[i-8]=value;else r.d[i]=value;}
            read16(sp);idle(4);
        }
    };
    auto bcd=[&](bool subtract,unsigned src,unsigned dst) {
        unsigned a=r.d[src]&255,b=r.d[dst]&255,carry=x;
        unsigned result,raw;
        if(subtract){
            unsigned low=(b&15)-(a&15)-carry;raw=uint16_t(b-a-carry);result=raw;
            if(low&0x10)result-=6;
            if(raw&0x100)result-=0x60;
        }else{
            unsigned low=(a&15)+(b&15)+carry;raw=a+b+carry;result=raw;
            if(low>9)result+=6;
            if(result>0x9f)result+=0x60;
        }
        c=(result&0x300)!=0;x=c;z=z&&uint8_t(result)==0;n=result&0x80;
        v=subtract?!(result&0x80)&&(raw&0x80):(result&0x80)&&!(raw&0x80);
        r.d[dst]=(r.d[dst]&0xffffff00)|uint8_t(result);idle(6);
    };
    pc=entry;
    for(unsigned instructions=0;instructions<1000000;++instructions) {
        switch(pc) {
#include "engine/omainirqcallee.inc"
#include "engine/omainirqgameflow.inc"
        default:throw std::runtime_error("unported IRQ4 callee instruction at "+std::to_string(pc));
        }
    }
    throw std::runtime_error("IRQ4 callee instruction limit");
}

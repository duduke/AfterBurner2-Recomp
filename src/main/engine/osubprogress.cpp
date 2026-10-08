// Bus phases follow MAME's m68000 microcode (BSD-3-Clause, Olivier Galibert).
// Only address-labelled instructions in osubprogress.inc may execute.
#include "engine/osubprogress.hpp"
#include "engine/osubprogress_manifest.hpp"
#include <stdexcept>
#include <algorithm>

OSubProgress::State OSubProgress::step(SubBus& bus,State s,const Clock& clock,const Phase& phase,const Select& select) const {
 if(!sub_instruction(s.pc))return OSubPublication{}.step(bus,s,clock,phase,select);
 if(s.ns%80||s.sp&1)throw std::runtime_error("unsupported sub progress context");
 const auto pc=s.pc;auto& r=s;auto& sp=s.sp;auto& sr=s.sr;uint32_t next=pc;
 auto idle=[&](unsigned n){s.ns+=n*80;};
 auto read16=[&](uint32_t a){clock(s.ns);auto v=bus.read16(a);phase(false,a&0xffffe,v,0xffff,pc,s.ns);idle(4);return v;};
 auto read8=[&](uint32_t a){clock(s.ns);auto v=bus.read8(a);auto mask=uint16_t(a&1?255:0xff00);phase(false,a&0xffffe,uint16_t(a&1?v:v<<8),mask,pc,s.ns);idle(4);return v;};
 auto read32=[&](uint32_t a){auto h=read16(a);return uint32_t(h)<<16|read16(a+2);};
 auto write16=[&](uint32_t a,uint16_t v){clock(s.ns);bus.write16(a,v);phase(true,a&0xffffe,v,0xffff,pc,s.ns);idle(4);};
 auto write8=[&](uint32_t a,uint8_t v){clock(s.ns);bus.write8(a,v);phase(true,a&0xffffe,uint16_t(a&1?v:v<<8),uint16_t(a&1?255:0xff00),pc,s.ns);idle(4);};
 auto word=[](uint32_t& a,uint16_t v){a=(a&0xffff0000)|v;};
 auto byte=[](uint32_t& a,uint8_t v){a=(a&0xffffff00)|v;};
 auto latch=[&](){if(select)select(s.ns,sr);};
 auto final=[&](uint32_t a){latch();return read16(a);};
 auto after=[&](uint32_t a){auto v=read16(a);latch();return v;};
 auto nz=[&](uint16_t v){sr=(sr&0xfff0)|(v?0:4)|(v&0x8000?8:0);};
 auto addsub=[&](uint32_t old,uint32_t operand,unsigned size,bool sub,bool extend){
  uint64_t mask=size==4?0xffffffffULL:(1ULL<<(size*8))-1;uint32_t sign=1u<<(size*8-1);
  uint32_t value=(sub?old-operand:old+operand)&mask;
  bool carry=sub?old<operand:uint64_t(old)+operand>mask;
  bool overflow=((sub?(old^operand):~(old^operand))&(old^value)&sign)!=0;
  sr=(sr&0xfff0)|(carry?1:0)|(overflow?2:0)|(value?0:4)|(value&sign?8:0);
  if(extend)sr=(sr&~16)|(carry?16:0);return value;
 };
 auto addsub_word=[&](uint16_t a,uint16_t b,bool sub,bool x){return uint16_t(addsub(a,b,2,sub,x));};
 auto addsub_byte=[&](uint8_t a,uint8_t b,bool sub,bool x){return uint8_t(addsub(a,b,1,sub,x));};
 auto addsub_long=[&](uint32_t a,uint32_t b,bool sub,bool x){return addsub(a,b,4,sub,x);};
 auto subx_word=[&](uint16_t a,uint16_t b){const unsigned x=(sr>>4)&1;const bool oldz=sr&4;
  auto v=uint16_t(a-b-x);bool carry=uint32_t(b)+x>a;bool overflow=((a^b)&(a^v)&0x8000)!=0;
  sr=(sr&0xffe0)|(carry?0x11:0)|(overflow?2:0)|(oldz&&!v?4:0)|(v&0x8000?8:0);return v;};
 enum class Shift{ASR,ASL,LSR,LSL,ROR,ROL};
 auto shift=[&](unsigned reg,unsigned count,unsigned size,Shift op){
  uint32_t mask=size==4?0xffffffff:(1u<<(size*8))-1,sign=1u<<(size*8-1);
  auto value=r.d[reg]&mask;bool carry=false,overflow=false;
  read16(pc+4);
  for(unsigned i=0;i<count;++i){auto old=value;
   if(op==Shift::ASR||op==Shift::LSR||op==Shift::ROR){carry=value&1;value>>=1;if(op==Shift::ASR)value|=old&sign;if(op==Shift::ROR&&carry)value|=sign;}
   else{carry=value&sign;value=(value<<1)&mask;if(op==Shift::ROL&&carry)value|=1;if(op==Shift::ASL)overflow|=bool((old^value)&sign);}
   idle(2);
  }
  const bool rotate=op==Shift::ROR||op==Shift::ROL;
  sr=(sr&(rotate?0xfff0:count?0xffe0:0xfff0))|(carry?(rotate?1:0x11):0)|(overflow?2:0)|(value?0:4)|(value&sign?8:0);
  if(size==2)word(r.d[reg],value);else r.d[reg]=value;latch();idle(size==2?2:4);next=pc+2;
 };
 auto branch=[&](bool take,uint32_t target,unsigned length){idle(2);if(take){read16(target);final(target+2);next=target;}
  else{idle(2);if(length==4)read16(pc+4);final(pc+length+2);next=pc+length;}};
 switch(pc){
#include "engine/osubprogress.inc"
 default:throw std::runtime_error("unported sub progress instruction");
 }
 s.pc=next;clock(s.ns);return s;
}

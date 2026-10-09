#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <cstring>
#include "engine/omainirqservice.hpp"
#include "engine/osubirq.hpp"
#include "roms.hpp"
namespace {
uint64_t number(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated CPU coverage capture");v=v<<8|c;}return v;}
struct State{uint64_t ns;uint32_t pc;uint16_t sr;OMainObjects::Registers r;uint32_t usp,ssp;uint32_t sp()const{return sr&0x2000?ssp:usp;}};
State state(std::ifstream& f){State s;s.ns=number(f,8);s.pc=number(f,4);s.sr=number(f,2);for(auto& v:s.r.d)v=number(f,4);for(auto& v:s.r.a)v=number(f,4);s.usp=number(f,4);s.ssp=number(f,4);return s;}
struct Phase{uint64_t ns;bool write,space;uint32_t a;uint16_t d,m;};
}
int main(int argc,char** argv)try{
 if(argc!=2||!roms.load_aburner2())throw std::runtime_error("missing nested capture/ROMs");
 std::ifstream f(argv[1],std::ios::binary);char magic[8];if(!f.read(magic,8)||std::memcmp(magic,"AB2NEST1",8))throw std::runtime_error("invalid nested capture");
 MainBus::Memory memory;MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
 OMainIrqService port;State retained{};unsigned calls=0,phases=0,pending=2;uint64_t retained_bias=0;
 while(f.peek()!=EOF){unsigned level=number(f,1);uint32_t buspc=number(f,4);uint64_t bias=number(f,8);auto pre=state(f);
  std::array<uint8_t,0x8000> ram;if(!f.read(reinterpret_cast<char*>(ram.data()),ram.size()))throw std::runtime_error("missing nested RAM");
  if(!calls){retained_bias=bias;retained=pre;std::copy(ram.begin(),ram.end(),memory.bytes.begin());}
  else if(retained.ns!=pre.ns||retained.pc!=pre.pc||retained.sr!=pre.sr||retained.r.d!=pre.r.d||retained.r.a!=pre.r.a||retained.usp!=pre.usp||retained.ssp!=pre.ssp||!std::equal(ram.begin(),ram.end(),memory.bytes.begin()))throw std::runtime_error("nested entry differs from retained first entry");
  if(bias!=retained_bias)throw std::runtime_error("nested E-clock origin differs");
  if(level!=pending||level!=(calls?6u:2u)||calls>=2)throw std::runtime_error("nested selection differs");
  std::vector<Phase> events;auto count=number(f,4);while(count--){Phase e;e.ns=number(f,8);e.write=number(f,1);e.space=number(f,1);e.a=number(f,4);e.d=number(f,2);e.m=number(f,2);events.push_back(e);}auto post=state(f);
  size_t cursor=0;uint64_t now=pre.ns;auto observe=[&](bool w,bool space,uint32_t a,uint16_t d,uint16_t m){
   if(cursor>=events.size())throw std::runtime_error("extra nested phase");const auto& e=events[cursor++];++phases;
   if(e.ns!=now||e.write!=w||e.space!=space||e.a!=a||e.m!=m||((e.d^d)&m)){std::cerr<<"nested phase "<<cursor<<" address "<<std::hex<<a<<'/'<<e.a<<std::dec<<" ns "<<now<<'/'<<e.ns<<'\n';throw std::runtime_error("nested phase differs");}
  };
  MainBus::Trace trace;trace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m){observe(w,false,a,d,m);};bus.trace=&trace;
  OMainIrqService::Context c{retained.r,retained.ns,buspc,retained.pc,retained.ssp,retained.sr,level};
  auto result=port.enter_joint(bus,c,[&](uint64_t t){now=t;},retained_bias,[&](uint64_t t){now=t;observe(false,true,0xfffff0+level*2,24+level,0xffff);},[&](uint16_t sr,uint64_t){pending=OMainIrqService::eligible(6,sr)?6:0;});
  retained={result.ns,result.pc,result.sr,result.registers,retained.usp,result.sp};
  if(cursor!=events.size()||retained.ns!=post.ns||retained.pc!=post.pc||retained.sr!=post.sr||retained.r.d!=post.r.d||retained.r.a!=post.r.a||retained.usp!=post.usp||retained.ssp!=post.ssp)throw std::runtime_error("nested post-state differs");++calls;
 }
 if(calls!=2||pending)throw std::runtime_error("incomplete nested IRQ fixture");
 std::cout<<"Nested main IRQ2 -> IRQ6: 2 retained entries, "<<phases<<" exact bus phases, 0 differences\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

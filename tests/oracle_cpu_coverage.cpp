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
struct Phase{uint64_t ns;bool write;uint32_t a;uint16_t d,m;};
}
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("expected one CPU capture path");
 std::ifstream f(argv[1],std::ios::binary);char magic[8];
 if(!f.read(magic,8)||std::memcmp(magic,"AB2COV02",8)||!roms.load_aburner2())throw std::runtime_error("missing CPU coverage capture/ROMs");
 std::vector<std::pair<unsigned,uint32_t>> planned;auto total=number(f,4);
 if(!total||total>50000)throw std::runtime_error("invalid CPU fixture plan size");
 for(unsigned i=0;i<total;++i){unsigned cpu=number(f,1);auto pc=uint32_t(number(f,4));planned.emplace_back(cpu,pc);}
 unsigned calls=0,identical=0,bad=0,phases=0;std::map<std::pair<unsigned,uint32_t>,std::array<unsigned,2>> branches;std::map<std::pair<unsigned,uint32_t>,unsigned> coverage;
 while(f.peek()!=EOF){unsigned cpu=number(f,1);uint32_t pc=number(f,4);
  if(calls>=planned.size()||planned[calls]!=std::make_pair(cpu,pc))throw std::runtime_error("CPU fixture plan/order differs");auto pre=state(f);MainBus::Memory memory;
  if(!f.read(reinterpret_cast<char*>(memory.bytes.data()),memory.bytes.size()))throw std::runtime_error("missing CPU fixture RAM");
  std::vector<Phase> events;auto count=number(f,4);if(count>128)throw std::runtime_error("invalid CPU fixture phase count");
  while(count--){Phase e;e.ns=number(f,8);e.write=number(f,1);e.a=number(f,4);e.d=number(f,2);e.m=number(f,2);events.push_back(e);}auto post=state(f);
  ++calls;try {
  MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);size_t cursor=0;uint64_t now=pre.ns;
  auto clock=[&](uint64_t t){now=t;};
  auto observe=[&](bool w,uint32_t a,uint16_t d,uint16_t m){++phases;
   if(cursor>=events.size())throw std::runtime_error("extra CPU fixture phase");const auto& e=events[cursor++];
   if(e.ns!=now||e.write!=w||e.a!=a||e.m!=m||((e.d^d)&m)){
    if(bad<20)std::cerr<<"CPU fixture phase cpu="<<cpu<<" pc="<<std::hex<<pc<<" address="<<a<<'/'<<e.a<<" data="<<d<<'/'<<e.d<<std::dec<<" ns="<<now<<'/'<<e.ns<<'\n';throw std::runtime_error("CPU fixture bus comparison failed");}
  };
  OMainIrqService::Result result{};uint32_t usp=pre.usp,ssp=pre.ssp;
  if(cpu==1){MainBus::Trace trace;trace.access=observe;bus.trace=&trace;OMainIrqService port;
   OMainIrqService::Context c{pre.r,pre.ns,pc,pc,pre.sp(),pre.sr,0};
   c.exception_supervisor_sp=pre.ssp;
   const auto opcode=(roms.maincpu.rom[pc]<<8)|roms.maincpu.rom[pc+1];
   if(opcode==0x4e73){ // Only classify the original ROM instruction; do not decode a port.
    result=port.return_joint(bus,{pre.r,pc,pre.sp(),pre.sr,pre.ns},pre.usp,clock,[](uint64_t){return 0u;},pre.ssp).state;
   }else result=port.caller_step(bus,c,clock,[](uint64_t){return 0u;}).state;
   if(result.sr&0x2000)ssp=result.sp;else usp=result.sp;
  }else if(cpu==2){OSubIrq::State s{};s.pc=pc;s.ns=pre.ns;s.sr=pre.sr;s.sp=pre.sp();s.usp=pre.usp;s.ssp=pre.ssp;s.d=pre.r.d;s.a=pre.r.a;
   auto out=OSubIrq{}.step(bus.sub,s,clock,[&](bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t,uint64_t t){now=t;observe(w,a,d,m);});
   result={{out.d,out.a},out.pc,out.sp,out.sr,out.ns};usp=out.usp;ssp=out.ssp;
  }else throw std::runtime_error("invalid CPU fixture owner");
  if(cursor!=events.size()||result.pc!=post.pc||result.sr!=post.sr||result.ns!=post.ns||result.registers.d!=post.r.d||result.registers.a!=post.r.a||usp!=post.usp||ssp!=post.ssp){
   if(bad<20)std::cerr<<"CPU fixture result cpu="<<cpu<<" pc="<<std::hex<<pc<<" next="<<result.pc<<'/'<<post.pc<<" sr="<<result.sr<<'/'<<post.sr<<" usp="<<usp<<'/'<<post.usp<<" ssp="<<ssp<<'/'<<post.ssp<<std::dec<<" ns="<<result.ns<<'/'<<post.ns<<" phases="<<cursor<<'/'<<events.size()<<'\n';throw std::runtime_error("CPU fixture state comparison failed");}
  const auto* image=cpu==1?roms.maincpu.rom:roms.subcpu.rom;const unsigned opcode=(image[pc]<<8)|image[pc+1];
  if((opcode&0xf000)==0x6000&&((opcode>>8)&15)>=2){const bool c=pre.sr&1,v=pre.sr&2,z=pre.sr&4,n=pre.sr&8;const bool take[]={true,true,!c&&!z,c||z,!c,c,!z,z,!v,v,!n,n,n==v,n!=v,!z&&n==v,z||n!=v};++branches[{cpu,pc}][take[(opcode>>8)&15]];}
  else if((opcode&0xfff8)==0x51c8)++branches[{cpu,pc}][result.pc!=pc+4];
  ++identical;++coverage[{cpu,pc}];
  }catch(const std::exception& e){++bad;if(bad<=20)std::cerr<<"CPU fixture failure "<<cpu<<' '<<std::hex<<pc<<std::dec<<": "<<e.what()<<'\n';}

 }
 for(const auto& [key,n]:coverage)std::cout<<"CPU fixture PC "<<key.first<<' '<<std::hex<<key.second<<std::dec<<": "<<n<<'\n';
 if(calls!=planned.size())throw std::runtime_error("incomplete original-ROM CPU fixture coverage");
 std::cout<<"CPU coverage fixtures: "<<identical<<" of "<<calls<<" identical, "<<bad<<" different, 0 skipped; "<<phases<<" bus phases compared\n";
 if(bad)return 1;
 std::ofstream census(std::string(argv[1])+".coverage.json");census<<"{\"cases\":"<<calls<<",\"records\":[";
 bool first=true;for(const auto& [key,n]:coverage){if(!first)census<<',';first=false;census<<'['<<key.first<<','<<key.second<<','<<n<<']';}
 census<<"],\"branches\":[";first=true;for(const auto& [key,n]:branches){if(!first)census<<',';first=false;census<<'['<<key.first<<','<<key.second<<','<<n[0]<<','<<n[1]<<']';}census<<"]}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

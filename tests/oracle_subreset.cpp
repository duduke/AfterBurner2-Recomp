#include <fstream>
#include <iostream>
#include <cstring>
#include "engine/osubirq.hpp"
#include "roms.hpp"
namespace {
uint64_t number(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){auto c=f.get();if(c<0)throw std::runtime_error("truncated RESET fixture");v=v<<8|c;}return v;}
OSubIrq::State state(std::ifstream& f){OSubIrq::State s;s.ns=number(f,8);s.pc=number(f,4);s.sr=number(f,2);for(auto& v:s.d)v=number(f,4);for(auto& v:s.a)v=number(f,4);s.usp=number(f,4);s.ssp=number(f,4);s.sp=s.sr&0x2000?s.ssp:s.usp;return s;}
}
int main(int argc,char** argv)try{
 std::ifstream f(argc>1?argv[1]:"build/oracle/subreset.bin",std::ios::binary);char header[8];
 if(!f.read(header,8)||memcmp(header,"AB2RST01",8))throw std::runtime_error("invalid RESET fixture");
 if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
 std::array<uint8_t,SubBus::SHARED_BYTES> ram{};std::array<uint8_t,SubBus::ROAD_BYTES> road{};
 SubBus bus(roms.subcpu.rom,roms.subcpu.length,ram.data(),road.data());
 unsigned count=0,phases=0;
 while(f.peek()!=EOF){auto pre=state(f),post=state(f);auto n=number(f,4);unsigned cursor=0;
  auto result=OSubIrq{}.reset(bus,pre,pre.pc,[](uint64_t){},[&](bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t ns){
   if(cursor++>=n)throw std::runtime_error("extra RESET phase");
   auto t=number(f,8);auto address=number(f,4);auto data=number(f,2);auto mask=number(f,2);auto curpc=number(f,4);
   if(w||ns!=t||a!=address||d!=data||m!=mask||pc!=curpc){std::cerr<<"RESET phase ns="<<ns<<'/'<<t<<" address="<<a<<'/'<<address<<" data="<<d<<'/'<<data<<'\n';throw std::runtime_error("RESET bus/timing mismatch");}++phases;
  });
  if(cursor!=n||result.ns!=post.ns||result.pc!=post.pc||result.sr!=post.sr||result.sp!=post.sp||result.d!=post.d||result.a!=post.a||result.usp!=post.usp||result.ssp!=post.ssp)throw std::runtime_error("RESET retained register/vector state mismatch");
  ++count;
 }
 if(count<2)throw std::runtime_error("missing natural RESET assertion/release");
 std::cout<<"Sub RESET: "<<count<<" of "<<count<<" states and "<<phases<<" of "<<phases<<" ordered vector/prefetch phases identical, 0 skipped\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

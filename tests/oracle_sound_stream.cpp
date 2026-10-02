// Continuous sound ring replay. Caller timestamps, selector and enqueue D0
// remain external; ported consumers/producers may never reset to captured RAM.
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "engine/omainsound.hpp"
using Registers=OMainObjects::Registers;
using Event=std::tuple<uint32_t,uint16_t,uint16_t>;
void read(std::ifstream& in,void* p,size_t n) {if(!in.read(static_cast<char*>(p),n)) throw std::runtime_error("truncated sound stream");}
uint32_t u32(std::ifstream& in) {uint8_t b[4];read(in,b,4);return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(b[2]<<8)|b[3];}
uint64_t u64(std::ifstream& in) {auto hi=u32(in);return (uint64_t(hi)<<32)|u32(in);}
Registers regs(std::ifstream& in) {Registers r;for(auto& d:r.d) d=u32(in);for(auto& a:r.a) a=u32(in);return r;}
Event event(std::ifstream& in) {auto a=u32(in),d=u32(in),m=u32(in);return {a,d&m,m};}
void apply_event(MainBus& b,const Event& e) {auto [a,d,m]=e;if(m==0xffff) b.write16(a,d);else if(m==0xff) b.write8(a+1,d);else if(m==0xff00) b.write8(a,d>>8);else throw std::runtime_error("bad byte mask");}
int main(int argc,char** argv) try {
 std::ifstream in(argc>1?argv[1]:"build/oracle/paired_sound.bin",std::ios::binary);
 char magic[8];read(in,magic,8);if(std::memcmp(magic,"AB2SND01",8)) throw std::runtime_error("not sound stream");
 MainBus::Memory memory;MainBus bus(nullptr,0,nullptr,0,memory);
 std::array<uint8_t,0x4c> state;read(in,state.data(),state.size());
 for(unsigned i=0;i<state.size();++i) bus.write8(0x3f8900+i,state[i]);
 unsigned calls=0,identical=0,external=0,producers=0,consumers=0,latches=0;uint64_t last=0;
 while(in.peek()!=std::char_traits<char>::eof()) {
  uint8_t kind;read(in,&kind,1);auto time=u64(in);if(time<last) throw std::runtime_error("sound timeline reversed");last=time;
  if(kind==0) {auto e=event(in);auto [a,d,m]=e;(void)d;(void)m;if(a<0x3f8900 || a>=0x3f894c) throw std::runtime_error("unlisted sound input");apply_event(bus,e);++external;continue;}
  if(kind!=1) throw std::runtime_error("unknown sound record");
  auto entry=u32(in);auto r=regs(in),post=regs(in);auto selector=u32(in);auto n=u32(in);if(n>100) throw std::runtime_error("bad sound writes");
  std::vector<Event> expected,actual;while(n--) expected.push_back(event(in));read(in,state.data(),state.size());
  bus.write16(0x29c09c,selector);
  MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m){if(w) {actual.emplace_back(a,d&m,m);if(a==0xe8016 && (m&255)) ++latches;}},{}};bus.trace=&trace;
  if(entry==0xb7f8) {OMainSound::consume(bus,r);++consumers;}
  else if(entry==0xb840 || entry==0xb84c) {OMainSound::enqueue(bus,r,entry==0xb84c);++producers;}
  else throw std::runtime_error("unknown sound caller");
  bus.trace=nullptr;++calls;
  bool ok=r.a==post.a && r.d==post.d && actual==expected;
  for(unsigned i=0;i<state.size();++i) ok &= bus.read8(0x3f8900+i)==state[i];
  if(ok) ++identical;else if(calls-identical<=8) std::cerr<<"sound call "<<calls<<" entry "<<std::hex<<entry<<std::dec<<" differs registers="<<(r.a==post.a && r.d==post.d)<<" writes="<<(actual==expected)<<'\n';
 }
 std::cout<<"Continuous sound vs arcade: "<<identical<<'/'<<calls<<" identical, "<<calls-identical<<" different, 0 skipped; "<<producers<<" producers, "<<consumers<<" consumers, "<<latches<<" latches, "<<external<<" explicit external ring writes\n";
 return !calls || identical!=calls || !producers || !latches;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

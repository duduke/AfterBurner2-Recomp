#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include "engine/omainirqservice.hpp"
#include "roms.hpp"
#include "hwmath/xboard_timeline.hpp"
namespace {
uint64_t num(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated caller capture");v=v<<8|c;}return v;}
OMainObjects::Registers regs(std::ifstream& f){OMainObjects::Registers r;for(auto& v:r.d)v=num(f,4);for(auto& v:r.a)v=num(f,4);return r;}
struct Event {uint64_t ns;uint32_t pc,a;uint16_t d,m;unsigned w,level;};
Event event(std::ifstream& f){Event e;e.ns=num(f,8);e.pc=num(f,4);e.a=num(f,4);e.d=num(f,2);e.m=num(f,2);e.w=num(f,1);e.level=num(f,1);return e;}
std::vector<Event> events(std::ifstream& f){auto n=num(f,4);if(n>16)throw std::runtime_error("invalid caller event count");std::vector<Event> v;while(n--)v.push_back(event(f));return v;}
}
int main(int argc,char** argv)try{
 std::ifstream f(argc>1?argv[1]:"build/oracle/caller.bin",std::ios::binary);char magic[8];
 if(!f.read(magic,8)||memcmp(magic,"AB2CALL1",8))throw std::runtime_error("invalid caller capture");
 if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
 MainBus::Memory memory;MainBus bus(roms.maincpu.rom,roms.maincpu.length,nullptr,0,memory);
 XBoardTimeline board(bus.compare_timer);
 OMainIrqService service;OMainIrqService::Instruction last{};bool have=false,expected_pending=false;
 unsigned calls=0,entries=0,failed=0,total=0,bad=0,chains=0,chain_bad=0,polls=0,masked=0,edge_frontiers=0,levels[7]={};
 std::map<uint32_t,unsigned> coverage,irqcoverage;
 std::map<uint32_t,uint8_t> owned;
 while(f.peek()!=std::char_traits<char>::eof()){
  auto kind=num(f,1);OMainIrqService::Context c{};c.interrupted_pc=num(f,4);c.boundary_ns=num(f,8);c.supervisor_sp=num(f,4);
  std::vector<Event> expected;OMainObjects::Registers post;uint64_t end;uint32_t sp,next;uint16_t sr;unsigned irq=0;
  bool ok=true;
  if(kind==1){if(expected_pending)throw std::runtime_error("missing caller IRQ entry");c.sr=num(f,2);c.registers=regs(f);expected=events(f);end=num(f,8);sp=num(f,4);sr=num(f,2);post=regs(f);next=num(f,4);irq=num(f,1);
   const bool chain=have&&last.state.pc==c.interrupted_pc&&last.state.ns==c.boundary_ns&&!last.irq;
   if(chain){++chains;auto a=last.state.registers.a;
    // LEA absolute assigns before its first observable prefetch.
    if(c.interrupted_pc==0x1458a)a[6]=c.registers.a[6];
    if(c.interrupted_pc==0x1459c)a[5]=c.registers.a[5];
    const bool same=last.state.sr==c.sr&&last.state.sp==c.supervisor_sp&&last.state.registers.d==c.registers.d&&a==c.registers.a;
    if(!same){++chain_bad;ok=false;if(chain_bad<6)std::cerr<<std::hex<<"Caller chain pc="<<c.interrupted_pc<<" previous sr="<<last.state.sr<<" next sr="<<c.sr<<std::dec<<" start="<<c.boundary_ns<<'\n';}
   }else owned.clear();
   expected_pending=irq!=0;
   ++calls;++coverage[c.interrupted_pc];
  }else if(kind==2){c.return_pc=num(f,4);c.sr=num(f,2);c.level=num(f,1);c.registers=regs(f);end=num(f,8);expected=events(f);sp=num(f,4);sr=num(f,2);
   if(!have||!expected_pending)throw std::runtime_error("entry without instruction");
   ok=last.irq==c.level&&last.state.pc==c.return_pc&&last.state.ns==c.boundary_ns&&last.state.sr==c.sr&&last.state.registers.d==c.registers.d&&last.state.registers.a==c.registers.a;
   // These are outputs of caller_step, never captured completion/return inputs.
   c.return_pc=last.state.pc;c.boundary_ns=last.state.ns;c.sr=last.state.sr;c.registers=last.state.registers;
   if(c.sr&0x2000)ok &= c.supervisor_sp==last.state.sp;
   expected_pending=false;
   ++entries;++levels[c.level];++irqcoverage[c.interrupted_pc];next=c.level==2?0x1461c:c.level==4?0x14674:0x1466c;post=c.registers;
  }else throw std::runtime_error("invalid caller record kind");
  size_t cursor=0;uint64_t now=c.boundary_ns;
  MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m){
   ++total;bool same=cursor<expected.size();
   if(same){const auto& e=expected[cursor++];same=e.ns==now&&e.pc==service.current_pc()&&e.a==a&&e.m==m&&e.w==unsigned(w)&&(e.d&m)==(d&m);
    if(!same&&bad<10)std::cerr<<std::hex<<"Caller pc="<<c.interrupted_pc<<" a="<<a<<'/'<<e.a<<" d="<<(d&m)<<'/'<<(e.d&m)<<std::dec<<" ns="<<now<<'/'<<e.ns<<'\n';}
   if(!same){++bad;ok=false;}
   if(kind==1&&w){auto o=MainBus::memory_offset(a);if(o!=MainBus::UNMAPPED){if(m&0xff00)owned[o]=d>>8;if(m&255)owned[o+1]=d;}}
  }};
  // Entry contexts and external handler/sub changes remain boundaries. Within
  // consecutive caller instructions, a computed main write cannot be replaced.
  trace.before_access=[&](bool w,uint32_t a,uint16_t m){
   if(w||kind!=1||cursor>=expected.size())return;
   const auto& e=expected[cursor];if(e.w||e.a!=a||e.m!=m)return;
   auto o=MainBus::memory_offset(a);if(o==MainBus::UNMAPPED)return;
   auto lane=[&](uint32_t i,uint8_t value){auto it=owned.find(i);if(it!=owned.end()){if(it->second!=value)ok=false;}else memory.bytes[i]=value;};
   if(m&0xff00)lane(o,e.d>>8);if(m&255)lane(o+1,e.d);
  };
  bus.trace=&trace;
  OMainIrqService::Result result;
  if(kind==1){
   last=service.caller_step(bus,c,[&](uint64_t t){now=t;},[&](uint64_t t){
    ++polls;board.advance(t);auto it=expected.begin();for(;it!=expected.end()&&it->ns<t;++it){}
    if(it==expected.end()||it->ns!=t){ok=false;return 0u;}
    // MAME may finish a CPU bus phase beyond a pending scanline timer
    // before delivering that timer. Physical IPL visibility is a supplied
    // boundary; report disagreements with ideal scanline time, never guess.
    if(board.vblank()!=bool(it->level&4))++edge_frontiers;
    if(it->level&&!OMainIrqService::eligible(it->level,c.sr))++masked;
    return it->level;
   });result=last.state;have=true;ok &= last.irq==irq;
  }else{result=service.enter(bus,c,[&](uint64_t t){now=t;});have=false;owned.clear();}
  bus.trace=nullptr;
  ok &= cursor==expected.size()&&result.ns==end&&result.pc==next&&result.sp==sp&&result.sr==sr&&result.registers.d==post.d&&result.registers.a==post.a;
  if(!ok){++failed;if(failed<10)std::cerr<<std::hex<<"Caller record pc="<<c.interrupted_pc<<" result="<<result.pc<<'/'<<next<<" sr="<<result.sr<<'/'<<sr<<" sp="<<result.sp<<'/'<<sp<<std::dec<<" end="<<result.ns<<'/'<<end<<" events="<<cursor<<'/'<<expected.size()<<" IRQ="<<last.irq<<'/'<<irq<<'\n';}
 }
 if(expected_pending)throw std::runtime_error("unfinished caller IRQ entry");
 std::cout<<"Caller instructions/IRQ entries: "<<calls+entries-failed<<" of "<<calls+entries<<" identical, "<<failed<<" different, 0 skipped\nCaller bus events: "<<total-bad<<" of "<<total<<" identical, "<<bad<<" different, 0 skipped\nConsecutive caller boundaries: "<<chains-chain_bad<<" of "<<chains<<" identical, "<<chain_bad<<" different, 0 skipped\nCoverage: instructions="<<calls<<" IRQ entries="<<entries<<" polls="<<polls<<" IRQ2="<<levels[2]<<" IRQ4="<<levels[4]<<" IRQ6="<<levels[6]<<" supplied edge frontiers="<<edge_frontiers<<" masked="<<masked<<'\n';
 for(auto [p,n]:coverage)std::cout<<std::hex<<p<<std::dec<<": "<<n<<" instructions, "<<irqcoverage[p]<<" IRQ entries\n";
 return calls&&entries&&!failed&&!bad?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

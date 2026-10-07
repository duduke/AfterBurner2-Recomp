#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <cstring>
#include <stdexcept>
#include "engine/osubpublication.hpp"
#include "roms.hpp"
namespace {
uint64_t num(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated sub publication capture");v=v<<8|c;}return v;}
using State=OSubPublication::State;
void regs(std::ifstream& f,State& s){for(auto& v:s.d)v=num(f,4);for(auto& v:s.a)v=num(f,4);}
State state(std::ifstream& f){State s;s.ns=num(f,8);s.pc=num(f,4);s.sp=num(f,4);s.sr=num(f,2);regs(f,s);return s;}
struct Event {uint64_t ns;uint32_t order,a;uint16_t d,m;bool w;};
struct Call {State pre,post;std::vector<Event> events;};
struct Signal {uint64_t ns;uint32_t order,pc,a;uint16_t d,m;bool main,read;};
bool same(const State& a,const State& b){return a.ns==b.ns&&a.pc==b.pc&&a.sp==b.sp&&a.sr==b.sr&&a.d==b.d&&a.a==b.a;}
}
int main(int argc,char** argv)try{
 const bool directed=argc>2&&std::string(argv[2])=="--require-endpoints";
 std::ifstream f(argc>1?argv[1]:"build/oracle/subpublication_short.bin",std::ios::binary);char magic[8];
 if(!f.read(magic,8)||memcmp(magic,"AB2SUBP2",8))throw std::runtime_error("invalid sub publication capture");
 std::vector<Call> calls;std::vector<Signal> signals;uint16_t initial_ready=0,initial_mutex=0;bool initial=false;unsigned cuts=0;
 while(f.peek()!=std::char_traits<char>::eof()){
  auto kind=num(f,1);
  if(kind==1){Call c;c.pre=state(f);auto n=num(f,4);if(n>5)throw std::runtime_error("invalid sub phase count");while(n--){Event e;e.ns=num(f,8);e.order=num(f,4);e.a=num(f,4);e.d=num(f,2);e.m=num(f,2);e.w=num(f,1);c.events.push_back(e);}c.post.ns=num(f,8);c.post.sp=num(f,4);c.post.sr=num(f,2);regs(f,c.post);c.post.pc=num(f,4);calls.push_back(c);}
  else if(kind==2||kind==4||kind==6){Signal s;s.ns=num(f,8);s.order=num(f,4);s.pc=num(f,4);s.a=num(f,4);s.d=num(f,2);s.m=num(f,2);s.main=kind==2||kind==6;s.read=kind==6;signals.push_back(s);}
  else if(kind==3){num(f,8);initial_ready=num(f,2);initial_mutex=num(f,2);initial=true;}
  else if(kind==5)++cuts;
  else throw std::runtime_error("invalid sub publication record");
 }
 if(!initial||calls.empty()||!roms.load_aburner2())throw std::runtime_error("missing sub publication inputs/ROMs");
 std::stable_sort(signals.begin(),signals.end(),[](const auto& a,const auto& b){return a.order<b.order;});
 std::array<uint8_t,SubBus::SHARED_BYTES> ram{};
 SubBus bus(roms.subcpu.rom,roms.subcpu.length,ram.data());bus.write16(0xa3c00,initial_ready);bus.write16(0x9c0d6,initial_mutex);
 OSubPublication port;size_t si=0;unsigned failures=0,phase_bad=0,phases=0,chains=0,chain_bad=0,frontiers=0,computed=0,external=0;
 std::map<uint32_t,unsigned> coverage,producers;std::map<uint32_t,std::set<unsigned>> operands;std::map<uint32_t,std::array<unsigned,2>> branches;
 std::map<uint32_t,uint16_t> main_operands;unsigned main_reads=0,main_bad=0,delivery_leads=0;
 uint32_t phase_order=0;uint64_t phase_time=0;
 auto clock=[&](uint64_t){while(si<signals.size()&&signals[si].order<phase_order){const auto& s=signals[si++];const auto a=s.a&0xfffff;
   if(s.ns>phase_time)++delivery_leads;
   if(s.read){auto v=bus.read16(a);++main_reads;main_operands[s.pc]=v;
    if((v&s.m)!=(s.d&s.m)){++main_bad;if(main_bad<6)std::cerr<<"Main signal read mismatch pc="<<std::hex<<s.pc<<std::dec<<'\n';}
   }else if(s.main&&OSubPublication::main_signal(bus,s.pc,[&](){
     if(s.pc!=0x1486e&&(s.pc==0x14674||s.pc==0x14798||s.pc==0x1467c)&&!main_operands.count(s.pc))throw std::runtime_error("main signal writer has no latched operand");
     return main_operands.count(s.pc)?main_operands.at(s.pc):uint16_t(0);
    }())){++computed;++producers[s.pc];main_operands.erase(s.pc);auto expected_a=(s.pc==0x1467c||s.pc==0x1486e)?0xa3c00:0x9c0d6;
    if(a!=unsigned(expected_a)||(bus.read16(a)&s.m)!=(s.d&s.m)){++main_bad;if(main_bad<6)std::cerr<<"Main signal mismatch pc="<<std::hex<<s.pc<<" a="<<a<<" got="<<bus.read16(a)<<" expected="<<s.d<<std::dec<<" ns="<<s.ns<<'\n';}
   }else{++external;auto old=bus.read16(a);bus.write16(a,(old&~s.m)|(s.d&s.m));}
  }};
 State last{};bool have=false;
 for(auto c:calls){
  ++coverage[c.pre.pc];bool ok=true;
  if(have&&last.ns==c.pre.ns&&last.pc==c.pre.pc){++chains;if(!same(last,c.pre)){++chain_bad;ok=false;}c.pre=last;}else ++frontiers;
  // Only external stack/data operands may seed entry RAM. Shared handshake
  // bytes are always retained across both CPU streams, including IRQ gaps.
  for(const auto& e:c.events)if(!e.w&&e.a>=0x80000&&e.a<0xc0000&&e.a!=0x9c0d6&&e.a!=0xa3c00){auto old=bus.read16(e.a);bus.write16(e.a,(old&~e.m)|(e.d&e.m));}
  size_t cursor=0;
  auto advance=[&](uint64_t t){phase_order=cursor<c.events.size()?c.events[cursor].order:c.events.back().order+1;phase_time=t;clock(t);};
  auto result=port.step(bus,c.pre,advance,[&](bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t t){
   ++phases;bool equal=cursor<c.events.size();
   if(equal){const auto& e=c.events[cursor++];equal=e.ns==t&&e.a==a&&e.w==w&&e.m==m&&(e.d&m)==(d&m);}
   if(!equal){ok=false;++phase_bad;if(phase_bad<8)std::cerr<<"Sub publication phase pc="<<std::hex<<pc<<" a="<<a<<" d="<<d<<std::dec<<" t="<<t<<'\n';}
   if(!w&&a==0xa3c00)operands[pc].insert(d);
  });
  ok&=cursor==c.events.size()&&same(result,c.post);if(!ok){++failures;if(failures<8)std::cerr<<"Sub publication record pc="<<std::hex<<c.pre.pc<<" next="<<result.pc<<'/'<<c.post.pc<<" sr="<<result.sr<<'/'<<c.post.sr<<std::dec<<" end="<<result.ns<<'/'<<c.post.ns<<'\n';}
  if(c.pre.pc==0x456||c.pre.pc==0x55a||c.pre.pc==0x646)++branches[c.pre.pc][result.pc!=c.pre.pc+2];
  last=result;have=true;
 }
 std::cout<<"Main signal reads/writes: "<<main_reads+computed-main_bad<<" of "<<main_reads+computed<<" identical, "<<main_bad<<" different, 0 skipped\n";
 std::cout<<"Sub publication instructions: "<<calls.size()-std::min<size_t>(failures,calls.size())<<" of "<<calls.size()<<" identical, "<<failures<<" different, 0 skipped\nSub publication bus phases: "<<phases-phase_bad<<" of "<<phases<<" identical, "<<phase_bad<<" different, 0 skipped\nSub publication chains: "<<chains-chain_bad<<" of "<<chains<<" identical, "<<chain_bad<<" different, 0 skipped\nSignal producers: "<<computed<<" computed, "<<external<<" supplied writes; "<<main_reads<<" checked main RMW reads, "<<delivery_leads<<" supplied cross-CPU delivery leads; "<<frontiers<<" supplied sub entry/IRQ/callee frontiers; "<<cuts<<" partial IRQ cuts\n";
 for(uint32_t pc:{0x452,0x456,0x458,0x556,0x55a,0x55c,0x640,0x646}){std::cout<<std::hex<<pc<<std::dec<<": "<<coverage[pc]<<" instructions\n";if(!coverage[pc])throw std::runtime_error("missing sub publication PC");}
 for(auto [p,n]:branches){std::cout<<"Branch "<<std::hex<<p<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';if(!n[0]||!n[1])throw std::runtime_error("missing sub publication branch direction");}
 for(uint32_t p:{0x14674,0x14798,0x1467c,0x1486e})if(!producers[p])throw std::runtime_error("missing main signal producer");
 if(directed)for(uint32_t p:{0x452,0x556})for(unsigned v:{0,1,0x7fff,0x8000,0xffff,0xfffe})if(!operands[p].count(v))throw std::runtime_error("missing sub readiness endpoint at "+std::to_string(p)+" value "+std::to_string(v));
 return failures||phase_bad||chain_bad||main_bad?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

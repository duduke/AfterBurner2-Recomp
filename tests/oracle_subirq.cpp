#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <cstring>
#include <stdexcept>
#include "engine/osubirq.hpp"
#include "hwmath/xboard_sub_irq_selection.hpp"
#include "engine/osubprogress_manifest.hpp"
#include "roms.hpp"
namespace {
uint64_t num(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated sub progress capture");v=v<<8|c;}return v;}
using State=OSubIrq::State;
void regs(std::ifstream& f,State& s){for(auto& v:s.d)v=num(f,4);for(auto& v:s.a)v=num(f,4);}
State state(std::ifstream& f){State s;s.ns=num(f,8);s.pc=num(f,4);s.sp=num(f,4);s.sr=num(f,2);regs(f,s);s.usp=num(f,4);s.ssp=num(f,4);s.cycle_bias=num(f,8);return s;}
struct Event {uint64_t ns;uint32_t order,a;uint16_t d,m;bool w;};
struct Call {bool frontier=false;State pre,post;std::vector<Event> events;bool irq=false;uint32_t buspc=0;};
struct Signal {uint64_t ns;uint32_t order,pc,a;uint16_t d,m;bool main,read;};
bool same(const State& a,const State& b){return a.cycle_bias==b.cycle_bias&&a.usp==b.usp&&a.ssp==b.ssp&& a.ns==b.ns&&a.pc==b.pc&&a.sp==b.sp&&a.sr==b.sr&&a.d==b.d&&a.a==b.a;}
}
int main(int argc,char** argv)try{
 const bool directed=argc>2&&std::string(argv[2])=="--require-endpoints";
 std::ifstream f(argc>1?argv[1]:"build/oracle/subirq_short.bin",std::ios::binary);char magic[8];
 if(!f.read(magic,8)||memcmp(magic,"AB2SUBI1",8))throw std::runtime_error("invalid sub progress capture");
 std::vector<Call> calls;std::vector<Signal> signals;uint16_t initial_ready=0,initial_mutex=0;bool initial=false;unsigned cuts=0;std::array<uint8_t,SubBus::SHARED_BYTES> initial_ram{};
 bool frontier=true;
 while(f.peek()!=std::char_traits<char>::eof()){
  auto kind=num(f,1);
  if(kind==1||kind==7){Call c;c.frontier=frontier;frontier=false;c.irq=kind==7;c.pre=state(f);if(c.irq)c.buspc=num(f,4);auto n=num(f,4);if(n>64)throw std::runtime_error("invalid sub phase count");while(n--){Event e;e.ns=num(f,8);e.order=num(f,4);e.a=num(f,4);e.d=num(f,2);e.m=num(f,2);e.w=num(f,1);c.events.push_back(e);}if(c.irq)c.post=state(f);else{c.post.ns=num(f,8);c.post.sp=num(f,4);c.post.sr=num(f,2);regs(f,c.post);c.post.usp=num(f,4);c.post.ssp=num(f,4);c.post.cycle_bias=num(f,8);c.post.pc=num(f,4);}calls.push_back(c);}
  else if(kind==2||kind==4||kind==6){Signal s;s.ns=num(f,8);s.order=num(f,4);s.pc=num(f,4);s.a=num(f,4);s.d=num(f,2);s.m=num(f,2);s.main=kind==2||kind==6;s.read=kind==6;signals.push_back(s);if(kind==4||(s.pc==0xffffffff&&((s.a&0xfffff)==0xa3e00||(s.a&0xfffff)==0xa3e40)))frontier=true;}
  else if(kind==3){num(f,8);initial_ready=num(f,2);initial_mutex=num(f,2);if(!f.read(reinterpret_cast<char*>(initial_ram.data()),initial_ram.size()))throw std::runtime_error("missing initial sub RAM");initial=true;}
  else if(kind==5){++cuts;frontier=true;}
  else throw std::runtime_error("invalid sub progress record");
 }
 if(!initial||calls.empty()||!roms.load_aburner2())throw std::runtime_error("missing sub progress inputs/ROMs");
 std::stable_sort(signals.begin(),signals.end(),[](const auto& a,const auto& b){return a.order<b.order;});
 auto ram=initial_ram;std::array<uint8_t,SubBus::ROAD_BYTES> road{};
 SubBus bus(roms.subcpu.rom,roms.subcpu.length,ram.data(),road.data());bus.write16(0xa3c00,initial_ready);bus.write16(0x9c0d6,initial_mutex);
 std::array<uint8_t,SubBus::ROAD_BYTES> displayed_road{};unsigned swaps=0,controls=0;
 bus.road_swap=[&](){std::swap(road,displayed_road);++swaps;};
 bus.road_control=[&](uint8_t){++controls;};
 OSubIrq port;size_t si=0;unsigned failures=0,phase_bad=0,phases=0,chains=0,chain_bad=0,frontiers=0,computed=0,external=0;
 std::map<uint32_t,unsigned> coverage,producers,badpc,chains_by_pc;std::set<uint32_t> diagnosed;std::map<uint32_t,std::set<unsigned>> operands;std::map<uint32_t,std::array<unsigned,2>> branches;
 std::map<uint32_t,uint16_t> main_operands;unsigned main_reads=0,main_bad=0,delivery_leads=0;
 constexpr unsigned supplied_operands=0; // all shared bytes come from the initial input image

 auto canonical=[](uint32_t a){a&=0xfffff;return a>=0x80000&&a<0xc0000?0x9c000+(a<0xa0000?0:0x4000)+(a&0x3fff):a;};
 uint32_t phase_order=0;uint64_t phase_time=0;
 auto clock=[&](uint64_t){while(si<signals.size()&&signals[si].order<phase_order){const auto& s=signals[si++];const auto a=canonical(s.a);
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
 XBoardSubIrqSelection delivery;State interrupted{};bool in_irq=false;unsigned resumptions=0,resumption_bad=0;State last{},expected_last{};bool have=false,last_tail=false;unsigned selections=0,selection_bad=0;unsigned pending=0;uint32_t last_executed=0;uint64_t completed_bus=0,poll_time=0,poll_bus=0;unsigned tail_chains=0,tail_frontiers=0;std::set<uint32_t> tail_frontier_pcs;
 for(size_t call_index=0;call_index<calls.size();++call_index){auto c=calls[call_index];
  const auto* next_call=call_index+1<calls.size()?&calls[call_index+1]:nullptr;
  const bool adjacent=next_call&&!next_call->frontier&&c.post.pc==next_call->pre.pc;
  ++coverage[c.irq?0xffffffff:c.pre.pc];bool ok=true;
  if(have&&last_tail){if(expected_last.ns==c.pre.ns)++tail_chains;else{++tail_frontiers;tail_frontier_pcs.insert(last.pc);}}
  if(have&&!c.frontier&&expected_last.pc==c.pre.pc){++selections;if(bool(pending)!=c.irq){++selection_bad;if(selection_bad<10)std::cerr<<"Sub IRQ selection mismatch next="<<std::hex<<c.pre.pc<<" previous="<<last_executed<<std::dec<<" pending="<<pending<<" entry="<<c.irq<<" busend="<<completed_bus<<" poll="<<poll_time<<" pollbus="<<poll_bus<<" t="<<c.pre.ns<<"\n";}++chains;if(!same(last,c.pre)){++chain_bad;++chains_by_pc[c.pre.pc];ok=false;}c.pre=last;}else{++frontiers;delivery.resume(c.pre.ns);}
  size_t cursor=0;
  auto advance=[&](uint64_t t){phase_order=cursor<c.events.size()?c.events[cursor].order:c.events.back().order+1;phase_time=t;clock(t);};
  auto observe=[&](bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t t){
   completed_bus=t+320;delivery.complete_bus(t);
   ++phases;bool equal=cursor<c.events.size();
   if(equal){const auto& e=c.events[cursor++];equal=e.ns==t&&e.a==a&&e.w==w&&e.m==m&&(e.d&m)==(d&m);}
   if(!equal){ok=false;++phase_bad;if(phase_bad<8)std::cerr<<"Sub progress phase pc="<<std::hex<<pc<<" a="<<a<<" d="<<d<<std::dec<<" t="<<t<<'\n';}
   if(!w&&(a==0xa3c00||a==0x9c300||a==0x9c302||pc==0x570))operands[pc].insert(pc==0x570?d&255:d);
  };
  State result;
  if(c.irq){if(in_irq)throw std::runtime_error("nested sub IRQ at masked IPL4");interrupted=c.pre;in_irq=true;result=port.enter(bus,c.pre,c.buspc,advance,observe);pending=0;}
  else{auto instruction=port.instruction(bus,c.pre,advance,observe,[&](uint64_t instruction_poll){
    poll_bus=completed_bus;return delivery.level(instruction_poll);
   });result=instruction.state;pending=instruction.irq;poll_time=instruction.poll_ns;}

  if(!c.irq&&c.pre.pc==0x592){if(!in_irq)throw std::runtime_error("sub RTE without retained interrupted state");++resumptions;bool restored=result.pc==interrupted.pc&&result.sp==interrupted.sp&&result.sr==interrupted.sr&&result.d==interrupted.d&&result.a==interrupted.a&&result.ssp==interrupted.ssp;if(!restored){++resumption_bad;ok=false;}in_irq=false;}
  auto m=sub_instruction(c.pre.pc);bool tail=!c.irq&&m&&(m->tail||m->variable);
  if(tail&&adjacent)c.post=next_call->pre;
  ok&=cursor==c.events.size()&&(tail&&!adjacent?(result.ns==c.post.ns&&result.pc==c.post.pc&&result.sp==c.post.sp):same(result,c.post));if(!ok){++failures;++badpc[c.pre.pc];if(diagnosed.insert(c.pre.pc).second)std::cerr<<"Sub progress record pc="<<std::hex<<c.pre.pc<<" next="<<result.pc<<'/'<<c.post.pc<<" sr="<<result.sr<<'/'<<c.post.sr<<std::dec<<" end="<<result.ns<<'/'<<c.post.ns<<'\n';}
  if(!c.irq&&m&&m->branch){const auto fall=c.pre.pc+(m->branch==-2?4:m->branch);++branches[c.pre.pc][result.pc!=fall];}
  if(!c.irq&&(c.pre.pc==0x428||c.pre.pc==0x56e||c.pre.pc==0x574||c.pre.pc==0x57c||c.pre.pc==0x58a)){auto len=c.pre.pc==0x574||c.pre.pc==0x57c||c.pre.pc==0x58a?4:2;++branches[c.pre.pc][result.pc!=c.pre.pc+len];}
  if(c.pre.pc==0x43a||c.pre.pc==0x446||c.pre.pc==0x456||c.pre.pc==0x55a||c.pre.pc==0x646)++branches[c.pre.pc][result.pc!=c.pre.pc+((c.pre.pc==0x43a||c.pre.pc==0x446)?4:2)];
  if(!ok&&badpc[c.pre.pc]==1){for(unsigned j=0;j<8;++j)if(result.d[j]!=c.post.d[j])std::cerr<<" D"<<j<<"="<<std::hex<<result.d[j]<<'/'<<c.post.d[j]<<std::dec;for(unsigned j=0;j<7;++j)if(result.a[j]!=c.post.a[j])std::cerr<<" A"<<j<<"="<<std::hex<<result.a[j]<<'/'<<c.post.a[j]<<std::dec;std::cerr<<" SP="<<std::hex<<result.sp<<'/'<<c.post.sp<<std::dec<<'\n';}
  delivery.complete_instruction(result.ns);completed_bus=result.ns; // completed internal tail is also an execution yield
  last_executed=c.pre.pc;last=result;expected_last=c.post;have=true;last_tail=tail;
 }
 std::cout<<"Main signal reads/writes: "<<main_reads+computed-main_bad<<" of "<<main_reads+computed<<" identical, "<<main_bad<<" different, 0 skipped\n";
 std::cout<<"Sub progress instructions: "<<calls.size()-std::min<size_t>(failures,calls.size())<<" of "<<calls.size()<<" identical, "<<failures<<" different, 0 skipped\nSub progress bus phases: "<<phases-phase_bad<<" of "<<phases<<" identical, "<<phase_bad<<" different, 0 skipped\nSub progress chains: "<<chains-chain_bad<<" of "<<chains<<" identical, "<<chain_bad<<" different, 0 skipped\nSignal producers: "<<computed<<" computed, "<<external<<" supplied writes; "<<main_reads<<" checked main RMW reads, "<<delivery_leads<<" supplied cross-CPU delivery leads; "<<frontiers<<" supplied sub entry/IRQ/callee frontiers; "<<supplied_operands<<" supplied operand words; "<<cuts<<" excluded partial IRQ/fixture cuts\n";
 for(uint32_t pc:{0x432,0x436,0x43a,0x43e,0x442,0x446,0x44a,0x44e,0x452,0x456,0x458,0x556,0x55a,0x55c,0x640,0x646}){std::cout<<std::hex<<pc<<std::dec<<": "<<coverage[pc]<<" instructions\n";if(!coverage[pc])throw std::runtime_error("missing sub progress PC");}
 for(auto [p,n]:badpc)std::cout<<"Mismatch PC "<<std::hex<<p<<std::dec<<": "<<n<<" records, "<<chains_by_pc[p]<<" incoming chains\n";
 std::cout<<"Completion-tail state assertions: "<<tail_chains<<" chained, "<<tail_frontiers<<" supplied next boundaries; returned PCs:";
 for(auto pc:tail_frontier_pcs)std::cout<<' '<<std::hex<<pc<<std::dec;std::cout<<'\n';
 std::cout<<"Road endpoints: "<<swaps<<" computed swaps, "<<controls<<" computed control writes\n";
 unsigned seen=0;for(const auto& i:sub_instructions)if(coverage[i.pc])++seen;
 std::cout<<"Timed sprite/road PC coverage: "<<seen<<" of "<<sub_instructions.size()<<"; unexercised PCs:";
 for(const auto& i:sub_instructions)if(!coverage[i.pc])std::cout<<' '<<std::hex<<i.pc<<std::dec;std::cout<<'\n';
 for(const auto& i:sub_instructions)if(i.branch&&coverage[i.pc]){
  auto n=branches[i.pc];std::cout<<"Graph branch "<<std::hex<<i.pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';}
 for(auto [p,n]:branches){std::cout<<"Branch "<<std::hex<<p<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';if((p==0x43a||p==0x446||p==0x456||p==0x55a||p==0x646)&&(!n[0]||!n[1]))throw std::runtime_error("missing sub progress branch direction");}
 for(uint32_t p:{0x14674,0x14798,0x1467c,0x1486e})if(!producers[p])throw std::runtime_error("missing main signal producer");
 if(directed)for(uint32_t p:{0x452,0x556,0x436,0x442})for(unsigned v:{0,1,0x7fff,0x8000,0xffff,0xfffe})if(!operands[p].count(v))throw std::runtime_error("missing sub progress endpoint at "+std::to_string(p)+" value "+std::to_string(v));
 if(directed&&seen!=sub_instructions.size())throw std::runtime_error("missing timed sprite/road PC coverage");
 for(uint32_t p:{0x420,0x428,0x55e,0x562,0x568,0x56e,0x570,0x574,0x578,0x57c,0x580,0x586,0x58a,0x58e,0x592}){std::cout<<"Sub IRQ/boot PC "<<std::hex<<p<<std::dec<<": "<<coverage[p]<<"\n";if(!coverage[p])throw std::runtime_error("missing sub ISR/boot PC");}
 if(directed){for(uint32_t p:{0x56e,0x574,0x57c,0x58a})if(!branches[p][0]||!branches[p][1])throw std::runtime_error("missing sub ISR branch direction");for(unsigned v:{0,1,2,0x7f,0x80,0xff})if(!operands[0x570].count(v))throw std::runtime_error("missing sub ISR countdown endpoint");}
 if(supplied_operands)throw std::runtime_error("unknown operand bytes supplied by assertions");
 std::cout<<"Sub IRQ entry records: "<<coverage[0xffffffff]<<"; RTE records: "<<coverage[0x592]<<"\n";
 std::cout<<"Sub IRQ selection: "<<selections-selection_bad<<" of "<<selections<<" identical, "<<selection_bad<<" different, 0 skipped\n";
 std::cout<<"Sub IRQ resumptions: "<<resumptions-resumption_bad<<" of "<<resumptions<<" identical, "<<resumption_bad<<" different, 0 skipped; "<<in_irq<<" service active at capture stop\n";
 return failures||phase_bad||chain_bad||main_bad||selection_bad||resumption_bad?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

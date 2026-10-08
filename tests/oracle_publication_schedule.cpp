#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <sstream>
#include <memory>
#include <cstring>
#include "hwmath/xboard_bus_arbiter.hpp"
#include "engine/mainbus.hpp"
#include "engine/omainirqservice.hpp"
#include "engine/osubpublication.hpp"
#include "hwmath/xboard_cpu_continuation.hpp"
#include "roms.hpp"
namespace {
uint64_t number(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){auto c=f.get();if(c<0)throw std::runtime_error("truncated arbiter capture");v=v<<8|c;}return v;}
unsigned cpu(const std::string& tag){if(tag==":mainpcb:maincpu")return 0;if(tag==":mainpcb:subcpu")return 1;if(tag==":mainpcb:soundcpu")return 2;throw std::runtime_error("unexpected grant CPU");}
struct Snapshot {uint32_t pc{},sp{};uint16_t sr{};OMainObjects::Registers registers;bool present=false;};
struct Phase{unsigned cpu;uint64_t ns;bool write;uint32_t a;uint16_t d,m;uint32_t pc;Snapshot snapshot;};
bool bounded(unsigned cpu,uint32_t pc){
 if(cpu==0){switch(pc){case 0x14856:case 0x1485c:case 0x1485e:case 0x14866:case 0x1486a:case 0x1486e:case 0x14876:case 0x14878:return true;}}
 if(cpu==1){switch(pc){case 0x432:case 0x436:case 0x43a:case 0x43e:case 0x442:case 0x446:case 0x44a:case 0x44e:case 0x452:case 0x456:case 0x458:case 0x556:case 0x55a:case 0x55c:case 0x640:case 0x646:return true;}}
 return false;
}
unsigned offset(const Phase& p){
 // First bus follows only the instruction's ROM-defined internal clocks.
 if(p.cpu==0&&(p.pc==0x1485c||p.pc==0x14866))return p.a==p.pc+4?4:2;
 if(p.cpu==1){switch(p.pc){case 0x432:case 0x43e:case 0x44a:case 0x44e:return 2;
 case 0x43a:case 0x446:case 0x456:case 0x55a:case 0x646:return p.a==p.pc+4?4:2;}}
 return 0;
}
struct Boost{uint64_t at,quantum,duration;};
struct Run{uint64_t start{},target{};unsigned budget{},ran{};bool present=false;std::vector<Boost> boosts;};
struct Group{uint64_t index{},base{},deadline{},quantum{},end{};bool outer=false;std::array<XBoardBusArbiter::Time,3> starts{};std::array<bool,3> enabled{};std::array<Run,3> runs;std::vector<Boost> before;};
}
int main(int argc,char** argv)try{
 std::ifstream f(argc>1?argv[1]:"build/oracle/publication_schedule.bin",std::ios::binary);char magic[8];if(!f.read(magic,8)||memcmp(magic,"AB2ARB04",8))throw std::runtime_error("invalid arbiter capture");
 if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
 MainBus::Memory memory;MainBus mainbus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);auto& bus=mainbus.sub;
 if(!f.read(reinterpret_cast<char*>(memory.bytes.data()),0x10000))throw std::runtime_error("missing initial work/shared RAM");
 unsigned reads=0,read_bad=0,writes=0,shared_leads=0,external_writes=0,native_shared_writes=0;std::array<uint64_t,SubBus::SHARED_BYTES> main_write_ns{};
 XBoardCpuContinuation maincpu,subcpu;std::array<XBoardCpuContinuation*,2> native{&maincpu,&subcpu};
 std::array<bool,2> running{},linked{};std::array<OMainIrqService::Result,2> result{};
 OMainIrqService mainport;OSubPublication subport;
 unsigned instructions=0,native_phases=0,native_writes=0,contexts=0,chains=0,state_bad=0,split_grants=0,bounded_returns=0,return_bad=0;
 std::map<std::pair<unsigned,uint32_t>,unsigned> coverage;
 std::function<void(unsigned,bool,uint32_t,uint16_t,uint16_t,uint32_t,uint64_t)> observe;
 MainBus::Trace maintrace;mainbus.trace=&maintrace;
 uint64_t main_phase_ns=0;
 maintrace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m){observe(0,w,a,d,m,mainport.current_pc(),main_phase_ns);};
 auto begin=[&](const Phase& p){
  const auto& snap=p.snapshot;if(!snap.present||snap.pc!=p.pc)throw std::runtime_error("missing native entry snapshot");
  auto state=OMainIrqService::Result{snap.registers,snap.pc,snap.sp,snap.sr,p.ns-offset(p)*80};
  // All CPU phases are captured. Only an actual phase from unported work
  // cuts continuity; disagreement with predicted time/PC cannot create a cut.
  if(linked[p.cpu]){
   ++chains;auto& r=result[p.cpu];if(r.ns!=state.ns||r.pc!=state.pc||r.sp!=state.sp||r.sr!=state.sr||r.registers.d!=state.registers.d||r.registers.a!=state.registers.a){++state_bad;std::cerr<<"Retained state CPU="<<p.cpu<<" pc="<<std::hex<<p.pc<<" sr="<<r.sr<<'/'<<state.sr<<" sp="<<r.sp<<'/'<<state.sp<<std::dec<<" ns="<<state.ns<<'\n';for(unsigned i=0;i<8;++i)if(r.registers.d[i]!=state.registers.d[i])std::cerr<<" D"<<i<<"="<<std::hex<<r.registers.d[i]<<'/'<<state.registers.d[i]<<std::dec<<'\n';for(unsigned i=0;i<7;++i)if(r.registers.a[i]!=state.registers.a[i])std::cerr<<" A"<<i<<"="<<std::hex<<r.registers.a[i]<<'/'<<state.registers.a[i]<<std::dec<<'\n';throw std::runtime_error("retained native instruction state mismatch");}state=r;
  }else ++contexts;
  ++instructions;++coverage[{p.cpu,p.pc}];running[p.cpu]=true;
  if(p.cpu==0)maincpu.start(state.ns,[&,state]{
   OMainIrqService::Context c{};c.registers=state.registers;c.boundary_ns=state.ns;c.interrupted_pc=state.pc;c.supervisor_sp=state.sp;c.sr=state.sr;
   result[0]=mainport.caller_step(mainbus,c,[&](uint64_t t){maincpu.clock(t);main_phase_ns=t;},[](uint64_t){return 0;}).state;maincpu.instruction_completed(result[0].ns);
  });
  else subcpu.start(state.ns,[&,state]{
   OSubPublication::State c{};c.d=state.registers.d;c.a=state.registers.a;c.pc=state.pc;c.sp=state.sp;c.sr=state.sr;c.ns=state.ns;
   auto end=subport.step(bus,c,[&](uint64_t t){subcpu.clock(t);},[&](bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t t){observe(1,w,a,d,m,pc,t);});
   result[1]={{end.d,end.a},end.pc,end.sp,end.sr,end.ns};subcpu.instruction_completed(end.ns);
  });
 };
 std::map<uint64_t,std::vector<Phase>> phases_by_base;
 while(f.peek()!=EOF){auto index=number(f,8);number(f,8);auto count=number(f,4);auto& phases=phases_by_base[index];while(count--){Phase p;p.cpu=number(f,1)-1;p.ns=number(f,8);p.write=number(f,1);p.a=number(f,4);p.d=number(f,2);p.m=number(f,2);p.pc=number(f,4);p.snapshot.present=number(f,1);if(p.snapshot.present){auto& s=p.snapshot;s.pc=number(f,4);s.sr=number(f,2);s.sp=number(f,4);for(auto& v:s.registers.d)v=number(f,4);for(auto& v:s.registers.a)v=number(f,4);}phases.push_back(p);}}
 if(phases_by_base.empty())throw std::runtime_error("empty bus capture");
 const auto last_capture_group=phases_by_base.rbegin()->first;
 std::ifstream trace(argc>2?argv[2]:"build/oracle/publication_schedule.txt");if(!trace)throw std::runtime_error("missing complete scheduler trace");
 std::unique_ptr<XBoardBusArbiter> port;Group g;bool active=false,outer=false;unsigned current=0;
 std::vector<Boost> pending;unsigned failures=0,phase_bad=0,budget_bad=0,grant_bad=0,groups=0,grants=0,phases=0,leads=0,boosts=0,empty=0;unsigned cpu_masks=0;
 auto boost=[&](const Boost& b){if(b.quantum==XBoardBusArbiter::DEFAULT_QUANTUM)return;if(b.quantum!=320000000000ULL||b.duration!=100000000000000ULL)throw std::runtime_error("unexpected board quantum request");port->boost(b.at);++boosts;};
 auto finish=[&](){
  if(!port)port=std::make_unique<XBoardBusArbiter>(XBoardBusArbiter::State{g.base,0,g.starts});
  for(auto b:g.before)boost(b);if(g.outer)port->timers_delivered();
  bool ok=port->state().base==g.base&&port->state().local==g.starts&&port->quantum()==g.quantum;
  // Quantum values are assertions only; expiry/renewal are native.
  auto it=phases_by_base.find(g.index);std::vector<Phase> none;auto& events=it==phases_by_base.end()?none:it->second;size_t cursor=0;uint64_t latest_main=0;
  if(events.empty())++empty;
  auto result=port->grant(g.deadline,[&](unsigned c,XBoardBusArbiter::Time target,unsigned budget){++grants;auto& r=g.runs[c];
   if(!r.present||target!=r.target||budget!=r.budget){++budget_bad;ok=false;if(budget_bad<5)std::cerr<<"Budget CPU="<<c<<" base="<<g.base<<" target="<<uint64_t(target)<<'/'<<r.target<<" cycles="<<budget<<'/'<<r.budget<<'\n';}
   r.present=false;for(auto b:r.boosts)boost(b);
   auto shared_check=[&](const Phase& p,bool apply){
    ++phases;if(c==0)latest_main=p.ns;else if(latest_main>p.ns)++leads;
    const bool shared=p.cpu==0?(p.a>=0x280000&&p.a<0x2c0000):(p.a>=0x80000&&p.a<0xc0000);
    if(shared){auto a=p.a&0xfffff;auto offset=(a<0xa0000?0:0x4000)+(a&0x3fff);
     if(p.write){++writes;if(apply)++external_writes;else ++native_shared_writes;for(unsigned lane=0;lane<2;++lane)if(p.m&(lane?255:0xff00))main_write_ns[offset+lane]=p.cpu==0?p.ns:0;}
     else{++reads;if(p.cpu==1&&(((p.m&0xff00)&&main_write_ns[offset]>p.ns)||((p.m&255)&&main_write_ns[offset+1]>p.ns)))++shared_leads;auto value=bus.read16(a);if((value&p.m)!=(p.d&p.m)){++read_bad;ok=false;if(read_bad<5)std::cerr<<"Arbitrated shared read CPU="<<p.cpu<<" a="<<std::hex<<a<<" value="<<value<<'/'<<p.d<<std::dec<<" ns="<<p.ns<<'\n';}}}
    if(apply&&p.write){
     if(p.cpu==0){auto o=MainBus::memory_offset(p.a);if(o!=MainBus::UNMAPPED){if(p.m&0xff00)memory.bytes[o]=p.d>>8;if(p.m&255)memory.bytes[o+1]=p.d;}}
     else if(shared){auto a=p.a&0xfffff;auto old=bus.read16(a);bus.write16(a,(old&~p.m)|(p.d&p.m));}
    }
   };
   observe=[&](unsigned cpu,bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t t){
    ++native_phases;if(w)++native_writes;native[cpu]->bus_completed(t);
    if(cursor>=events.size()||events[cursor].cpu!=cpu)throw std::runtime_error("native phase outside granted CPU events");
    auto p=events[cursor++];
    if(p.ns!=t||p.pc!=pc||p.a!=a||p.write!=w||p.m!=m||(p.d&m)!=(d&m)){
     std::cerr<<"Native phase cpu="<<cpu<<" pc="<<std::hex<<pc<<" a="<<a<<'/'<<p.a<<" d="<<d<<'/'<<p.d<<std::dec<<" ns="<<t<<'/'<<p.ns<<" grant="<<g.index<<'\n';
     throw std::runtime_error("native publication phase mismatch");}
    shared_check(p,false);
   };
   const auto start_ns=uint64_t(port->state().local[c])/1000000000ULL;
   const auto target_ns=start_ns+uint64_t(budget)*(c==2?250:80);
   uint64_t end_ns=start_ns;bool all_native=c<2&&running[c]&&g.index<=last_capture_group,any_native=false;
   if(c<2&&running[c])++split_grants;
   while((cursor<events.size()&&events[cursor].cpu==c)||(c<2&&running[c]&&g.index<=last_capture_group)){
    if(c<2&&running[c]){
     any_native=true;end_ns=native[c]->advance(target_ns);
     if(!native[c]->done())break;
     running[c]=false;linked[c]=true;
     if(end_ns>=target_ns)break;
    }else if(cursor<events.size()&&events[cursor].cpu==c){
     const auto p=events[cursor];
     if(c<2&&bounded(c,p.pc)&&p.snapshot.present&&(c==0||!(p.snapshot.sr&0x2000))){begin(p);any_native=true;}
     else{all_native=false;if(c<2)linked[c]=false;shared_check(p,true);++cursor;end_ns=p.ns+320;}
    }else break;
   }
   const auto cycle=c==2?XBoardBusArbiter::SOUND_CYCLE:XBoardBusArbiter::CYCLE;
   auto supplied_end=port->state().local[c]+r.ran*cycle;
   // Only a grant wholly inside the retained bounded graph claims a native
   // completion. Other CPU work/zero-progress exits remain labelled inputs.
   if(all_native&&any_native){++bounded_returns;
    if(end_ns*1000000000ULL!=supplied_end){++return_bad;std::cerr<<"Native grant completion "<<g.index<<" CPU="<<c<<" ns="<<end_ns<<'/'<<uint64_t(supplied_end)/1000000000ULL<<'\n';}
   }
   return all_native&&any_native?XBoardBusArbiter::Time(end_ns)*1000000000ULL:supplied_end;
  },g.enabled);
  for(auto r:g.runs)if(r.present){++grant_bad;ok=false;}
  if(cursor!=events.size()){phase_bad+=events.size()-cursor;ok=false;}
  ok&=result==g.end;
  if(!ok){++failures;if(failures<8)std::cerr<<"Arbiter base="<<g.base<<" next="<<uint64_t(result)<<'/'<<g.end<<" phases="<<cursor<<'/'<<events.size()<<'\n';}
  if(it!=phases_by_base.end())phases_by_base.erase(it);
  ++groups;
 };
 std::string line;
 while(std::getline(trace,line)){std::istringstream in(line);char kind;in>>kind;
  if(kind=='T'){if(active)throw std::runtime_error("timer delivery inside grant");outer=true;}
  else if(kind=='G'){if(active)throw std::runtime_error("unterminated grant");g={};in>>g.index>>g.base>>g.deadline>>g.quantum;if(g.index!=groups+1)throw std::runtime_error("discontinuous source grant index");g.before=std::move(pending);pending.clear();g.outer=outer;outer=false;active=true;}
  else if(kind=='C'){std::string tag;uint64_t start;unsigned suspend,eat;in>>tag>>start>>suspend>>eat;current=cpu(tag);g.starts[current]=start;g.enabled[current]=!suspend||eat;++cpu_masks;}
  else if(kind=='Q'){Boost b;in>>b.at>>b.quantum>>b.duration;if(active)g.runs[current].boosts.push_back(b);else pending.push_back(b);}
  else if(kind=='R'){std::string tag;in>>tag;auto& r=g.runs[cpu(tag)];in>>r.start>>r.target>>r.budget>>r.ran;r.present=true;}
  else if(kind=='E'){in>>g.end;finish();active=false;}
  else throw std::runtime_error("invalid scheduler trace record");
  if(!in)throw std::runtime_error("invalid scheduler trace fields");
 }
 if(active||cpu_masks!=3*groups||!phases_by_base.empty())throw std::runtime_error("incomplete grant or bus capture");
 std::cout<<"CPU grant groups: "<<groups-failures<<" of "<<groups<<" identical, "<<failures<<" different, 0 skipped; "<<grants<<" CPU grants, "<<empty<<" groups without captured program phases\nMain/sub bus arbitration: "<<phases<<" phases in computed CPU order, "<<phase_bad<<" undelivered; "<<leads<<" cross-CPU local-clock leads; "<<boosts<<" computed quantum requests; "<<budget_bad<<" budget differences, "<<grant_bad<<" missing grants\n";
 std::cout<<"Arbitrated shared reads: "<<reads-read_bad<<" of "<<reads<<" identical, "<<read_bad<<" different, 0 skipped; "<<native_shared_writes<<" computed and "<<external_writes<<" supplied shared writes, "<<shared_leads<<" reads after a main write with a later local timestamp, 0 read assertions seeded\n";
 // Unexercised joint paths are the missed-count ADDQ and optional normal
 // sprite BSR ($1486A/$43E); their independent directed oracles remain separate.
 for(unsigned c=0;c<2;++c)for(uint32_t pc=0;pc<0x14900;pc+=2)
  if(bounded(c,pc)&&pc!=0x1486a&&pc!=0x43e&&!coverage[{c,pc}])throw std::runtime_error("missing joint publication PC");
 std::cout<<"Capture stop: "<<unsigned(running[0])+unsigned(running[1])<<" native continuations parked outside accepted final grant; all later CPU completions supplied\n";
 for(auto [key,n]:coverage)std::cout<<"Native publication PC CPU="<<key.first<<" pc="<<std::hex<<key.second<<std::dec<<": "<<n<<'\n';
 std::cout<<"Joint native publication: "<<instructions<<" instructions, "<<native_phases<<" phases, "<<native_writes<<" writes computed; "<<chains<<" retained instruction contexts, "<<contexts<<" supplied entry contexts; "<<split_grants<<" grants resume an active instruction; "<<bounded_returns-return_bad<<" of "<<bounded_returns<<" bounded grant completions identical, "<<return_bad<<" different, 0 skipped\n";
 return failures||phase_bad||budget_bad||grant_bad||read_bad||state_bad||return_bad||!bounded_returns?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

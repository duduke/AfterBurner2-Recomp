#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include "engine/omaincallerschedule.hpp"
#include "roms.hpp"
#include "cpu_visibility_capture.hpp"
namespace {
uint64_t num(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated caller capture");v=v<<8|c;}return v;}
OMainObjects::Registers regs(std::ifstream& f){OMainObjects::Registers r;for(auto& v:r.d)v=num(f,4);for(auto& v:r.a)v=num(f,4);return r;}
struct Event {uint64_t ns;uint32_t pc,a;uint16_t d,m;unsigned w,level;};
Event event(std::ifstream& f){Event e;e.ns=num(f,8);e.pc=num(f,4);e.a=num(f,4);e.d=num(f,2);e.m=num(f,2);e.w=num(f,1);e.level=num(f,1);return e;}
std::vector<Event> events(std::ifstream& f){auto n=num(f,4);if(n>200)throw std::runtime_error("invalid caller event count");std::vector<Event> v;while(n--)v.push_back(event(f));return v;}

using ReturnKey=std::pair<uint32_t,uint64_t>;
std::map<ReturnKey,OMainIrqService::Result> return_boundaries(const char* path,bool successors,bool closure=false){
 std::ifstream f(path,std::ios::binary);char magic[8];
 if(!f.read(magic,8))throw std::runtime_error("invalid return baseline version");
 const bool valid=closure?(!memcmp(magic,"AB2CALL7",8)||!memcmp(magic,"AB2CALL8",8)||!memcmp(magic,"AB2CALL9",8)):
     !memcmp(magic,successors?"AB2CALL6":"AB2CALL5",8);
 if(!valid)throw std::runtime_error("invalid return baseline version");
 auto skip=[&](size_t n){f.ignore(n);if(size_t(f.gcount())!=n)throw std::runtime_error("truncated return baseline");};
 auto skip_events=[&](){auto n=num(f,4);if(n>200)throw std::runtime_error("invalid baseline event count");skip(n*22);};
 std::map<ReturnKey,OMainIrqService::Result> result;
 OMainIrqService::Result last{};
 while(f.peek()!=std::char_traits<char>::eof()){
  switch(num(f,1)){
  case 1:skip(4+8+4+2+60);skip_events();skip(8+4+2+60+4+1);break;
  case 2:skip(4+8+4+4+2+1+60+8);skip_events();skip(4+2);break;
  case 3:case 7:break;
  case 4:
   skip(0x8000);skip_events();last.ns=num(f,8);last.sp=num(f,4);last.sr=num(f,2);last.pc=num(f,4);last.registers=regs(f);skip(0x8000);break;
  case 5:
   if(!closure&&last.pc!=(successors?0x8afe:0x8af8)&&last.pc!=(successors?0xb632:0xb62e))throw std::runtime_error("unexpected returned PC in baseline");
   if(!result.emplace(ReturnKey{last.pc,last.ns},last).second)throw std::runtime_error("duplicate returned boundary");
   break;
  case 6:skip(1);break;
  default:throw std::runtime_error("invalid return baseline record");
  }
 }
 if(result.empty())throw std::runtime_error("empty return baseline");
 return result;
}
}
int main(int argc,char** argv)try{
 std::ifstream f(argc>1?argv[1]:"build/oracle/caller.bin",std::ios::binary);char magic[8];
 if(!f.read(magic,8)||(memcmp(magic,"AB2CALL1",8)&&memcmp(magic,"AB2CALL2",8)&&memcmp(magic,"AB2CALL3",8)&&memcmp(magic,"AB2CALL4",8)&&memcmp(magic,"AB2CALL5",8)&&memcmp(magic,"AB2CALL6",8)&&memcmp(magic,"AB2CALL7",8)&&memcmp(magic,"AB2CALL8",8)&&memcmp(magic,"AB2CALL9",8)))throw std::runtime_error("invalid caller capture");
 const bool snapshots=!memcmp(magic,"AB2CALL9",8);
 const bool closure=snapshots||!memcmp(magic,"AB2CALL8",8);
 const bool successors=closure||!memcmp(magic,"AB2CALL7",8);
 bool require_taken=false,require_closed=false;const char* baseline_path=nullptr;
 for(int i=2;i<argc;++i){
  const std::string arg=argv[i];
  if(arg=="--require-taken")require_taken=true;
  else if(arg=="--require-closed")require_closed=true;
  else if(!baseline_path)baseline_path=argv[i];
  else throw std::runtime_error("unexpected caller argument");
 }
 if(require_taken&&!successors)throw std::runtime_error("taken-branch fixture requires CALL7+");
 if(require_closed&&!snapshots)throw std::runtime_error("closed return graph requires instruction snapshots");
 auto baseline=baseline_path?return_boundaries(baseline_path,successors,closure):std::map<ReturnKey,OMainIrqService::Result>{};
 const auto baseline_count=baseline.size();unsigned baseline_passed=0,baseline_bad=0;
 if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
 MainBus::Memory memory;MainBus bus(roms.maincpu.rom,roms.maincpu.length,nullptr,0,memory);
 CpuVisibilityCapture visibility((std::string(argc>1?argv[1]:"build/oracle/caller.bin")+".visibility").c_str());
 visibility.report();
 if(visibility.failed)throw std::runtime_error("physical visibility mismatch");
 OMainCallerSchedule scheduled;OMainIrqService::Instruction last{};bool have=false,expected_pending=false;
 unsigned calls=0,entries=0,failed=0,total=0,bad=0,chains=0,chain_bad=0,polls=0,masked=0,computed_frontiers=0,frontier_bad=0,levels[7]={};
 std::map<uint32_t,unsigned> coverage,irqcoverage,external_return_pcs;
 std::map<uint32_t,uint8_t> owned;
 uint64_t prior_phase_end=0;
 const bool returned_words=successors||!memcmp(magic,"AB2CALL6",8);
 if(baseline_path&&!returned_words)throw std::runtime_error("return baseline requires CALL6/7");
 unsigned internal_polls=0,internal_poll_bad=0;
 unsigned branch_taken=0,branch_untaken=0;bool branch_nv[4]{},sub_flags[32]{};
 std::set<uint16_t> sub_inputs;
 std::set<uint16_t> word_inputs[2];unsigned word_x[2][2]{};bool word_flags[2][16]{};
 const bool returned_steps=returned_words||!memcmp(magic,"AB2CALL5",8);
 const bool returns=returned_steps||!memcmp(magic,"AB2CALL4",8);
 const bool linked=returns||!memcmp(magic,"AB2CALL3",8);
 unsigned selections=0,selection_bad=0,back_to_back=0;
 bool service_expected=false,rte_entry_pending=false,selection_expected=false;
 unsigned services=0,service_bad=0,service_events=0,service_event_bad=0,resumptions=0,service2=0,service6=0,user_returns=0,adc_inputs=0,external_returns=0;
 unsigned graph_exits=0;
 unsigned autonomous=0,autonomous_events=0,autonomous_bad=0,cuts=0,resume_operands=0;
 bool wait_operand_known=false,service_return_pending=false;
 while(f.peek()!=std::char_traits<char>::eof()){
  auto kind=num(f,1);
  if(service_expected&&kind!=4)throw std::runtime_error("missing linked caller service");
  if(selection_expected&&kind!=6)throw std::runtime_error("missing RTE selection assertion");
  if(kind==6){
   if(!returns||!service_return_pending)throw std::runtime_error("RTE selection without service return");
   selection_expected=false;const unsigned irq=num(f,1);++selections;
   if(irq!=last.irq||last.poll_ns+320!=last.state.ns){++selection_bad;std::cerr<<"RTE selection mismatch "<<last.irq<<'/'<<irq<<" at "<<last.poll_ns<<'\n';}
   expected_pending=last.irq!=0;
   if(expected_pending){++back_to_back;service_return_pending=false;rte_entry_pending=true;}
   continue;
  }
  if(kind==4){
   if(!service_expected)throw std::runtime_error("service without computed caller entry");
   service_expected=false;
   // Caller entry is computed. Shared RAM still comes from external
   // producers; exception-frame lanes cannot be reseeded. ADC results come
   // from the continuous production physical timeline, never expected data.
   std::array<uint8_t,0x8000> before{},after{};
   if(!f.read(reinterpret_cast<char*>(before.data()),before.size()))throw std::runtime_error("truncated service RAM");
   const auto expected=events(f);const auto end=num(f,8),sp=num(f,4),sr=num(f,2),next=num(f,4);const auto post=regs(f);
   if(!f.read(reinterpret_cast<char*>(after.data()),after.size()))throw std::runtime_error("truncated service RAM");
   bool ok=true;const auto entered=scheduled.state().state;
   const auto frame=MainBus::memory_offset(entered.sp);
   for(size_t i=0;i<before.size();++i){
    if(frame!=MainBus::UNMAPPED&&i>=frame&&i<frame+6){
     if(memory.bytes[i]!=before[i]){ok=false;std::cerr<<"Service frame lane "<<std::hex<<i<<" got="<<unsigned(memory.bytes[i])<<" expected="<<unsigned(before[i])<<std::dec<<" end="<<end<<'\n';}
    }
    else memory.bytes[i]=before[i];
   }
   owned.clear();
   size_t cursor=0;uint64_t now=entered.ns;
   bus.endpoints.adc_read=[&](){
    const auto* phase=visibility.find(now);
    if(!phase||phase->address!=0x130000||phase->write)throw std::runtime_error("missing computed ADC producer phase");
    ++adc_inputs;return phase->adc;
   };
   MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m){
    ++service_events;bool same=cursor<expected.size();
    if(same){const auto& e=expected[cursor++];same=e.ns==now&&e.pc==scheduled.current_pc()&&e.a==a&&e.m==m&&e.w==unsigned(w)&&(e.d&m)==(d&m);}
    if(!same){++service_event_bad;ok=false;}
    prior_phase_end=now+4*OMainIrqService::CYCLE_NS;
    if(w){const auto o=MainBus::memory_offset(a);if(o!=MainBus::UNMAPPED){if(m&0xff00)owned[o]=d>>8;if(m&255)owned[o+1]=d;}}
   }};bus.trace=&trace;
   const auto result=scheduled.finish(bus,[&](uint64_t t){now=t;},returns?OMainIrqService::Poll([&](uint64_t t){
    const auto* phase=visibility.find(t);
    if(!phase)throw std::runtime_error("missing RTE physical selection phase");
    return phase->level;
   }):OMainIrqService::Poll{});
   bus.trace=nullptr;bus.endpoints.adc_read={};
   ok &= cursor==expected.size()&&result.ns==end&&result.sp==sp&&result.sr==sr&&result.pc==next&&result.registers.d==post.d&&result.registers.a==post.a;
   for(const auto& e:expected)if(e.w){const auto o=MainBus::memory_offset(e.a);if(o<after.size()-1){if(e.m&0xff00)ok &= memory.bytes[o]==after[o];if(e.m&255)ok &= memory.bytes[o+1]==after[o+1];
    if(((e.m&0xff00)&&memory.bytes[o]!=after[o])||((e.m&255)&&memory.bytes[o+1]!=after[o+1]))std::cerr<<"Service write lane "<<std::hex<<e.a<<" got="<<unsigned(memory.bytes[o])<<'/'<<unsigned(memory.bytes[o+1])<<" expected="<<unsigned(after[o])<<'/'<<unsigned(after[o+1])<<std::dec<<" end="<<end<<'\n';}}
   if(((entered.sr>>8)&7)==2)++service2;else ++service6;
   if(!(result.sr&0x2000))++user_returns;
   ++services;if(!ok){++service_bad;if(service_bad<6)std::cerr<<"Linked service mismatch end="<<result.ns<<'/'<<end<<" sp="<<std::hex<<result.sp<<'/'<<sp<<" sr="<<result.sr<<'/'<<sr<<" pc="<<result.pc<<'/'<<next<<" regs="<<(result.registers.d==post.d&&result.registers.a==post.a)<<std::dec<<'\n';}
   prior_phase_end=result.ns;
   last=scheduled.state();have=true;wait_operand_known=false;service_return_pending=true;selection_expected=returns;continue;
  }
  if(kind==7){if(!returned_steps)throw std::runtime_error("unexpected graph exit");++graph_exits;service_return_pending=false;have=false;owned.clear();wait_operand_known=false;prior_phase_end=0;continue;}
  if(kind==5){++external_returns;++external_return_pcs[last.state.pc];service_return_pending=false;have=false;owned.clear();wait_operand_known=false;prior_phase_end=0;continue;}
  if(kind==3){service_return_pending=false;have=false;owned.clear();wait_operand_known=false;prior_phase_end=0;++cuts;continue;}OMainIrqService::Context c{};c.interrupted_pc=num(f,4);c.boundary_ns=num(f,8);c.supervisor_sp=num(f,4);
  std::vector<Event> expected;OMainObjects::Registers post;uint64_t end;uint32_t sp,next;uint16_t sr;unsigned irq=0;
  bool ok=true,chain=false,baseline_return=false;
  if(kind==1){if(service_expected)throw std::runtime_error("unfinished linked caller service");
 if(expected_pending)throw std::runtime_error("missing caller IRQ entry");c.sr=num(f,2);c.registers=regs(f);expected=events(f);end=num(f,8);sp=num(f,4);sr=num(f,2);post=regs(f);next=num(f,4);irq=num(f,1);
   auto spin=[](uint32_t p){return p==0x14856||p==0x1485c||p==0xb676||p==0xb67c;};
   // Sampling omits ordinary busy spins, not interrupted instructions. Run
   // the bounded CPU from its own state until the next recorded assertion.
   // Unported post-RTE execution and directed fixture resets explicitly
   // cut this schedule; linked IRQ2/6 returns retain computed CPU state.
   if(have&&!last.irq&&spin(last.state.pc)&&spin(c.interrupted_pc)&&last.state.ns<c.boundary_ns){
    // Retain the same scheduler, including SSP/USP, through omitted spins.
    while(last.state.ns<c.boundary_ns){
     if(!spin(last.state.pc))throw std::runtime_error("wait left bounded graph inside sampling gap");
     uint64_t now=last.state.ns;bool frontier_ok=true;
     MainBus::Trace unseen{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m){
      ++autonomous_events;const auto* phase=visibility.find(now);
      if(!frontier_ok||!phase||phase->pc!=scheduled.current_pc()||phase->address!=a||phase->mask!=m||phase->write!=w||(phase->data&m)!=(d&m)){
       if(++autonomous_bad<10)std::cerr<<"Scheduled phase t="<<now<<" target="<<c.boundary_ns<<" pc="<<std::hex<<scheduled.current_pc()<<" a="<<a<<" d="<<(d&m)<<" expected="<<(phase?(phase->data&m):0)<<" captured pc="<<(phase?phase->pc:0)<<std::dec<<" frontier="<<frontier_ok<<'\n';
      }
      prior_phase_end=now+4*OMainIrqService::CYCLE_NS;
     }};
     unseen.before_access=[&](bool w,uint32_t a,uint16_t m){
      const auto* phase=visibility.find(now);
      frontier_ok=phase&&phase->previous_end==prior_phase_end;
      // An external ISR may return straight into BGE, before any retained
      // status read. Supply its first resumed operand once, then carry it.
      if(!w&&a==0x2a3c00&&!wait_operand_known&&phase&&phase->address==a&&phase->mask==m){
       const auto o=MainBus::memory_offset(a);memory.bytes[o]=phase->data>>8;memory.bytes[o+1]=phase->data;
       wait_operand_known=true;++resume_operands;
      }
     };
     bus.trace=&unseen;
     last=scheduled.step(bus,[&](uint64_t t){now=t;},[&](uint64_t t){
      const auto* phase=visibility.find(t);if(!phase)throw std::runtime_error("missing scheduled poll at "+std::to_string(t)+" target "+std::to_string(c.boundary_ns));return phase->level;
     });bus.trace=nullptr;++autonomous;
     if(last.irq)throw std::runtime_error("unrecorded IRQ inside sampling gap");
     if(last.state.ns>c.boundary_ns)throw std::runtime_error("scheduled wait overshot caller boundary");
    }
   }
   chain=have&&last.state.pc==c.interrupted_pc&&last.state.ns==c.boundary_ns&&!last.irq;
   auto baseline_it=baseline.find({c.interrupted_pc,c.boundary_ns});
   if(baseline_it!=baseline.end()){
    baseline_return=true;const auto& seed=baseline_it->second;
    // The old unported marker is an assertion of the computed RTE state,
    // never a context supplied to the new instruction or scheduler.
    ok &= chain&&service_return_pending&&last.state.sp==seed.sp&&last.state.sr==seed.sr&&
          last.state.registers.d==seed.registers.d&&last.state.registers.a==seed.registers.a;
    baseline.erase(baseline_it);
   }
   if(chain){++chains;if(service_return_pending){++resumptions;service_return_pending=false;}auto a=last.state.registers.a;auto d=last.state.registers.d;auto entry_sr=last.state.sr;
    // LEA absolute assigns before its first observable prefetch.
    if(c.interrupted_pc==0x1458a)a[6]=c.registers.a[6];
    if(c.interrupted_pc==0x1459c)a[5]=c.registers.a[5];
    if(!snapshots&&c.interrupted_pc==0x8b42)a[0]=0x33ed8;
    if(!snapshots&&c.interrupted_pc==0xb640)a[1]=c.registers.a[1];
    if(!snapshots&&(c.interrupted_pc==0x8b20||c.interrupted_pc==0x8b22||c.interrupted_pc==0x8b3e||c.interrupted_pc==0x8b4c)){
     const auto value=c.interrupted_pc==0x8b4c?uint16_t(int8_t(d[6])):uint16_t(d[c.interrupted_pc==0x8b3e?7:0]);
     const unsigned target=c.interrupted_pc==0x8b20?7:c.interrupted_pc==0x8b22?1:6;
     d[target]=(d[target]&0xffff0000)|value;entry_sr=(entry_sr&0xfff0)|(value?0:4)|(value&0x8000?8:0);
    }
    const bool same=((!snapshots&&(c.interrupted_pc==0xb636||c.interrupted_pc==0x8b06||c.interrupted_pc==0x8b24||c.interrupted_pc==0x8b2e||c.interrupted_pc==0x8b40||c.interrupted_pc==0x8b5e)) ? (entry_sr&0xfff0)==(c.sr&0xfff0) : entry_sr==c.sr)&&last.state.sp==c.supervisor_sp&&d==c.registers.d&&a==c.registers.a;
    if(!same){++chain_bad;ok=false;if(chain_bad<6)std::cerr<<std::hex<<"Caller chain pc="<<c.interrupted_pc<<" previous sr="<<last.state.sr<<" next sr="<<c.sr<<std::dec<<" start="<<c.boundary_ns<<'\n';}
    // Continue from the computed CPU state/time, after asserting the captured
    // boundary. Only unported calls/ISR intervals and fixture cuts reseed
    // context; linked IRQ2/6 returns and omitted waits carry computed state.
    c.registers=last.state.registers;c.sr=last.state.sr;
    c.supervisor_sp=last.state.sp;c.boundary_ns=last.state.ns;
   }else {owned.clear();wait_operand_known=false;service_return_pending=false;}
   expected_pending=irq!=0;
   ++calls;++coverage[c.interrupted_pc];
   if(c.interrupted_pc==0x8afe){sub_inputs.insert(uint16_t(c.registers.d[1]));sub_flags[sr&31]=true;}
   if(c.interrupted_pc==0xb632){
    if(next==0xb654)++branch_taken;else ++branch_untaken;
    branch_nv[(bool(c.sr&8)<<1)|bool(c.sr&2)]=true;
   }
   if(c.interrupted_pc==0x8af8||c.interrupted_pc==0xb62e){
    const unsigned i=c.interrupted_pc==0xb62e;
    word_inputs[i].insert(i?uint16_t(c.registers.d[0]):uint16_t(post.d[1]));
    ++word_x[i][bool(c.sr&16)];word_flags[i][sr&15]=true;
   }
  }else if(kind==2){c.return_pc=num(f,4);c.sr=num(f,2);c.level=num(f,1);c.registers=regs(f);end=num(f,8);expected=events(f);sp=num(f,4);sr=num(f,2);
   if(!have||!expected_pending)throw std::runtime_error("entry without instruction");
   ok=last.irq==c.level&&last.state.pc==c.return_pc&&last.state.ns==c.boundary_ns&&last.state.sr==c.sr&&last.state.registers.d==c.registers.d&&last.state.registers.a==c.registers.a;
   // These are outputs of caller_step, never captured completion/return inputs.
   c.return_pc=last.state.pc;c.boundary_ns=last.state.ns;c.sr=last.state.sr;c.registers=last.state.registers;
   if(c.sr&0x2000)ok &= c.supervisor_sp==last.state.sp;
   if(rte_entry_pending){
    ok &= c.supervisor_sp==scheduled.supervisor_stack()&&c.interrupted_pc==scheduled.current_pc();
    c.supervisor_sp=scheduled.supervisor_stack();rte_entry_pending=false;
   }
   expected_pending=false;
   ++entries;++levels[c.level];++irqcoverage[c.interrupted_pc];next=c.level==2?0x1461c:c.level==4?0x14674:0x1466c;post=c.registers;
  }else throw std::runtime_error("invalid caller record kind");
  size_t cursor=0;uint64_t now=c.boundary_ns,phase_end=chain?prior_phase_end:0;
  MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m){
   ++total;bool same=cursor<expected.size();
   if(same){const auto& e=expected[cursor++];same=e.ns==now&&e.pc==scheduled.current_pc()&&e.a==a&&e.m==m&&e.w==unsigned(w)&&(e.d&m)==(d&m);
    if(!same&&bad<10)std::cerr<<std::hex<<"Caller pc="<<c.interrupted_pc<<" a="<<a<<'/'<<e.a<<" d="<<(d&m)<<'/'<<(e.d&m)<<std::dec<<" ns="<<now<<'/'<<e.ns<<'\n';}
   if(!same){++bad;ok=false;}
   if(kind==1&&w){auto o=MainBus::memory_offset(a);if(o!=MainBus::UNMAPPED){if(m&0xff00)owned[o]=d>>8;if(m&255)owned[o+1]=d;}}
  }};
  // Entry contexts and external handler/sub changes remain boundaries. Within
  // consecutive caller instructions, a computed main write cannot be replaced.
  trace.before_access=[&](bool w,uint32_t a,uint16_t m){
   if(kind==1){
    auto phase=visibility.find(now);
    if(!phase)throw std::runtime_error("missing physical bus observation at PC "+std::to_string(scheduled.current_pc())+" time "+std::to_string(now)+" address "+std::to_string(a));
    if(phase_end){++computed_frontiers;if(phase_end!=phase->previous_end){++frontier_bad;ok=false;}}
    phase_end=now+4*OMainIrqService::CYCLE_NS;
   }
   if(kind==1&&a==0x2a3c00)wait_operand_known=true;
   if(w||kind!=1||cursor>=expected.size())return;
   const auto& e=expected[cursor];if(e.w||e.a!=a||e.m!=m)return;
   auto o=MainBus::memory_offset(a);if(o==MainBus::UNMAPPED)return;
   auto lane=[&](uint32_t i,uint8_t value){auto it=owned.find(i);if(it!=owned.end()){if(it->second!=value){ok=false;std::cerr<<"Owned lane mismatch address="<<std::hex<<a<<" offset="<<i<<" owned="<<unsigned(it->second)<<" expected="<<unsigned(value)<<std::dec<<" ns="<<now<<'\n';}}else memory.bytes[i]=value;};
   if(m&0xff00)lane(o,e.d>>8);if(m&255)lane(o+1,e.d);
  };
  bus.trace=&trace;
  OMainIrqService::Result result;
  if(kind==1){
   if(!chain)scheduled.resume({c.registers,c.interrupted_pc,c.supervisor_sp,c.sr,c.boundary_ns});
   last=scheduled.step(bus,[&](uint64_t t){now=t;},[&](uint64_t t){
    ++polls;
    const auto p=c.interrupted_pc;
    const bool internal=CpuVisibilityCapture::internal(p);
    const auto* phase=visibility.find(t);
    const auto level=internal?visibility.internal_poll(c.boundary_ns):phase?phase->level:0;
    if(internal){++internal_polls;if(t+160!=end){++internal_poll_bad;ok=false;}}
    if(!internal&&!phase)ok=false;
    if(level&&!OMainIrqService::eligible(level,c.sr))++masked;
    return level;
   });result=last.state;have=true;ok &= last.irq==irq;
  }else{result=scheduled.enter(bus,c.supervisor_sp,[&](uint64_t t){now=t;});service_expected=linked&&c.level!=4;have=false;owned.clear();wait_operand_known=false;}
  prior_phase_end=phase_end;
  bus.trace=nullptr;
  ok &= cursor==expected.size()&&result.ns==end&&result.pc==next&&result.sp==sp&&result.sr==sr&&result.registers.d==post.d&&result.registers.a==post.a;
  if(baseline_return){if(ok)++baseline_passed;else ++baseline_bad;}
  if(!ok){++failed;if(failed<10)std::cerr<<std::hex<<"Caller record pc="<<c.interrupted_pc<<" result="<<result.pc<<'/'<<next<<" sr="<<result.sr<<'/'<<sr<<" sp="<<result.sp<<'/'<<sp<<std::dec<<" end="<<result.ns<<'/'<<end<<" events="<<cursor<<'/'<<expected.size()<<" IRQ="<<last.irq<<'/'<<irq<<'\n';}
 }
 std::cout<<"Internal completion selections: "<<internal_polls-internal_poll_bad<<" of "<<internal_polls<<" identical, "<<internal_poll_bad<<" different, 0 skipped\n";
 if(baseline_count)std::cout<<"Prior unported return boundaries: "<<baseline_passed<<" of "<<baseline_count<<" identical, "<<baseline_bad<<" different, "<<baseline.size()<<" missing, 0 skipped\n";
 std::cout<<"RTE selections: "<<selections-selection_bad<<" of "<<selections<<" identical, "<<selection_bad<<" different, 0 skipped; "<<back_to_back<<" immediate exceptions\n";
 std::cout<<"Linked IRQ services: "<<services-service_bad<<" of "<<services<<" identical, "<<service_bad<<" different, 0 skipped; "<<service_events-service_event_bad<<" of "<<service_events<<" bus events identical, "<<service_event_bad<<" different; "<<resumptions<<" resumed caller boundaries, "<<external_returns<<" unported post-RTE boundaries; "<<graph_exits<<" explicit unported graph exits; IRQ2/IRQ6="<<service2<<'/'<<service6<<" user/supervisor returns="<<user_returns<<'/'<<services-user_returns<<"; "<<services<<" supplied shared-RAM snapshots, "<<adc_inputs<<" computed ADC results, 0 supplied ADC results\n";
 std::cout<<"Independently scheduled wait instructions: "<<autonomous<<" instructions, "<<autonomous_events-autonomous_bad<<" of "<<autonomous_events<<" bus assertions identical, "<<autonomous_bad<<" different, 0 skipped; "<<cuts<<" explicit fixture cuts, "<<resume_operands<<" supplied first-resume operands\n";
 if(service_expected)throw std::runtime_error("unfinished linked caller service");
 if(expected_pending)throw std::runtime_error("unfinished caller IRQ entry");
 if(selection_expected)throw std::runtime_error("unfinished RTE selection assertion");
 std::cout<<"Caller instructions/IRQ entries: "<<calls+entries-failed<<" of "<<calls+entries<<" identical, "<<failed<<" different, 0 skipped\nCaller bus events: "<<total-bad<<" of "<<total<<" identical, "<<bad<<" different, 0 skipped\nConsecutive caller boundaries: "<<chains-chain_bad<<" of "<<chains<<" identical, "<<chain_bad<<" different, 0 skipped\nCoverage: instructions="<<calls<<" IRQ entries="<<entries<<" polls="<<polls<<" IRQ2="<<levels[2]<<" IRQ4="<<levels[4]<<" IRQ6="<<levels[6]<<" computed phase frontiers="<<computed_frontiers<<" frontier differences="<<frontier_bad<<" masked="<<masked<<'\n';
 if(!sub_inputs.empty()){
  std::cout<<"Successor SUB: "<<sub_inputs.size()<<" source words, XNZVC";
  for(unsigned flags=0;flags<32;++flags)if(sub_flags[flags])std::cout<<' '<<std::hex<<flags<<std::dec;
  std::cout<<'\n';
 }
 if(branch_taken||branch_untaken){
  std::cout<<"Successor BGE: taken/untaken="<<branch_taken<<'/'<<branch_untaken<<" NV";
  for(unsigned flags=0;flags<4;++flags)if(branch_nv[flags])std::cout<<' '<<flags;
  std::cout<<'\n';
 }
 for(unsigned i=0;i<2;++i)if(!word_inputs[i].empty()){
  std::cout<<"Returned word "<<std::hex<<(i?0xb62e:0x8af8)<<std::dec<<": "<<word_inputs[i].size()<<" operand values, X clear/set="<<word_x[i][0]<<'/'<<word_x[i][1]<<" NZVC";
  for(unsigned flags=0;flags<16;++flags)if(word_flags[i][flags])std::cout<<' '<<std::hex<<flags<<std::dec;
  std::cout<<'\n';
 }
 for(auto [p,n]:external_return_pcs)std::cout<<"Unported returned PC "<<std::hex<<p<<std::dec<<": "<<n<<" boundaries\n";
 for(auto [p,n]:coverage)std::cout<<std::hex<<p<<std::dec<<": "<<n<<" instructions, "<<irqcoverage[p]<<" IRQ entries\n";
 return calls&&entries&&(!linked||(services&&resumptions))&&!failed&&!bad&&!frontier_bad&&!autonomous_bad&&!service_bad&&!service_event_bad&&!selection_bad&&!baseline_bad&&baseline.empty()&&(!require_closed||external_returns==0)&&(!require_taken||(branch_taken&&branch_nv[0]&&branch_nv[3]))&&(!returns||(selections==services&&(successors||back_to_back)))?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

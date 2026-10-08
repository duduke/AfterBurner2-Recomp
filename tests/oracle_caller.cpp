#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <memory>
#include "caller_irq4_capture.hpp"
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
 if(!f.read(magic,8)||(memcmp(magic,"AB2CALL1",8)&&memcmp(magic,"AB2CALL2",8)&&memcmp(magic,"AB2CALL3",8)&&memcmp(magic,"AB2CALL4",8)&&memcmp(magic,"AB2CALL5",8)&&memcmp(magic,"AB2CALL6",8)&&memcmp(magic,"AB2CALL7",8)&&memcmp(magic,"AB2CALL8",8)&&memcmp(magic,"AB2CALL9",8)&&memcmp(magic,"AB2CAL10",8)))throw std::runtime_error("invalid caller capture");
 const bool linked_irq4=!memcmp(magic,"AB2CAL10",8);
 const bool snapshots=linked_irq4||!memcmp(magic,"AB2CALL9",8);
 const bool closure=snapshots||!memcmp(magic,"AB2CALL8",8);
 const bool successors=closure||!memcmp(magic,"AB2CALL7",8);
 bool require_host=false,require_host_path=false;
 bool require_weapon=false,require_weapon_path=false;
 bool require_companion=false,require_companion_path=false,require_flight=false,require_flight_path=false,require_input=false,require_input_path=false,require_soundproducer_path=false,require_soundproducer=false,require_taken=false,require_closed=false,require_follower=false,require_ready=false,require_worldinit=false,require_publication_init=false,require_allirq=false,require_ready_path=false,require_clear=false,require_trail=false,require_trail_path=false;const char* baseline_path=nullptr;
 for(int i=2;i<argc;++i){
  const std::string arg=argv[i];
  if(arg=="--require-host")require_host=true;
  else if(arg=="--require-host-path")require_host_path=true;
  else if(arg=="--require-weapon")require_weapon=true;
  else if(arg=="--require-weapon-path")require_weapon_path=true;
  else if(arg=="--require-companion")require_companion=true;
  else if(arg=="--require-companion-path")require_companion_path=true;
  else if(arg=="--require-flight")require_flight=true;
  else if(arg=="--require-flight-path")require_flight_path=true;
  else if(arg=="--require-input")require_input=true;
  else if(arg=="--require-input-path")require_input_path=true;
  else if(arg=="--require-soundproducer-path")require_soundproducer_path=true;
  else if(arg=="--require-soundproducer")require_soundproducer=true;
  else if(arg=="--require-taken")require_taken=true;
  else if(arg=="--require-closed")require_closed=true;
  else if(arg=="--require-follower")require_follower=true;
  else if(arg=="--require-ready")require_ready=true;
  else if(arg=="--require-worldinit")require_worldinit=true;
  else if(arg=="--require-publication-init")require_publication_init=true;
  else if(arg=="--require-trail")require_trail=true;
  else if(arg=="--require-trail-path")require_trail_path=true;
  else if(arg=="--require-clear")require_clear=true;
  else if(arg=="--require-allirq")require_allirq=true;
  else if(arg=="--require-ready-path")require_ready_path=true;
  else if(!baseline_path)baseline_path=argv[i];
  else throw std::runtime_error("unexpected caller argument");
 }
 if(require_taken&&!successors)throw std::runtime_error("taken-branch fixture requires CALL7+");
 if(require_closed&&!snapshots)throw std::runtime_error("closed return graph requires instruction snapshots");
 auto baseline=baseline_path?return_boundaries(baseline_path,successors,closure):std::map<ReturnKey,OMainIrqService::Result>{};
 const auto baseline_count=baseline.size();unsigned baseline_passed=0,baseline_bad=0;
 if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
 MainBus::Memory memory;MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
 std::unique_ptr<CallerIrq4Capture> irq4_capture;
 if(linked_irq4)irq4_capture=std::make_unique<CallerIrq4Capture>(std::string(argv[1])+".irq4",bus);
 const CallerIrq4Capture::Envelope* irq4_pending=nullptr;
 unsigned services4=0,bus4=0,bus4_bad=0,service4_bad=0,sub4=0;
 CpuVisibilityCapture visibility((std::string(argc>1?argv[1]:"build/oracle/caller.bin")+".visibility").c_str());
 visibility.report();
 if(visibility.failed)throw std::runtime_error("physical visibility mismatch");
 std::unique_ptr<OMainIO> physical_input;
 if(visibility.physical_buttons)physical_input=std::make_unique<OMainIO>(bus);
 OMainCallerSchedule scheduled;OMainIrqService::Instruction last{};bool have=false,expected_pending=false;
 unsigned calls=0,entries=0,failed=0,total=0,bad=0,chains=0,chain_bad=0,polls=0,masked=0,computed_frontiers=0,frontier_bad=0,levels[7]={};
 std::map<uint32_t,unsigned> coverage,irqcoverage,external_return_pcs;
 unsigned computed_raw_input_reads=0,fixture_raw_input_reads=0,external_raw_input_reads=0;
 unsigned sprite_cursor_frontiers=0;
 std::map<uint32_t,uint8_t> owned,sprite_owned_hidden;
 auto clear_owned=[&](){owned.clear();sprite_owned_hidden.clear();};
 auto track_write=[&](uint32_t a,uint16_t d,uint16_t m){
  const auto o=MainBus::memory_offset(a);
  if(o!=MainBus::UNMAPPED){if(m&0xff00)owned[o]=d>>8;if(m&255)owned[o+1]=d;}
  else if(a>=0x110000&&a<0x120000){
   // The hardware swaps CPU-visible and hidden sprite RAM before this trace.
   // Ownership follows each bank; retired list bytes cannot own the new bank.
   std::map<uint32_t,uint8_t> retired;
   auto first=owned.lower_bound(MainBus::SPRITE),last=owned.lower_bound(MainBus::PALETTE);
   retired.insert(first,last);owned.erase(first,last);
   owned.insert(sprite_owned_hidden.begin(),sprite_owned_hidden.end());
   sprite_owned_hidden=std::move(retired);
   // MAME sega_xboard_sprite_device::draw initializes/updates word 7 in
   // every rendered record, including hidden sprites, before the end marker.
   // Those published cursor words now belong to the external video producer.
   // CPU writes were asserted above; future cursor values are video inputs.
   for(size_t i=0;i<memory.sprite_buffer.size();i+=16){
    if(memory.sprite_buffer[i]&0x80)break;
    sprite_cursor_frontiers+=sprite_owned_hidden.erase(MainBus::SPRITE+i+14);
    sprite_cursor_frontiers+=sprite_owned_hidden.erase(MainBus::SPRITE+i+15);
   }
   owned[MainBus::SPRITE]=0xff;owned[MainBus::SPRITE+1]=0xff;
  }
 };
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
 unsigned graph_exits=0;std::map<uint32_t,unsigned> graph_exit_pcs;
 std::map<uint32_t,std::array<unsigned,2>> trail_branches;
 std::map<uint32_t,std::array<unsigned,2>> companion_branches,weapon_branches;
 std::map<uint32_t,std::array<unsigned,2>> host_branches;
 unsigned host_flight_calls=0,host_hud_calls=0,host_launch_calls=0,host_sound_calls=0;
 std::set<unsigned> host_decimal,host_commands;
 std::map<uint32_t,std::set<unsigned>> host_return_bytes;
 unsigned weapon_dispatches=0,weapon_fire_calls=0,weapon_allocation_calls=0;
 std::set<unsigned> weapon_ammo,weapon_reserve,weapon_launch,weapon_append;
 std::set<unsigned> companion_counts;
 std::map<uint32_t,std::set<unsigned>> companion_bytes,companion_banks,companion_counters,companion_rows;
 std::map<uint32_t,std::set<unsigned>> companion_tables;
 std::map<uint32_t,std::array<unsigned,2>> flight_branches;
 std::set<unsigned> flight_slew[2],flight_counts,flight_rolls;
 std::map<uint32_t,std::array<unsigned,2>> input_branches;
 std::set<unsigned> input_axis[3],input_accum[2],physical_button_values;
 std::map<uint32_t,std::array<unsigned,2>> producer_branches;
 std::set<unsigned> producer_indices,producer_counts,producer_commands;
 bool ready_bits[2]{};unsigned ready_taken=0,ready_untaken=0;
 std::set<uint16_t> follower_stages;std::set<uint8_t> follower_ages;
 unsigned autonomous=0,autonomous_events=0,autonomous_bad=0,cuts=0,resume_operands=0;
 bool wait_operand_known=false,service_return_pending=false;
 while(f.peek()!=std::char_traits<char>::eof()){
  auto kind=num(f,1);
  if(service_expected&&kind!=4&&kind!=8)throw std::runtime_error("missing linked caller service");
  if(selection_expected&&kind!=6)throw std::runtime_error("missing RTE selection assertion");
  if(kind==6){
   if(!returns||!service_return_pending)throw std::runtime_error("RTE selection without service return");
   selection_expected=false;const unsigned irq=num(f,1);++selections;
   if(irq!=last.irq||last.poll_ns+320!=last.state.ns){++selection_bad;std::cerr<<"RTE selection mismatch "<<last.irq<<'/'<<irq<<" at "<<last.poll_ns<<'\n';}
   expected_pending=last.irq!=0;
   if(expected_pending){++back_to_back;service_return_pending=false;rte_entry_pending=true;}
   continue;
  }
  if(kind==8){
   if(!service_expected||!irq4_pending)throw std::runtime_error("IRQ4 return without computed entry");
   service_expected=false;const auto& capture=*irq4_pending;irq4_pending=nullptr;
   const auto end=num(f,8),sp=num(f,4),sr=num(f,2),next=num(f,4);const auto post=regs(f);
   const auto entered=scheduled.state().state;bool ok=true;
   const auto frame=MainBus::memory_offset(entered.sp);
   for(size_t i=0;i<capture.memory.size();++i){
    // The companion snapshot is at the FIRST entry write, before the saved
    // PC lanes. The entire exception frame has already been computed here.
    if(frame!=MainBus::UNMAPPED&&i>=frame&&i<frame+6)continue;
    if(owned.count(i)||(wait_operand_known&&i>=MainBus::memory_offset(0x2a3c00)&&i<MainBus::memory_offset(0x2a3c00)+2)){
     if(memory.bytes[i]!=capture.memory[i]){ok=false;if(service4_bad<3)std::cerr<<"IRQ4 retained lane "<<std::hex<<i<<std::dec<<'\n';}
    }else memory.bytes[i]=capture.memory[i];
   }
   size_t cursor=0,subcursor=0,hcursor=0;uint64_t now=entered.ns;
   while(cursor<capture.events.size()&&capture.events[cursor].ns<entered.ns)++cursor;
   unsigned frontier=cursor;
   auto apply_sub=[&](){
    while(subcursor<capture.sub.size()&&capture.sub[subcursor].frontier<=frontier){
     const auto& e=capture.sub[subcursor++].event;const auto o=MainBus::memory_offset(e.a);
     if(!((o>=MainBus::SHARED&&o<MainBus::TILE)||(o>=MainBus::ROAD&&o+1<memory.bytes.size())))throw std::runtime_error("invalid linked IRQ4 sub lane");
     if(e.a==0x29c0d6){
      if(e.pc!=0x458||e.m!=0xff00)throw std::runtime_error("invalid linked sub mutex write");
      memory.bytes[o]=(memory.bytes[o]&~2)|((e.d>>8)&2);
     }else if(e.m&0xff00)memory.bytes[o]=e.d>>8;
     if(e.m&255)memory.bytes[o+1]=e.d;++sub4;
    }
   };
   MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m){
    ++frontier;++bus4;bool equal=cursor<capture.events.size();
    if(equal){const auto& e=capture.events[cursor++];equal=e.ns==now&&e.pc==scheduled.current_pc()&&e.a==a&&e.m==m&&e.w==w&&(e.d&m)==(d&m);}
    if(!equal){++bus4_bad;ok=false;if(bus4_bad<10)std::cerr<<"Linked IRQ4 bus pc="<<std::hex<<scheduled.current_pc()<<" a="<<a<<std::dec<<" t="<<now<<'\n';}
    if(w)track_write(a,d,m);
    prior_phase_end=now+320;
   }};
   trace.before_access=[&](bool w,uint32_t a,uint16_t m){
    apply_sub();
    if(physical_input&&a==0x150000)physical_input->set_buttons(visibility.buttons_at(now));
    if(w||cursor>=capture.events.size())return;const auto& e=capture.events[cursor];if(e.a!=a||e.m!=m)return;
    if(scheduled.current_pc()==0xb7e2||(scheduled.current_pc()>=0xb7ea&&scheduled.current_pc()<=0xb7f0)){
     const auto o=MainBus::memory_offset(a);if(o<MainBus::SHARED||o>=MainBus::TILE)throw std::runtime_error("invalid linked IRQ4 blit input");
     memory.bytes[o]=e.d>>8;memory.bytes[o+1]=e.d;
    }else if(a==0x150000&&!physical_input)bus.io[1].input=[v=uint8_t(e.d)](unsigned p){return p==0?v:uint8_t(0xff);};
   };
   bus.trace=&trace;
   const auto result=scheduled.finish(bus,[&](uint64_t t){now=t;},[&](uint64_t t){
    const auto* phase=visibility.find(t);if(!phase)throw std::runtime_error("missing IRQ4 RTE visibility");return phase->level;
   },{},[&](bool entering,uint32_t target,const OMainObjects::Registers& r,uint64_t at){
    if(hcursor>=capture.hooks.size())throw std::runtime_error("missing linked IRQ4 callee assertion");
    const auto& h=capture.hooks[hcursor];const auto& expected=entering?h.pre:h.post;
    if(h.entry!=target||at!=(entering?h.start:h.end)||r.d!=expected.d||r.a!=expected.a)ok=false;
    if(!entering)++hcursor;
   });bus.trace=nullptr;apply_sub();
   ok &= cursor==capture.events.size()&&subcursor==capture.sub.size()&&hcursor==capture.hooks.size()&&
     result.ns==end&&end==capture.end&&result.sp==sp&&scheduled.supervisor_stack()==capture.sp&&result.sr==sr&&sr==capture.sr&&
     result.pc==next&&result.registers.d==post.d&&post.d==capture.post.d&&result.registers.a==post.a&&post.a==capture.post.a;
   ++services4;if(!ok){++service4_bad;std::cerr<<"Linked IRQ4 return mismatch at "<<end<<'\n';}
   prior_phase_end=result.ns;last=scheduled.state();have=true;service_return_pending=true;selection_expected=true;continue;
  }
  if(kind==4){
   if(!service_expected||irq4_pending)throw std::runtime_error("service without computed caller entry");
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
    else if(owned.count(i)||(wait_operand_known&&i>=MainBus::memory_offset(0x2a3c00)&&i<MainBus::memory_offset(0x2a3c00)+2)){
     if(memory.bytes[i]!=before[i]){ok=false;std::cerr<<"Retained service lane "<<std::hex<<i<<" got="<<unsigned(memory.bytes[i])<<" expected="<<unsigned(before[i])<<std::dec<<" end="<<end<<'\n';}
    } else memory.bytes[i]=before[i];
   }
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
    if(w)track_write(a,d,m);
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
   last=scheduled.state();have=true;service_return_pending=true;selection_expected=returns;continue;
  }
  if(kind==7){if(!returned_steps)throw std::runtime_error("unexpected graph exit");++graph_exits;++graph_exit_pcs[last.state.pc];service_return_pending=false;have=false;clear_owned();wait_operand_known=false;prior_phase_end=0;continue;}
  if(kind==5){++external_returns;++external_return_pcs[last.state.pc];service_return_pending=false;have=false;clear_owned();wait_operand_known=false;prior_phase_end=0;continue;}
  if(kind==3){service_return_pending=false;have=false;clear_owned();wait_operand_known=false;prior_phase_end=0;++cuts;continue;}OMainIrqService::Context c{};c.interrupted_pc=num(f,4);c.boundary_ns=num(f,8);c.supervisor_sp=num(f,4);
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
    // context; linked IRQ2/4/6 returns and omitted waits carry computed state.
    c.registers=last.state.registers;c.sr=last.state.sr;
    c.supervisor_sp=last.state.sp;c.boundary_ns=last.state.ns;
   }else {clear_owned();wait_operand_known=false;service_return_pending=false;}
   if(const auto* instruction=main_input_instruction(c.interrupted_pc);instruction&&instruction->branch){
    ++input_branches[c.interrupted_pc][next!=c.interrupted_pc+instruction->branch];
   }
   if(const auto* instruction=main_flight_instruction(c.interrupted_pc);instruction&&instruction->branch)
    ++flight_branches[c.interrupted_pc][next!=c.interrupted_pc+instruction->branch];
   if(const auto* instruction=main_host_instruction(c.interrupted_pc);instruction&&instruction->branch)
    ++host_branches[c.interrupted_pc][next!=c.interrupted_pc+instruction->branch];
   if(c.interrupted_pc==0x145b0&&(next==0x9810||next==0x9626||next==0x964c))++host_flight_calls;
   if(c.interrupted_pc==0x145b0&&next==0x9f7c)++host_launch_calls;
   if(c.interrupted_pc==0x9ed2)++host_hud_calls;
   if(c.interrupted_pc==0x969a||c.interrupted_pc==0xa022){++host_sound_calls;host_commands.insert(uint8_t(c.registers.d[0]));}
   if(c.interrupted_pc==0xd062)host_decimal.insert(c.registers.d[0]&65535);
   if(const auto* instruction=main_weapon_instruction(c.interrupted_pc);instruction&&instruction->branch){
    // $9D6A's taken target equals its fall-through. Its Z flag distinguishes
    // both paths even though the resulting PC agrees.
    const bool taken=c.interrupted_pc==0x9d6a?bool(c.sr&4):next!=c.interrupted_pc+instruction->branch;
    ++weapon_branches[c.interrupted_pc][taken];
   }
   if(c.interrupted_pc==0x145b0&&(next==0x9d34||next==0x9d56))++weapon_dispatches;
   if(c.interrupted_pc==0x9d84||c.interrupted_pc==0x9d88)++weapon_fire_calls;
   if(c.interrupted_pc==0x9e4a||c.interrupted_pc==0x9e7c)++weapon_allocation_calls;
   if(const auto* instruction=main_companion_instruction(c.interrupted_pc);instruction&&instruction->branch)
    ++companion_branches[c.interrupted_pc][next!=c.interrupted_pc+instruction->branch];
   if(c.interrupted_pc==0xb62e&&(c.registers.a[5]&0x3fffff)==0x3fc080)weapon_append.insert(uint16_t(c.registers.d[0]));
   for(const auto& e:expected)if(!e.w){
    if(c.interrupted_pc==0x9e42&&e.a==0x3f8d66)weapon_ammo.insert(e.d);
    if(c.interrupted_pc==0x9e92&&e.a==0x3f8e7e)weapon_reserve.insert(e.d&255);
    if(c.interrupted_pc==0x9eac&&e.a==((c.registers.a[5]+62)&0x3ffffe))weapon_launch.insert(e.d);
   }
   if(c.interrupted_pc==0xaf64||c.interrupted_pc==0xaff4)companion_bytes[c.interrupted_pc].insert(uint8_t(c.registers.d[0]));
   if(c.interrupted_pc==0xaf72||c.interrupted_pc==0xb002)companion_banks[c.interrupted_pc].insert(uint16_t(c.registers.d[0]));
   if(c.interrupted_pc==0xaf4c||c.interrupted_pc==0xafdc||c.interrupted_pc==0xb06c)
    for(const auto& e:expected)if(!e.w&&e.a==0x3f8d78)companion_counters[c.interrupted_pc].insert(e.d);
   if(c.interrupted_pc==0xafa2||c.interrupted_pc==0xb032)companion_rows[c.interrupted_pc].insert(uint16_t(c.registers.d[1]));
   if(c.interrupted_pc==0xafa2||c.interrupted_pc==0xb032)companion_tables[c.interrupted_pc].insert(c.registers.a[1]);
   if(c.interrupted_pc==0xb62e&&c.registers.a[5]==0x3ff400)companion_counts.insert(uint16_t(c.registers.d[0]));
   if(c.interrupted_pc==0x9938)flight_slew[0].insert(uint16_t(c.registers.d[0]));
   if(c.interrupted_pc==0x9960)flight_slew[1].insert(uint16_t(c.registers.d[0]));
   if(c.interrupted_pc==0x9bae)flight_rolls.insert(uint16_t(c.registers.d[0])&255);
   if(c.interrupted_pc==0xb62e&&c.registers.a[5]==0x3ffc00)flight_counts.insert(uint16_t(c.registers.d[0]));
   switch(c.interrupted_pc){
   case 0xb850:++producer_branches[c.interrupted_pc][next==0xb87c];break;
   case 0xb85c:++producer_branches[c.interrupted_pc][next==0xb884];break;
   case 0xab06:case 0xabca:case 0xabd2:case 0xac36:++trail_branches[c.interrupted_pc][next==0xae32];break;
   case 0xab28:++trail_branches[c.interrupted_pc][next==0xab44];break;
   case 0xac2a:++trail_branches[c.interrupted_pc][next==0xac4a];break;
   }
   if(c.interrupted_pc==0xb868){producer_indices.insert(uint16_t(c.registers.d[1]));producer_commands.insert(uint8_t(c.registers.d[0]));}
   if(c.interrupted_pc==0xc090){if(next==0xc098)++ready_taken;else ++ready_untaken;}
   expected_pending=irq!=0;
   if(c.interrupted_pc==0x6844||c.interrupted_pc==0x14ee8)
    for(const auto& e:expected)if(!e.w&&e.a==0x29c200&&e.m==0xff00)host_return_bytes[c.interrupted_pc].insert(e.d>>8);
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
   if(w)track_write(a,d,m);
  }};
  // Entry contexts and external handler/sub changes remain boundaries. Within
  // consecutive caller instructions, a computed main write cannot be replaced.
  trace.before_access=[&](bool w,uint32_t a,uint16_t m){
   if(kind==1){
    auto phase=visibility.find(now);
    if(!phase)throw std::runtime_error("missing physical bus observation at PC "+std::to_string(scheduled.current_pc())+" time "+std::to_string(now)+" address "+std::to_string(a));
    if(phase_end){++computed_frontiers;if(phase_end!=phase->previous_end){++frontier_bad;ok=false;}}
    if(physical_input){physical_input->set_buttons(phase->buttons);if(c.interrupted_pc==0x14980&&a==0x150000)physical_button_values.insert(phase->buttons&0x30);}
    if(main_input_instruction(c.interrupted_pc)&&cursor<expected.size()&&!w){
     const auto data=expected[cursor].d;
     if(a>=0x3f8892&&a<=0x3f8894){
      if(m&0xff00)input_axis[a-0x3f8892].insert(data>>8);
      if(m&255&&a+1<=0x3f8894)input_axis[a+1-0x3f8892].insert(data&255);
     }
     if((c.interrupted_pc==0xb10c||c.interrupted_pc==0xb1dc)&&a==((c.registers.a[5]+(c.interrupted_pc==0xb10c?24:28))&0x3ffffe))input_accum[c.interrupted_pc==0xb1dc].insert(data);
    }
    phase_end=now+4*OMainIrqService::CYCLE_NS;
   }
   if(kind==1&&!w&&c.interrupted_pc==0xb876&&a==0x3f8906&&cursor<expected.size())producer_counts.insert(expected[cursor].d>>8);
   if(kind==1&&!w&&c.interrupted_pc==0xc088&&a==0x29c200&&cursor<expected.size())ready_bits[bool(expected[cursor].d&0x100)]=true;
   if(kind==1&&!w&&c.interrupted_pc==0x7bce&&a==0x29c09c&&cursor<expected.size())follower_stages.insert(expected[cursor].d);
   if(kind==1&&!w&&c.interrupted_pc==0x7bf4&&a==((c.registers.a[5]+21)&0x3ffffe)&&cursor<expected.size())follower_ages.insert(expected[cursor].d&255);
   const bool retained_wait=kind==1&&a==0x2a3c00&&wait_operand_known;
   if(w||kind!=1||cursor>=expected.size())return;
   const auto& e=expected[cursor];if(e.w||e.a!=a||e.m!=m)return;
   auto o=MainBus::memory_offset(a);if(o==MainBus::UNMAPPED)return;
   const bool raw_input=visibility.physical_buttons&&main_input_instruction(c.interrupted_pc)&&a>=0x3f8892&&a<=0x3f8896;
   const bool fixture_input=raw_input&&c.registers.a[5]==0x3ffc00;
   const auto* raw_phase=raw_input?visibility.find(now):nullptr;
   if(fixture_input)++fixture_raw_input_reads;
   auto lane=[&](uint32_t i,uint8_t value){
    if(raw_input&&!fixture_input){
     const unsigned offset=i-MainBus::memory_offset(0x3f8892);
     if(raw_phase&&offset<6&&(raw_phase->sample_mask&(1u<<offset))){
      ++computed_raw_input_reads;memory.bytes[i]=raw_phase->sampled[offset];
      if(memory.bytes[i]!=value){ok=false;std::cerr<<"Computed input sample mismatch at "<<now<<'\n';}return;
     }else ++external_raw_input_reads;
    }
    auto it=owned.find(i);if(retained_wait){if(memory.bytes[i]!=value){ok=false;std::cerr<<"Retained wait operand mismatch at "<<now<<'\n';}}else if(it!=owned.end()){if(it->second!=value){ok=false;std::cerr<<"Owned lane mismatch address="<<std::hex<<a<<" offset="<<i<<" owned="<<unsigned(it->second)<<" expected="<<unsigned(value)<<std::dec<<" ns="<<now<<'\n';}}else memory.bytes[i]=value;};
   if(m&0xff00)lane(o,e.d>>8);if(m&255)lane(o+1,e.d);
   if(a==0x2a3c00)wait_operand_known=true;
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
  }else{result=scheduled.enter(bus,c.supervisor_sp,[&](uint64_t t){now=t;});service_expected=linked&&(c.level!=4||linked_irq4);
   if(linked_irq4&&c.level==4){
    irq4_pending=&irq4_capture->find(c.boundary_ns,c.return_pc);
    const auto& q=irq4_pending->context;
    ok &= q.interrupted_pc==c.interrupted_pc&&q.sr==c.sr&&q.registers.d==c.registers.d&&q.registers.a==c.registers.a;
   }
   have=false;if(!service_expected){clear_owned();wait_operand_known=false;}}
  prior_phase_end=phase_end;
  bus.trace=nullptr;
  ok &= cursor==expected.size()&&result.ns==end&&result.pc==next&&result.sp==sp&&result.sr==sr&&result.registers.d==post.d&&result.registers.a==post.a;
  if(baseline_return){if(ok)++baseline_passed;else ++baseline_bad;}
  if(!ok){++failed;if(failed<10)std::cerr<<std::hex<<"Caller record pc="<<c.interrupted_pc<<" result="<<result.pc<<'/'<<next<<" sr="<<result.sr<<'/'<<sr<<" sp="<<result.sp<<'/'<<sp<<std::dec<<" end="<<result.ns<<'/'<<end<<" events="<<cursor<<'/'<<expected.size()<<" IRQ="<<last.irq<<'/'<<irq<<'\n';}
 }
 if(linked_irq4&&(service_expected||selection_expected||expected_pending||rte_entry_pending))throw std::runtime_error("incomplete linked IRQ capture");
 std::cout<<"Internal completion selections: "<<internal_polls-internal_poll_bad<<" of "<<internal_polls<<" identical, "<<internal_poll_bad<<" different, 0 skipped\n";
 if(baseline_count)std::cout<<"Prior unported return boundaries: "<<baseline_passed<<" of "<<baseline_count<<" identical, "<<baseline_bad<<" different, "<<baseline.size()<<" missing, 0 skipped\n";
 std::cout<<"RTE selections: "<<selections-selection_bad<<" of "<<selections<<" identical, "<<selection_bad<<" different, 0 skipped; "<<back_to_back<<" immediate exceptions\n";
 if(visibility.physical_buttons)std::cout<<"Input sample reads: "<<computed_raw_input_reads<<" computed, "<<fixture_raw_input_reads<<" directed raw-input fixtures, "<<external_raw_input_reads<<" initial/external producer lanes; physical buttons use OMainIO\n";
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
 for(auto [p,n]:graph_exit_pcs)std::cout<<"Unported graph exit PC "<<std::hex<<p<<std::dec<<": "<<n<<" boundaries\n";
 if(linked_irq4)std::cout<<"Linked IRQ4 services: "<<services4-service4_bad<<" of "<<services4<<" identical, "<<service4_bad<<" different, 0 skipped; "<<bus4-bus4_bad<<" of "<<bus4<<" bus events identical, "<<bus4_bad<<" different; "<<sub4<<" supplied sub writes; "<<sprite_cursor_frontiers<<" published video cursor byte frontiers\n";
 if(require_host||require_host_path){
  unsigned observed=0,missing=0;
  for(const auto& i:main_host_instructions){
   observed+=coverage[i.pc]!=0;
   if(graph_exit_pcs[i.pc]||external_return_pcs[i.pc])throw std::runtime_error("host graph remains external at "+std::to_string(i.pc));
   if(require_host&&!coverage[i.pc]){++missing;std::cerr<<"Missing host instruction "<<std::hex<<i.pc<<std::dec<<'\n';}
   const auto opcode=bus.read16(i.pc);
   if(require_host&&i.branch&&(opcode&0xff00)!=0x6000){
    const auto& n=host_branches[i.pc];std::cout<<"Host branch "<<std::hex<<i.pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
    if(!n[0]||!n[1]){++missing;std::cerr<<"Missing host branch direction "<<std::hex<<i.pc<<std::dec<<'\n';}
   }
  }
  if(require_host_path&&(!host_flight_calls||!host_hud_calls||!host_launch_calls||!host_sound_calls))throw std::runtime_error("missing actual flight/HUD/launch-sound caller");
  if(require_host)for(unsigned v:{0,1,9,10,99,100,101,999,10000,65535})if(!host_decimal.count(v))throw std::runtime_error("missing decimal endpoint");
  std::cout<<"Host caller coverage: "<<observed<<'/'<<main_host_instructions.size()<<" PCs; flight/HUD/missile dispatch/sound calls="<<host_flight_calls<<'/'<<host_hud_calls<<'/'<<host_launch_calls<<'/'<<host_sound_calls<<"; commands";
  for(auto command:host_commands)std::cout<<' '<<std::hex<<command<<std::dec;std::cout<<'\n';
  for(auto [pc,values]:host_return_bytes){std::cout<<"Host returned bit-test "<<std::hex<<pc<<" bytes";for(auto value:values)std::cout<<' '<<value;std::cout<<std::dec<<'\n';}
  if(missing)throw std::runtime_error("incomplete host coverage");
 }
 if(require_weapon||require_weapon_path){
  unsigned observed=0;
  for(const auto& i:main_weapon_instructions){
   observed+=coverage[i.pc]!=0;
   if(graph_exit_pcs[i.pc]||external_return_pcs[i.pc])throw std::runtime_error("weapon graph remains external at "+std::to_string(i.pc));
   if(require_weapon&&!coverage[i.pc])throw std::runtime_error("missing weapon instruction at "+std::to_string(i.pc));
   if(require_weapon&&i.branch&&i.pc!=0x9d80){
    const auto& n=weapon_branches[i.pc];std::cout<<"Weapon branch "<<std::hex<<i.pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
    if(!n[0]||!n[1])throw std::runtime_error("missing weapon branch direction at "+std::to_string(i.pc));
   }
  }
  if(require_weapon){
   for(unsigned v:{0,20,0x8000,0xffff})if(!weapon_ammo.count(v))throw std::runtime_error("missing signed missile-ammo endpoint");
   for(unsigned v:{0,24,25,0x7f})if(!weapon_reserve.count(v))throw std::runtime_error("missing missile reserve endpoint");
   for(unsigned v:{0,0x3c,0x7fff,0xffff})if(!weapon_launch.count(v))throw std::runtime_error("missing launch-channel wrap endpoint");
   for(unsigned v:{0,0xbf,0xc0,0xc1})if(!weapon_append.count(v))throw std::runtime_error("missing weapon append endpoint");
  }
  if(!coverage[0x9d56]||!weapon_fire_calls)throw std::runtime_error("missing weapon controller/fire caller");
  if(require_weapon_path&&!weapon_dispatches)throw std::runtime_error("missing actual dispatcher weapon caller");
  std::cout<<"Weapon coverage: "<<observed<<'/'<<main_weapon_instructions.size()<<" PCs; "<<weapon_dispatches<<" dispatcher calls, "<<weapon_fire_calls<<" controller fire calls, "<<weapon_allocation_calls<<" allocation calls; HUD/pool reset remain labelled boundaries\n";
 }
 if(require_companion||require_companion_path){
  unsigned observed=0;
  for(const auto& i:main_companion_instructions){
   observed+=coverage[i.pc]!=0;
   if(graph_exit_pcs[i.pc]||external_return_pcs[i.pc])throw std::runtime_error("companion graph remains external at "+std::to_string(i.pc));
   if(require_companion&&!coverage[i.pc])throw std::runtime_error("missing companion instruction at "+std::to_string(i.pc));
  }
  for(const auto& i:main_companion_return_instructions){
   if(require_companion&&(!coverage[i.pc]||graph_exit_pcs[i.pc]||external_return_pcs[i.pc]))throw std::runtime_error("missing companion returned-instruction continuation");
   std::cout<<"Companion return "<<std::hex<<i.pc<<std::dec<<": "<<coverage[i.pc]<<" observations\n";
  }
  for(uint32_t p:{0x9c9e,0xaf3c,0xafcc})if(!coverage[p])throw std::runtime_error("missing natural shadow/exhaust update entry");
  if(require_companion){
   for(const auto& i:main_companion_instructions)if(i.branch&&i.pc!=0x9d1c&&i.pc!=0xaeb0&&i.pc!=0xaf7a&&i.pc!=0xaf98&&i.pc!=0xafc8&&i.pc!=0xb00a&&i.pc!=0xb028&&i.pc!=0xb058){
    const auto& n=companion_branches[i.pc];std::cout<<"Companion branch "<<std::hex<<i.pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
    if(!n[0]||!n[1])throw std::runtime_error("missing companion branch direction at "+std::to_string(i.pc));
   }
   for(uint32_t p:{0xaf64,0xaff4})for(unsigned v:{0,0x7f,0x80,0xfe,0xff})if(!companion_bytes[p].count(v))throw std::runtime_error("missing companion byte arithmetic endpoint");
   for(uint32_t p:{0xaf72,0xb002})for(unsigned v:{0xfffb,0xffff,0,1,5})if(!companion_banks[p].count(v))throw std::runtime_error("missing companion bank endpoint");
   for(uint32_t p:{0xaf4c,0xafdc,0xb06c})for(unsigned v:{0,1,2,0x8000,0xffff})if(!companion_counters[p].count(v))throw std::runtime_error("missing companion signed lifetime endpoint");
   for(uint32_t p:{0xafa2,0xb032})for(unsigned v:{0,4,8,12})if(!companion_rows[p].count(v))throw std::runtime_error("missing companion pitch row");
   for(unsigned v:{0,0xbf,0xc0,0xc1})if(!companion_counts.count(v))throw std::runtime_error("missing companion append capacity endpoint");
   for(uint32_t p:{0xafa2,0xb032})for(unsigned v:{0x29018,0x2939c})if(!companion_tables[p].count(v))throw std::runtime_error("missing companion runtime table choice");
  }
  std::cout<<"Companion coverage: "<<observed<<'/'<<main_companion_instructions.size()<<" PCs; "<<coverage[0x9c9e]<<" shadows, "<<coverage[0xaf3c]<<" primary exhausts, "<<coverage[0xafcc]<<" sibling exhausts, "<<coverage[0xb05c]<<" tails\n";
 }
 if(require_flight||require_flight_path){
  unsigned observed=0;
  for(const auto& i:main_flight_instructions){
   observed+=coverage[i.pc]!=0;
   if(graph_exit_pcs[i.pc]||external_return_pcs[i.pc])throw std::runtime_error("flight graph remains external at "+std::to_string(i.pc));
   if(require_flight&&!coverage[i.pc])throw std::runtime_error("missing flight instruction at "+std::to_string(i.pc));
  }
  unsigned returned_observed=0;
  for(const auto& i:main_flight_return_instructions){
   returned_observed+=coverage[i.pc]!=0;
   if(require_flight&&(!coverage[i.pc]||graph_exit_pcs[i.pc]||external_return_pcs[i.pc]))throw std::runtime_error("missing flight returned-instruction continuation");
  }
  std::cout<<"Flight returned-instruction continuations: "<<returned_observed<<'/'<<main_flight_return_instructions.size()<<" PCs observed\n";
  for(uint32_t p:{0x98c0,0x9ae0,0x9ae2,0x9c1e})if(!coverage[p])throw std::runtime_error("missing flight entry/projection/return");
  if(require_flight){
   for(const auto& i:main_flight_instructions)if(i.branch&&i.pc!=0x98f6&&i.pc!=0x9942&&i.pc!=0x996a&&i.pc!=0x99a6&&i.pc!=0x99f2&&i.pc!=0x9a2e){
    const auto& n=flight_branches[i.pc];std::cout<<"Flight branch "<<std::hex<<i.pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
    if(!n[0]||!n[1])throw std::runtime_error("missing flight branch direction at "+std::to_string(i.pc));
   }
   for(unsigned axis=0;axis<2;++axis)for(unsigned v:{0xfffb,0xfffc,0xfffd,0,3,4,5})if(!flight_slew[axis].count(v))throw std::runtime_error("missing flight slew endpoint");
   for(unsigned v:{0,0xbf,0xc0,0xc1})if(!flight_counts.count(v))throw std::runtime_error("missing flight append capacity endpoint");
   for(unsigned v:{0,0x40,0x80,0xc0,0xff})if(!flight_rolls.count(v))throw std::runtime_error("missing flight rotation quadrant");
   for(uint32_t p:{0xb628,0xb62e,0xb632,0xb636,0xb638,0xb63e,0xb640,0xb642,0xb644,0xb646,0xb648,0xb64a,0xb64e,0xb654})if(!coverage[p])throw std::runtime_error("missing flight append instruction");
   if(!branch_taken||!branch_untaken)throw std::runtime_error("missing flight append branch direction");
  }
  std::cout<<"Flight coverage: "<<observed<<'/'<<main_flight_instructions.size()<<" PCs; "<<coverage[0x98c0]<<" update entries, "<<coverage[0x9ae2]<<" projections; timed player/append execution\n";
 }
 if(require_input||require_input_path){
  if(!visibility.physical_buttons||!computed_raw_input_reads)throw std::runtime_error("missing computed input producers");
  unsigned observed=0;
  for(const auto& i:main_input_instructions){
   observed+=coverage[i.pc]!=0;
   if(graph_exit_pcs[i.pc]||external_return_pcs[i.pc])throw std::runtime_error("input graph remains external at "+std::to_string(i.pc));
   // Debug-mode front end is disabled by the production ROM word $B0AA.
   const bool debug=i.pc>=0xb2c0&&i.pc<=0xb2da;
   if(require_input&&!debug&&!coverage[i.pc])throw std::runtime_error("missing input instruction at "+std::to_string(i.pc));
  }
  for(uint32_t p:{0xb0ac,0xb368,0x14976,0x1498e,0xd0de})if(!coverage[p])throw std::runtime_error("missing input caller entry/return");
  if(require_input){
   for(const auto& i:main_input_instructions)if(i.branch&&i.pc!=0xb0da&&i.pc!=0xb0ee&&i.pc!=0xb150&&i.pc!=0xb182&&i.pc!=0xb1aa&&i.pc!=0xb1be&&i.pc!=0xb220&&i.pc!=0xb282&&i.pc!=0xb318&&i.pc!=0xb352&&i.pc!=0xb2bc&&i.pc!=0xb364&&!(i.pc>=0xb2c0&&i.pc<=0xb2da)){
    const auto& n=input_branches[i.pc];std::cout<<"Input branch "<<std::hex<<i.pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
    if(!n[0]||!n[1])throw std::runtime_error("missing input branch direction at "+std::to_string(i.pc));
   }
   for(unsigned v:{0,0x30,0x10,0x20})if(!physical_button_values.count(v))throw std::runtime_error("missing physical weapon button combination");
   for(unsigned v:{0x20,0x80,0xe0})if(!input_axis[0].count(v))throw std::runtime_error("missing X endpoint");
   for(unsigned v:{0x40,0x80,0xc0})if(!input_axis[1].count(v))throw std::runtime_error("missing Y endpoint");
   for(unsigned v:{0,0x80,0xff})if(!input_axis[2].count(v))throw std::runtime_error("missing throttle endpoint");
   for(unsigned axis=0;axis<2;++axis)for(unsigned v:{0,1,2,3,0xffff,0xfffe,0xfffd})if(!input_accum[axis].count(v))throw std::runtime_error("missing input deadband accumulator endpoint");
  }
  std::cout<<"Input coverage: "<<observed<<'/'<<main_input_instructions.size()<<" PCs; "<<coverage[0xb0ac]<<" axis entries, "<<coverage[0x14976]<<" button entries; computed input-to-target execution\n";
 }
 if(require_soundproducer||require_soundproducer_path){
  for(uint32_t p:{0xb84c,0xb850,0xb854,0xb85c,0xb860,0xb864,0xb868,0xb86c,0xb86e,0xb872,0xb876,0xb87a,0xb87c,0xb880,0xb884})
   if((require_soundproducer||(p!=0xb87c&&p!=0xb880))&&!coverage[p])throw std::runtime_error("missing sound producer instruction at "+std::to_string(p));
  for(auto [p,n]:graph_exit_pcs)if(p>=0xb84c&&p<=0xb884&&n)throw std::runtime_error("sound producer remains external");
  if(require_soundproducer){
   for(uint32_t p:{0xb850,0xb85c})if(!producer_branches[p][0]||!producer_branches[p][1])throw std::runtime_error("missing sound producer branch direction");
   if(!coverage[0xd0da]||graph_exit_pcs[0xd0da]||external_return_pcs[0xd0da])throw std::runtime_error("missing computed directed counter return");
   for(unsigned index:{62,63})if(!producer_indices.count(index))throw std::runtime_error("missing sound producer wrap endpoint");
   for(unsigned count:{0,0x7f,0xff})if(!producer_counts.count(count))throw std::runtime_error("missing sound producer count endpoint");
   for(unsigned command:{0,0x7f,0x80,0xff})if(!producer_commands.count(command))throw std::runtime_error("missing sound producer command endpoint");
  }
  unsigned producer_pcs=0;for(auto [p,n]:coverage)if(p>=0xb84c&&p<=0xb884&&n)++producer_pcs;
  std::cout<<"Sound producer coverage: "<<producer_pcs<<"/15 PCs; "<<coverage[0xb84c]<<" entries, "<<coverage[0xb87a]+coverage[0xb884]<<" returns; disabled branch taken/untaken="<<producer_branches[0xb850][1]<<'/'<<producer_branches[0xb850][0]<<" stage-one branch taken/untaken="<<producer_branches[0xb85c][1]<<'/'<<producer_branches[0xb85c][0]<<"; "<<coverage[0xd0da]<<" computed counter increments\n";
 }
 if(require_trail||require_trail_path){
  if(!coverage[0xaaee])throw std::runtime_error("missing trail entry");
  if(require_trail)for(uint32_t p:{0xaaee,0xaaf4,0xaafa,0xaafe,0xab02,0xab04,0xab06,0xab0a,0xab0e,0xab12,0xab1a,0xab1e,0xab22,0xab28,0xab2c,0xab30,0xab34,0xab36,0xab3a,0xab3e,0xab42,0xab44,0xab48,0xab4a,0xab4c,0xab52,0xab56,0xab58,0xab5c,0xab64,0xab68,0xab6a,0xab6e,0xab76,0xab7c,0xab80,0xab84,0xab8a,0xab8e,0xab96,0xab9a,0xab9c,0xab9e,0xaba0,0xaba2,0xaba6,0xabaa,0xabae,0xabb2,0xabb8,0xabbc,0xabbe,0xabc2,0xabc4,0xabc8,0xabca,0xabce,0xabd0,0xabd2,0xabd6,0xabde,0xabe4,0xabe8,0xabec,0xabf2,0xabf6,0xabfe,0xac02,0xac04,0xac06,0xac08,0xac0c,0xac10,0xac14,0xac1a,0xac1e,0xac20,0xac24,0xac26,0xac28,0xac2a,0xac2e,0xac30,0xac34,0xac36,0xac3a,0xac3e,0xac42,0xac46,0xac4a,0xae32,0xae36,0xae3a,0xae3e,0xae42,0xae44,0xae46,0xae48,0xae4e})
   if(!coverage[p])throw std::runtime_error("missing trail instruction at "+std::to_string(p));
  for(auto [p,n]:graph_exit_pcs)if(n&&((p>=0xaaee&&p<=0xac4a)||(p>=0xae32&&p<=0xae4e)))throw std::runtime_error("trail remains external");
  if(require_trail)for(uint32_t p:{0xab06,0xab28,0xabca,0xabd2,0xac2a,0xac36}){
   const auto& n=trail_branches[p];
   std::cout<<"Trail branch "<<std::hex<<p<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
   if(!n[0]||!n[1])throw std::runtime_error("missing trail branch direction at "+std::to_string(p));
  }
  std::cout<<"Trail coverage: computed projection, clipping, append and free-pool return"<<(require_trail?"; 99/99 PCs":"")<<'\n';
 }
 if(require_clear){
  for(uint32_t p:{0xb698,0xb69e,0xb6a4,0xb6a6,0xb6ac,0xb6ae,0xb6b4,0xb6b8,0xb6ba,0xb6bc,0xb6be,0xb6c0,0xb6c4,0xb6ce,0xb6d6,0xb6dc,0xb6e2,0xb6e4,0xb6ea,0xb6ee,0xb6f0,0xb6f2,0xb6f4,0xb6f6,0xb6fa,0xb704,0xb70a,0xb710,0xb712,0xb714,0xb71a,0xb720,0xb722,0xb728,0xb72e,0xb732,0xb734,0xb738,0xb73c,0xb740,0xb744,0xb748,0xb74c,0xb750,0xb754,0xb758,0xb762,0xb76a,0xb770,0xb776,0xb778,0xb77e,0xb782,0xb784,0xb788,0xb78c,0xb790,0xb794,0xb798,0xb79c,0xb7a0,0xb7a4,0xb7a8,0xb7b2,0xb7b8,0xb7be,0xb7c0})
   if(!coverage[p])throw std::runtime_error("missing sprite clear instruction at "+std::to_string(p));
  for(auto [p,n]:graph_exit_pcs)if(p>=0xb698&&p<=0xb7c0&&n)throw std::runtime_error("sprite clear remains external");
  std::cout<<"Sprite clear coverage: 67/67 PCs, full and strided RAM uploads\n";
 }
 if(require_allirq&&(!linked_irq4||!services4||!service2||!service6))throw std::runtime_error("missing linked IRQ level coverage");
 if(require_publication_init){
  if(!coverage[0xb66e]||graph_exit_pcs[0xb66e])throw std::runtime_error("missing computed publication initializer");
  std::cout<<"Publication initializer coverage: computed $B66E fallthrough\n";
 }
 if(require_worldinit){
  for(uint32_t p:{0x88c2,0x88ca,0x88ce,0x88d6,0x88da,0x88dc,0x88de,0x88e6,0x88ec,0x88f0,0x88f2,0x88f4,
     0x88f6,0x88fa,0x88fc,0x8900,0x8904,0x8908,0x890c,0x8912,0x8916,0x891e,0x8924,0x8928,0x892a,0x892c,
     0x892e,0x8932,0x8936,0x893a,0x8940,0x8944})
   if(!coverage[p])throw std::runtime_error("missing world initializer coverage at "+std::to_string(p));
  if(graph_exit_pcs[0x88ca]||graph_exit_pcs[0x8944])throw std::runtime_error("world initializer remains external");
  std::cout<<"World initializer coverage: 32/32 PCs, computed math-chip operands\n";
 }
 if(require_ready||require_ready_path){
  std::cout<<"Ready observed: bits="<<ready_bits[0]<<ready_bits[1]<<" branches="<<ready_taken<<'/'<<ready_untaken<<'\n';
  for(uint32_t p:{0xc082,0xc088,0xc090,0xc094,0xc098})
   if(!coverage[p])throw std::runtime_error("missing ready instruction coverage at "+std::to_string(p));
  if(require_ready&&(!ready_bits[0]||!ready_bits[1]||!ready_taken||!ready_untaken))throw std::runtime_error("missing ready bit/branch coverage");
  if(graph_exit_pcs[0xc082]||graph_exit_pcs[0xc098])throw std::runtime_error("ready graph remains external");
  std::cout<<"Ready coverage: 5/5 PCs, taken/untaken="<<ready_taken<<'/'<<ready_untaken<<"\n";
 }
 if(require_follower){
  for(uint32_t p:{0x7bb6,0x7bbc,0x7bc2,0x7bc8,0x7bce,0x7bd6,0x7bda,0x7be2,0x7be6,0x7bec,0x7bee,0x7bf4,0x7bf8,0x7bfc,0x7c00,0x7c02,0x7c06,0x7c0c,0x7c10})
   if(!coverage[p])throw std::runtime_error("missing follower instruction coverage at "+std::to_string(p));
  for(uint16_t stage:{1,7,2})if(!follower_stages.count(stage))throw std::runtime_error("missing follower stage coverage");
  for(uint8_t age:{0,4,8,0x80})if(!follower_ages.count(age))throw std::runtime_error("missing follower animation coverage");
  std::cout<<"Follower coverage: 19/19 instruction PCs, stages 1/7/2, animation 0/4/8/128\n";
 }
 for(auto [p,n]:external_return_pcs)std::cout<<"Unported returned PC "<<std::hex<<p<<std::dec<<": "<<n<<" boundaries\n";
 for(auto [p,n]:coverage)std::cout<<std::hex<<p<<std::dec<<": "<<n<<" instructions, "<<irqcoverage[p]<<" IRQ entries\n";
 return calls&&entries&&(!linked||((services+services4)&&resumptions))&&!service4_bad&&!bus4_bad&&!failed&&!bad&&!frontier_bad&&!autonomous_bad&&!service_bad&&!service_event_bad&&!selection_bad&&!baseline_bad&&baseline.empty()&&(!require_closed||external_returns==0)&&(!require_taken||(branch_taken&&branch_nv[0]&&branch_nv[3]))&&(!returns||(selections==services+services4&&(successors||back_to_back)))?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

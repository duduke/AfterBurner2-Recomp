#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <sstream>
#include <memory>
#include <cstring>
#include "hwmath/xboard_bus_arbiter.hpp"
#include "engine/mainbus.hpp"
namespace {
uint64_t number(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){auto c=f.get();if(c<0)throw std::runtime_error("truncated arbiter capture");v=v<<8|c;}return v;}
unsigned cpu(const std::string& tag){if(tag==":mainpcb:maincpu")return 0;if(tag==":mainpcb:subcpu")return 1;if(tag==":mainpcb:soundcpu")return 2;throw std::runtime_error("unexpected grant CPU");}
struct Phase{unsigned cpu;uint64_t ns;bool write;uint32_t a;uint16_t d,m;unsigned flags=0;};
struct Boost{uint64_t at,quantum,duration;};
struct Run{uint64_t start{},target{};unsigned budget{},ran{};bool present=false;std::vector<Boost> boosts;};
struct Group{uint64_t index{},base{},deadline{},quantum{},end{};bool outer=false;std::array<XBoardBusArbiter::Time,3> starts{};std::array<bool,3> enabled{};std::array<Run,3> runs;std::vector<Boost> before;};
}
int main(int argc,char** argv)try{
 std::ifstream f(argc>1?argv[1]:"build/oracle/bus_arbiter_short.bin",std::ios::binary);char magic[8];if(!f.read(magic,8)||(memcmp(magic,"AB2ARB03",8)&&memcmp(magic,"AB2ARB04",8)))throw std::runtime_error("invalid arbiter capture");
 const bool reset_tags=!memcmp(magic,"AB2ARB04",8);
 const bool boot=argc>3&&std::string(argv[3])=="--require-boot";
 MainBus::Memory memory; auto* ram=memory.bytes.data()+MainBus::SHARED;
 std::array<uint8_t,SubBus::SHARED_BYTES> initial{};
 if(!f.read(reinterpret_cast<char*>(initial.data()),initial.size()))throw std::runtime_error("missing initial shared RAM");
 if(boot){if(!std::all_of(initial.begin(),initial.end(),[](uint8_t v){return v==0;}))throw std::runtime_error("cold shared RAM differs from MAME zero initialization");}
 else std::copy(initial.begin(),initial.end(),ram);
 MainBus bus(nullptr,0,nullptr,0,memory);unsigned reads=0,read_bad=0,writes=0,shared_leads=0;std::array<uint64_t,SubBus::SHARED_BYTES> main_write_ns{};
 std::array<unsigned,2> vector_sequences{},vector_next{8,8};
 std::array<std::array<unsigned,3>,2> lane_writes{};
 std::array<unsigned,2> mirrors{};
 std::array<std::array<unsigned,2>,2> mirror_probes{},mirror_alias_masks{};
 std::map<uint64_t,std::vector<Phase>> phases_by_base;
 while(f.peek()!=EOF){auto index=number(f,8);number(f,8);auto count=number(f,4);auto& phases=phases_by_base[index];while(count--){Phase p;p.cpu=number(f,1)-1;p.ns=number(f,8);p.write=number(f,1);p.a=number(f,4);p.d=number(f,2);p.m=number(f,2);if(p.cpu>1)throw std::runtime_error("invalid program CPU");if(reset_tags){p.flags=number(f,1);if(p.flags>2)throw std::runtime_error("invalid phase flags");}phases.push_back(p);}}
 if(phases_by_base.empty())throw std::runtime_error("empty bus capture");
 std::ifstream trace(argc>2?argv[2]:"build/oracle/arbiter_schedule.txt");if(!trace)throw std::runtime_error("missing complete scheduler trace");
 uint64_t first_base=0; bool first_group=true;
 std::unique_ptr<XBoardBusArbiter> port;Group g;bool active=false,outer=false;unsigned current=0;
 std::vector<Boost> pending;unsigned failures=0,phase_bad=0,budget_bad=0,grant_bad=0,groups=0,grants=0,phases=0,leads=0,boosts=0,empty=0;unsigned cpu_masks=0;
 auto boost=[&](const Boost& b){if(b.quantum==XBoardBusArbiter::DEFAULT_QUANTUM)return;if(b.quantum!=320000000000ULL||b.duration!=100000000000000ULL)throw std::runtime_error("unexpected board quantum request");port->boost(b.at);++boosts;};
 auto finish=[&](){
  if(first_group){first_base=g.base;first_group=false;}
  if(!port)port=std::make_unique<XBoardBusArbiter>(XBoardBusArbiter::State{g.base,0,g.starts});
  for(auto b:g.before)boost(b);if(g.outer)port->timers_delivered();
  bool ok=port->state().base==g.base&&port->state().local==g.starts&&port->quantum()==g.quantum;
  // Quantum values are assertions only; expiry/renewal are native.
  auto it=phases_by_base.find(g.index);std::vector<Phase> none;auto& events=it==phases_by_base.end()?none:it->second;size_t cursor=0;uint64_t latest_main=0;
  if(events.empty())++empty;
  auto result=port->grant(g.deadline,[&](unsigned c,XBoardBusArbiter::Time target,unsigned budget){++grants;auto& r=g.runs[c];
   if(!r.present||target!=r.target||budget!=r.budget){++budget_bad;ok=false;if(budget_bad<5)std::cerr<<"Budget CPU="<<c<<" base="<<g.base<<" target="<<uint64_t(target)<<'/'<<r.target<<" cycles="<<budget<<'/'<<r.budget<<'\n';}
   r.present=false;for(auto b:r.boosts)boost(b);
   while(cursor<events.size()&&events[cursor].cpu==c){auto p=events[cursor++];++phases;if(c==0)latest_main=p.ns;else if(!(p.flags&2)&&latest_main>p.ns)++leads;
    if(p.flags&1){
     if(p.write||p.a!=0||p.m!=0xffff||vector_next[c]!=8)throw std::runtime_error("invalid reset entry tag");
     vector_next[c]=0;
    }
    if(vector_next[c]<8){
     if(p.write||p.a!=vector_next[c]||p.m!=0xffff)throw std::runtime_error("incomplete reset vector sequence");
     vector_next[c]+=2;if(vector_next[c]==8)++vector_sequences[c];
    }
    const bool shared=p.cpu==0?(p.a>=0x280000&&p.a<0x2c0000):(p.a>=0x80000&&p.a<0xc0000);
    if(shared){auto a=p.a&0xfffff;if(p.flags&2){if(p.write)throw std::runtime_error("write in read-only mirror probe");++mirror_probes[c][a>=0xa0000];mirror_alias_masks[c][a>=0xa0000]|=1u<<((a&0x1c000)>>14);}auto offset=(a<0xa0000?0:0x4000)+(a&0x3fff);
     if(a<0xa0000?(a&0x1c000)!=0x1c000:(a&0x1c000)!=0)++mirrors[c];
     if(p.m!=0xffff&&p.m!=0xff00&&p.m!=0xff)throw std::runtime_error("invalid shared byte lanes");
     if(p.write){
      ++lane_writes[c][p.m==0xffff?0:p.m==0xff00?1:2];
      if(c==0){if(p.m==0xffff)bus.write16(p.a,p.d);else bus.write8(p.a+(p.m==0xff),p.m==0xff?p.d:p.d>>8);}
      else {if(p.m==0xffff)bus.sub.write16(p.a,p.d);else bus.sub.write8(p.a+(p.m==0xff),p.m==0xff?p.d:p.d>>8);}
      ++writes;for(unsigned lane=0;lane<2;++lane)if(p.m&(lane?255:0xff00))main_write_ns[offset+lane]=p.cpu==0?p.ns:0;}else{++reads;if(!(p.flags&2)&&p.cpu==1&&(((p.m&0xff00)&&main_write_ns[offset]>p.ns)||((p.m&255)&&main_write_ns[offset+1]>p.ns)))++shared_leads;auto value=p.m==0xffff?(c==0?bus.read16(p.a):bus.sub.read16(p.a)):(c==0?bus.read8(p.a+(p.m==0xff)):bus.sub.read8(p.a+(p.m==0xff)))<<(p.m==0xff00?8:0);if((value&p.m)!=(p.d&p.m)){++read_bad;ok=false;if(read_bad<5)std::cerr<<"Arbitrated shared read CPU="<<p.cpu<<" a="<<std::hex<<a<<" value="<<value<<'/'<<p.d<<std::dec<<" ns="<<p.ns<<'\n';}}}
   }
   const auto cycle=c==2?XBoardBusArbiter::SOUND_CYCLE:XBoardBusArbiter::CYCLE;
   return port->state().local[c]+r.ran*cycle;
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
 std::cout<<"Arbitrated shared reads: "<<reads-read_bad<<" of "<<reads<<" identical, "<<read_bad<<" different, 0 skipped; "<<writes<<" explicit producer writes, "<<shared_leads<<" reads after a main write with a later local timestamp, 0 read assertions seeded\n";
 std::cout<<"Boot/shared edges: "<<vector_sequences[0]<<" main and "<<vector_sequences[1]<<" sub vector sequences; writes word/high/low main="<<lane_writes[0][0]<<'/'<<lane_writes[0][1]<<'/'<<lane_writes[0][2]<<" sub="<<lane_writes[1][0]<<'/'<<lane_writes[1][1]<<'/'<<lane_writes[1][2]<<"; noncanonical mirror phases="<<mirrors[0]<<'/'<<mirrors[1]<<"; labelled mirror probes="<<mirror_probes[0][0]<<'/'<<mirror_probes[0][1]<<'/'<<mirror_probes[1][0]<<'/'<<mirror_probes[1][1]<<'\n';
 if(boot&&(!reset_tags||first_base!=0||vector_sequences[0]!=1||vector_sequences[1]!=6||!shared_leads||mirror_probes[0][0]!=16||mirror_probes[0][1]!=16||mirror_probes[1][0]!=16||mirror_probes[1][1]!=16||mirror_alias_masks[0][0]!=255||mirror_alias_masks[0][1]!=255||mirror_alias_masks[1][0]!=255||mirror_alias_masks[1][1]!=255||!lane_writes[0][1]||!lane_writes[0][2]||!lane_writes[1][1]||!lane_writes[1][2]))throw std::runtime_error("missing cold boot/shared coverage");
 return failures||phase_bad||budget_bad||grant_bad||read_bad?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

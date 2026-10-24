#include "engine/xboard_host.hpp"
#include "hwaudio/xboard_sound_board.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <bit>
#include <cstring>
#include <map>
#include <set>
#include <charconv>

namespace {
using Host=XBoardHost<XBoardSoundBoard>;using Time=Host::Time;
uint64_t number(std::istream& f,unsigned n){uint64_t v=0;while(n--){const int c=f.get();if(c<0)throw std::runtime_error("truncated whole-host assertion");v=v<<8|c;}return v;}
void tag(std::istream& f,const char* expected){char magic[8];if(!f.read(magic,8)||memcmp(magic,expected,8))throw std::runtime_error(std::string("expected ")+expected);}
Time decode(uint64_t raw,Time reference){const Time period=Time(1)<<64,half=period/2;Time v=(reference&~(period-1))|raw;if(v+half<reference)v+=period;else if(v>reference+half&&v>=period)v-=period;return v;}
unsigned owner(const std::string& name){if(name==":mainpcb:maincpu")return 0;if(name==":mainpcb:subcpu")return 1;if(name==":mainpcb:soundcpu")return 2;throw std::runtime_error("unknown CPU owner");}
struct ExpectedPhase {Host::Phase p;Host::Entry e;bool entry{};};
struct SoundRecord {unsigned kind;uint64_t grant,ns;uint16_t pc,a;uint8_t d;};
struct Grant {Time target{},end{};unsigned budget{};bool present{};};
struct Group {uint64_t index{},quantum{};Time base{},deadline{},end{};bool outer{};std::array<Time,3> starts{};std::array<bool,3> enabled{};std::array<Grant,3> grants;std::vector<Time> boosts;};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main(int argc,char** argv)try{
 require(argc==3||argc==4,"usage: oracle_whole_host capture.bin scheduler.txt [integrated-width]");const std::string path=argv[1];
 unsigned integrated_width=0;
 if(argc==4){const std::string_view value(argv[3]);const auto parsed=std::from_chars(value.data(),value.data()+value.size(),integrated_width);
  require(parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()&&integrated_width>=320&&integrated_width<=640&&integrated_width%2==0,"invalid integrated width");}
 require(roms.load_aburner2(),"ROMs required");
 HWPalette palette(8192);HWRoad road(0x1700,0x1720,0x1780,-166);HWSprites sprites;HWTiles tiles;
 road.init(roms.road.rom,roms.road.length);sprites.init(roms.sprites.rom,roms.sprites.length);tiles.init_gfx(roms.tiles.rom,roms.tiles.length);
 XBoardSoundBoard sound({roms.soundcpu.rom,roms.soundcpu.length},{roms.pcm.rom,roms.pcm.length});
 Host host(roms,palette,road,sprites,tiles,sound);
 if(integrated_width){host.integrated_viewport(integrated_width);std::cout<<"Integrated whole-host renderer width="<<integrated_width<<'\n';}
 std::ifstream bus(path,std::ios::binary);tag(bus,"AB2ARB10");MainBus::Memory initial;
 require(bool(bus.read(reinterpret_cast<char*>(initial.bytes.data()),initial.bytes.size()))&&initial.bytes==host.memory.bytes,"initial cold RAM differs");
 for(unsigned i=0;i<32;++i){const auto actual=i<8?host.bus.mul.read(i):i<16?host.bus.div.read(i-8):i<24?host.bus.sub.mul.read(i-16):host.bus.sub.div.read(i-24);require(number(bus,2)==actual,"initial math differs");}number(bus,1);
 std::ifstream input(path+".inputs",std::ios::binary);tag(input,"AB2PIN03");for(auto& s:host.switches)s=number(input,1);
 while(input.peek()!=EOF){Host::Input v;v.ns=number(input,8);v.buttons=number(input,1);for(auto& a:v.analog)a=number(input,1);host.input(v);}
 std::ifstream banks(path+".video",std::ios::binary);tag(banks,"AB2VBN03");
 require(bool(banks.read(reinterpret_cast<char*>(initial.sprite_buffer.data()),4096))&&initial.sprite_buffer==host.memory.sprite_buffer,"initial sprite bank differs");
 require(bool(banks.read(reinterpret_cast<char*>(initial.road_buffer.data()),4096))&&initial.road_buffer==host.memory.road_buffer,"initial road bank differs");require(number(banks,1)==host.memory.road_control,"initial road control differs");
 std::array<std::set<uint32_t>,2> pcs;std::array<uint64_t,8> kinds{};unsigned io_owners=0;
 uint64_t bank_count=0,phase_count=0,entry_count=0,pixels=0,latches=0,frames=0,grants=0,timer_count=0,heads=0,sound_count=0,host_sound_count=0;
 auto check_bank=[&](unsigned kind,uint64_t ns,uint32_t pc){require(number(banks,1)==kind&&number(banks,8)==ns&&number(banks,4)==pc,"bank event frontier differs");const auto control=number(banks,1);
  if(kind==3)require(control==host.memory.road_control,"road control differs");
  else{std::array<uint8_t,4096> expected{};require(bool(banks.read(reinterpret_cast<char*>(expected.data()),4096)),"bank assertion missing");require(expected==(kind==2?host.memory.road_buffer:host.memory.sprite_buffer),"native bank bytes differ");}++bank_count;
 };
 std::ifstream pixel(path+".pixels",std::ios::binary),screen(path+".screen"),latch(path+".latches");require(pixel&&screen&&latch,"missing video assertions");
 host.latched=[&](uint64_t ns){uint64_t at;require(bool(latch>>at)&&at==ns,"latch frontier differs");for(auto v:tiles.frame_registers()){unsigned n;require(bool(latch>>n)&&n==v,"latch values differ");}++latches;};
 host.rendered=[&](uint64_t ns,unsigned top,unsigned bottom,bool display){
  uint64_t at;unsigned left,right,a,b,enabled;require(bool(screen>>at>>left>>right>>a>>b>>enabled)&&at==ns&&left==0&&right==319&&a==top&&b==bottom&&bool(enabled)==display,"screen frontier differs");
  require(number(pixel,8)==ns&&number(pixel,2)==top&&number(pixel,2)==bottom&&bool(number(pixel,1))==display,"pixel frontier differs");
  for(unsigned y=top;y<=bottom;++y)for(unsigned x=0;x<320;++x){const auto i=y*320+x;const auto index=number(pixel,2),priority=number(pixel,1),rgb=number(pixel,3);const auto color=host.pixels[i];
   require(rgb==unsigned(color.r<<16|color.g<<8|color.b)&&(!display||(index==host.compositor().indices()[i]&&priority==host.compositor().priorities()[i])),"whole-host RGB/index/priority differs");++pixels;}
 };
 host.frame=[&](uint64_t ns){check_bank(4,ns,0);++frames;};
 std::ifstream sound_in(path+".soundcpu",std::ios::binary);tag(sound_in,"AB2ZEX02");std::array<uint8_t,0x800> ram{};
 require(bool(sound_in.read(reinterpret_cast<char*>(ram.data()),ram.size()))&&ram==sound.cpu.ram,"initial sound RAM differs");
 std::vector<SoundRecord> records,host_records;uint64_t sound_end=0;
 while(sound_in.peek()!=EOF){const unsigned kind=number(sound_in,1);if(kind==255){sound_end=number(sound_in,8);break;}require(kind<=5,"invalid sound assertion kind");SoundRecord r{kind,number(sound_in,8),number(sound_in,8),uint16_t(number(sound_in,2)),uint16_t(number(sound_in,2)),uint8_t(number(sound_in,1))};(kind<4?records:host_records).push_back(r);}
 require(sound_end&&sound_in.peek()==EOF,"invalid sound assertion end");size_t sc=0,hc=0;
 sound.observer=[&](const auto& e,uint64_t ns){require(sc<records.size(),"extra sound transaction");const auto r=records[sc++];require(r.kind==unsigned(e.kind)&&r.grant==host.group&&r.ns==ns&&r.pc==e.pc&&r.a==e.address&&r.d==e.data,"whole-host sound CPU transaction differs");++sound_count;};
 auto check_sound_host=[&](const Host::Phase& p,unsigned kind,unsigned address,uint8_t data){require(hc<host_records.size(),"extra sound host transaction");const auto r=host_records[hc++];require(r.kind==kind&&r.grant==host.group&&r.ns==p.ns&&r.a==address&&r.d==data,"whole-host sound producer differs");++host_sound_count;};
 std::array<std::ifstream,2> audio;for(unsigned i=0;i<2;++i){audio[i].open(path+".audio"+(i?".pcm":".fm"),std::ios::binary);tag(audio[i],"AB2AST01");}
 std::ifstream speaker(path+".audio.speaker",std::ios::binary);tag(speaker,"AB2SPK01");std::array<uint64_t,2> samples{},batches{};uint64_t mixed=0,mix_batches=0;
 sound.audio.stream=[&](unsigned d,uint64_t ns,uint64_t first,const auto& out){auto& f=audio[d];require(number(f,8)==ns&&number(f,8)==first&&number(f,4)==out.size(),"audio batch frontier differs");for(auto v:out)require(int32_t(number(f,4))==v.left&&int32_t(number(f,4))==v.right,"raw audio sample differs");sound.speaker.append(d,first,out);samples[d]+=out.size();++batches[d];};
 sound.output=[&](uint64_t ns,const auto& out){require(number(speaker,8)==ns&&number(speaker,8)==mixed&&number(speaker,4)==out.size(),"mixed speaker frontier differs");for(auto v:out)for(float ch:v)require(number(speaker,4)==std::bit_cast<uint32_t>(ch),"mixed speaker float differs");mixed+=out.size();++mix_batches;};
 std::array<std::deque<std::pair<uint64_t,uint64_t>>,7> timers;
 {std::ifstream devices(path.substr(0,path.size()-4)+".devices");require(bool(devices),"missing device assertions");const char* names[]={"segaic16_video_device::tilemap_16b_latch_values","screen_device::first_scanline_tick","screen_device::vblank_begin","screen_device::vblank_end","sound_manager::update","ym_generic_device::fm_mode_write","ym_generic_device::fm_timer_handler"};Time previous=0;
  for(std::string line;std::getline(devices,line);){std::istringstream in(line);char kind;in>>kind;if(kind!='F')continue;uint64_t base,expiry;std::string name;require(bool(in>>base>>expiry>>name),"invalid device assertion");const auto b=decode(base,previous),e=decode(expiry,b);previous=b;for(unsigned i=0;i<7;++i)if(name==names[i])timers[i].emplace_back(uint64_t(b/1000000000),uint64_t(e/1000000000));}}
 auto check_timer=[&](unsigned owner,uint64_t ns,uint64_t base){auto& q=timers[owner];require(!q.empty()&&q.front()==std::pair(base,ns),"device callback frontier differs");q.pop_front();++timer_count;};
 host.devices.callback=[&](auto owner,uint64_t ns){check_timer(owner,ns,host.delivered_ns);if(owner==XBoardDeviceDeadlines::SoundUpdate)sound.update_speaker(ns);};
 sound.audio.timer_callback=[&](bool mode,unsigned,uint64_t ns,uint64_t base){check_timer(mode?5:6,ns,base);};
 uint64_t capture_group=0,last_capture=0;std::vector<ExpectedPhase> expected;size_t cursor=0;
 auto next_capture=[&]{expected.clear();if(bus.peek()==EOF){capture_group=0;return;}capture_group=number(bus,8);number(bus,8);const auto count=number(bus,4);require(capture_group>last_capture,"invalid phase grant index");last_capture=capture_group;expected.reserve(count);
  for(unsigned i=0;i<count;++i){ExpectedPhase v{};auto& p=v.p;p.cpu=number(bus,1)-1;p.ns=number(bus,8);p.write=number(bus,1);p.address=number(bus,4);p.data=number(bus,2);p.mask=number(bus,2);p.pc=number(bus,4);p.space=number(bus,1);v.e.kind=number(bus,1);v.entry=v.e.kind!=0;
   if(v.entry){auto& e=v.e;e.state.ns=number(bus,8);e.state.pc=number(bus,4);e.state.sr=number(bus,2);e.state.sp=number(bus,4);for(auto& r:e.state.registers.d)r=number(bus,4);for(auto& r:e.state.registers.a)r=number(bus,4);e.usp=number(bus,4);e.ssp=number(bus,4);e.bias=number(bus,8);}expected.push_back(v);}
 };next_capture();
 host.phase=[&](const Host::Phase& p){require(capture_group==host.group&&cursor<expected.size(),"native phase outside observed grant");const auto& v=expected[cursor++];const auto& e=v.p;
  if(p.cpu!=e.cpu||p.write!=e.write||p.address!=e.address||p.mask!=e.mask||p.pc!=e.pc||p.ns!=e.ns||p.space!=e.space||(p.data&p.mask)!=(e.data&e.mask)){
   std::cerr<<"Phase "<<phase_count<<" grant="<<host.group<<" CPU="<<p.cpu<<" pc="<<std::hex<<p.pc<<'/'<<e.pc<<" address="<<p.address<<'/'<<e.address<<" data="<<p.data<<'/'<<e.data<<std::dec<<" ns="<<p.ns<<'/'<<e.ns<<'\n';throw std::runtime_error("whole-host bus phase differs");}
  require(bool(p.entry)==v.entry,"instruction snapshot presence differs");
  if(p.entry){const auto& a=*p.entry;const auto& b=v.e;require(a.kind==b.kind&&a.usp==b.usp&&a.ssp==b.ssp&&a.bias==b.bias&&a.state.pc==b.state.pc&&a.state.sp==b.state.sp&&a.state.sr==b.state.sr&&a.state.ns==b.state.ns&&a.state.registers.d==b.state.registers.d&&a.state.registers.a==b.state.registers.a,"whole-host retained CPU state differs");++entry_count;++kinds[a.kind];if(a.kind==1)pcs[p.cpu].insert(a.state.pc);}
  if(p.cpu==0&&p.write&&p.address>=0x110000&&p.address<0x120000)check_bank(1,p.ns,p.pc);
  if(p.cpu==1&&p.address>=0xee000&&p.address<0xf0000){if(!p.write)check_bank(2,p.ns,p.pc);else if(p.mask&255)check_bank(3,p.ns,p.pc);}
  if(p.cpu==0&&p.write){if(p.address>=0x140000&&p.address<0x160000)io_owners|=1u<<((p.address>>16)-0x14);
   if(p.address>=0x140000&&p.address<0x150000&&(p.mask&255))check_sound_host(p,5,(p.address/2)&7,p.data&255);
   else if(p.address>=0xe8000&&p.address<0xec000&&(((p.address/2)&15)==11||((p.address/2)&15)==15))check_sound_host(p,4,0,p.data&255);}
  ++phase_count;
 };
 Group g{};std::vector<Time> observed_boosts;host.boosted=[&](Time at){observed_boosts.push_back(at);};
 host.granted=[&](unsigned c,Time target,unsigned budget,Time end){auto& r=g.grants[c];require(r.present&&target==r.target&&budget==r.budget&&end==r.end,"whole-host CPU grant differs");r.present=false;++grants;};
 std::ifstream trace(argv[2]);require(bool(trace),"missing scheduler assertions");Time previous=0;bool outer=false,active=false;std::vector<Time> before;
 auto finish=[&]{
  require(g.index==host.group+1&&g.base==host.arbiter.state().base&&g.starts==host.arbiter.state().local,"whole-host scheduler initial state differs");
  require(g.enabled==std::array<bool,3>{true,true,true},"unverified suspend mask");
  require((host.deadline()<=g.base)==g.outer,"whole-host timer delivery selection differs");host.deliver();
  require(host.arbiter.quantum()==g.quantum&&host.deadline()==g.deadline,"whole-host quantum/device head differs");++heads;
  require(!capture_group||capture_group>=g.index,"missing captured phase group");cursor=0;
  host.step();require(host.arbiter.state().base==g.end,"whole-host group completion differs");
  for(const auto& r:g.grants)require(!r.present,"missing native CPU grant");require(observed_boosts==g.boosts,"whole-host boost owner differs");observed_boosts.clear();
  if(capture_group==g.index){require(cursor==expected.size(),"unconsumed CPU phases");next_capture();}else require(cursor==0,"extra CPU phases");
 };
 for(std::string line;std::getline(trace,line);){std::istringstream in(line);char k;in>>k;
  if(k=='T'){uint64_t raw;in>>raw;previous=decode(raw,previous);outer=true;}
  else if(k=='G'){require(!active,"nested scheduler group");g={};uint64_t raw,deadline;in>>g.index>>raw>>deadline>>g.quantum;g.base=decode(raw,previous);previous=g.base;g.deadline=decode(deadline,g.base);g.outer=outer;outer=false;g.boosts=std::move(before);before.clear();active=true;}
  else if(k=='C'){std::string name;uint64_t raw;unsigned suspend,eat;in>>name>>raw>>suspend>>eat;const auto c=owner(name);g.starts[c]=decode(raw,g.base);g.enabled[c]=!suspend||eat;}
  else if(k=='Q'){uint64_t raw,q,d;in>>raw>>q>>d;if(q!=1000000000000000000ULL/6000){require(q==320000000000ULL&&d==100000000000000ULL,"unknown quantum assertion");(active?g.boosts:before).push_back(decode(raw,active?g.base:previous));}}
  else if(k=='R'){std::string name;uint64_t start,target;unsigned ran;in>>name>>start>>target;auto& r=g.grants[owner(name)];in>>r.budget>>ran;r.present=true;r.target=decode(target,g.base);r.end=decode(start,g.base)+Time(ran)*(owner(name)==2?Time(250000000000ULL):Time(80000000000ULL));}
  else if(k=='E'){uint64_t raw;in>>raw;g.end=decode(raw,g.base);previous=g.end;finish();active=false;}
  else throw std::runtime_error("unknown scheduler record");require(bool(in),"invalid scheduler assertion");
 }
 host.deliver();require(!active&&!capture_group&&host.group==last_capture,"incomplete whole-host capture");
 require(sc==records.size()&&hc==host_records.size(),"unconsumed sound transactions");
 for(auto& q:timers)require(q.empty(),"unconsumed device callbacks");for(auto& f:audio)require(f.peek()==EOF,"unconsumed audio batches");
 require(speaker.peek()==EOF&&pixel.peek()==EOF&&banks.peek()==EOF,"unconsumed speaker/video assertions");screen>>std::ws;latch>>std::ws;require(screen.peek()==EOF&&latch.peek()==EOF,"unconsumed screen/latch assertions");
 require(kinds[7]==1&&kinds[3]>0&&kinds[2]>0&&kinds[4]>0&&kinds[5]>0&&kinds[6]>0&&io_owners==3&&pixels&&latches&&mixed,"missing whole-host owner coverage");
 std::cout<<"Observed native instruction PCs: main="<<pcs[0].size()<<" sub="<<pcs[1].size()<<"; main IRQ2/4/6="<<kinds[4]<<'/'<<kinds[5]<<'/'<<kinds[6]<<"; sub IRQ="<<kinds[2]<<'\n';
 std::cout<<"Autonomous whole host: "<<host.group<<" groups, "<<grants<<" grants, "<<heads<<" native device heads, "<<timer_count<<" callbacks; "<<entry_count<<" retained CPU snapshots, "<<phase_count<<" bus phases; "<<bank_count<<" bank/control/frame assertions, "<<latches<<" latches, "<<frames<<" frames, "<<pixels<<" RGB pixels\n";
 std::cout<<"Joint audio: "<<sound_count<<" Z80 transactions, "<<host_sound_count<<" native host writes; FM="<<samples[0]<<"/"<<batches[0]<<", PCM="<<samples[1]<<"/"<<batches[1]<<" samples/batches; mixed="<<mixed<<" frames/"<<mix_batches<<" batches; "<<host.reset_deliveries<<" sub RESET deliveries, "<<host.reset_sequences<<" reset sequences\nZero supplied execution/state/read seeds; zero differences/skips within this finite capture.\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"Whole-host oracle: "<<e.what()<<'\n';return 1;}

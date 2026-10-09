#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <sstream>
#include <memory>
#include <deque>
#include <cstring>
#include "hwmath/xboard_bus_arbiter.hpp"
#include "hwmath/xboard_device_deadlines.hpp"
#include "engine/mainbus.hpp"
#include "engine/omainirqservice.hpp"
#include "engine/omainjointmanifest.hpp"
#include "engine/omainirqwrapperschedule.hpp"
#include "engine/osubirq.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/screen.hpp"
#include "hwmath/xboard_timeline.hpp"
#include "hwmath/xboard_irq_lines.hpp"
#include "engine/osubprogress_manifest.hpp"
#include "hwmath/xboard_cpu_continuation.hpp"
#include "roms.hpp"
#include "sound_execution_fixture.hpp"
namespace {
uint64_t number(std::ifstream& f,unsigned n){uint64_t v=0;while(n--){auto c=f.get();if(c<0)throw std::runtime_error("truncated arbiter capture");v=v<<8|c;}return v;}
unsigned cpu(const std::string& tag){if(tag==":mainpcb:maincpu")return 0;if(tag==":mainpcb:subcpu")return 1;if(tag==":mainpcb:soundcpu")return 2;throw std::runtime_error("unexpected grant CPU");}
struct Snapshot {uint32_t pc{},sp{};uint64_t ns{};uint16_t sr{};uint32_t usp{},ssp{};uint64_t bias{};unsigned kind{};OMainObjects::Registers registers;bool present=false;};
struct Phase{unsigned cpu;uint64_t ns;bool write;uint32_t a;uint16_t d,m;uint32_t pc;unsigned space{};Snapshot snapshot;};
bool mainprogress=false;
bool main_rte(uint32_t pc){return pc==0x1466a||pc==0x14672||pc==0x147c6||pc==0x147e6||pc==0x14842;}
bool publication_bounded(unsigned cpu,uint32_t pc){
 if(cpu==0){switch(pc){case 0x1458a:case 0x14590:case 0x14594:case 0x1459c:case 0x145a0:case 0x145a4:case 0x145a8:case 0x145ac:case 0x145b0:case 0x145b2:case 0x145b6:case 0x145b8:case 0x14856:case 0x1485c:case 0x1485e:case 0x14866:case 0x1486a:case 0x1486e:case 0x14876:case 0x14878:return true;}}
 if(cpu==1&&(sub_instruction(pc)||OSubIrq::startup_instruction(pc)))return true;
 if(cpu==1){switch(pc){case 0x420:case 0x428:case 0x55e:case 0x562:case 0x568:case 0x56e:case 0x570:case 0x574:case 0x578:case 0x57c:case 0x580:case 0x586:case 0x58a:case 0x58e:case 0x592:case 0x432:case 0x436:case 0x43a:case 0x43e:case 0x442:case 0x446:case 0x44a:case 0x44e:case 0x452:case 0x456:case 0x458:case 0x556:case 0x55a:case 0x55c:case 0x640:case 0x646:return true;}}
 return false;
}
bool bounded(unsigned cpu,uint32_t pc){return publication_bounded(cpu,pc)||(cpu==0&&mainprogress&&main_joint_instruction(pc));}
struct Boost{uint64_t at,quantum,duration;};
struct Run{uint64_t start{},target{};unsigned budget{},ran{};bool present=false;std::vector<Boost> boosts;};
struct Group{uint64_t index{},base{},deadline{},quantum{},end{};bool outer=false;std::array<XBoardBusArbiter::Time,3> starts{};std::array<bool,3> enabled{};std::array<Run,3> runs;std::vector<Boost> before;};
}
int main(int argc,char** argv)try{
 std::ifstream f(argc>1?argv[1]:"build/oracle/publication_schedule.bin",std::ios::binary);char magic[8];if(!f.read(magic,8)||(memcmp(magic,"AB2ARB07",8)&&memcmp(magic,"AB2ARB08",8)&&memcmp(magic,"AB2ARB09",8)))throw std::runtime_error("invalid arbiter capture");
 const bool mainentries=!memcmp(magic,"AB2ARB09",8);
 mainprogress=mainentries||!memcmp(magic,"AB2ARB08",8);
 bool require_video_rgb=false,require_video_fixture=false;
 bool native_sound=false,sound_attached=false,native_deadlines=false,require_speaker=false;
 bool reset_window=false,require_reset=false,require_main_irq=false,physical_main=false,require_main_closed=false,require_sub_continuity=false,require_video_banks=false,require_video_frontiers=false,require_sound_handshake=false,progress=false;
 for(int i=3;i<argc;++i){
  if(!std::strcmp(argv[i],"--require-video-fixture"))require_video_fixture=true;
  else if(!std::strcmp(argv[i],"--require-video-rgb")){require_video_rgb=true;require_video_frontiers=true;require_video_banks=true;physical_main=true;}
  else if(!std::strcmp(argv[i],"--reset-window")){reset_window=true;require_reset=true;}
  else if(!std::strcmp(argv[i],"--require-reset"))require_reset=true;
  else if(!std::strcmp(argv[i],"--physical-main"))physical_main=true;
  else if(!std::strcmp(argv[i],"--require-main-closed")){require_main_closed=true;physical_main=true;require_main_irq=true;}
  else if(!std::strcmp(argv[i],"--progress"))progress=true;
  else if(!std::strcmp(argv[i],"--require-sub-continuity"))require_sub_continuity=true;
  else if(!std::strcmp(argv[i],"--require-video-banks"))require_video_banks=true;
  else if(!std::strcmp(argv[i],"--require-video-frontiers")){require_video_frontiers=true;require_video_banks=true;physical_main=true;}
  else if(!std::strcmp(argv[i],"--require-sound-handshake"))require_sound_handshake=true;
  else if(!std::strcmp(argv[i],"--require-native-sound")){native_sound=true;require_sound_handshake=true;physical_main=true;}
  else if(!std::strcmp(argv[i],"--require-speaker")){require_speaker=true;}
  else if(!std::strcmp(argv[i],"--require-native-deadlines")){native_deadlines=true;native_sound=true;require_sound_handshake=true;physical_main=true;}
  else if(!std::strcmp(argv[i],"--require-main-irq"))require_main_irq=true;
  else throw std::runtime_error("unexpected joint acceptance option");
 }
 if(require_main_irq&&!mainentries)throw std::runtime_error("main IRQ acceptance requires a fresh AB2ARB09 capture");
 if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
 std::unique_ptr<SoundExecutionFixture> sound_cpu;
 if(native_sound)sound_cpu=std::make_unique<SoundExecutionFixture>((std::string(argv[1])+".soundcpu").c_str());
 if(native_deadlines)sound_cpu->enable_audio(std::string(argv[1])+".audio");
 if(require_speaker){if(!native_deadlines)throw std::runtime_error("speaker requires native deadlines");sound_cpu->enable_speaker(std::string(argv[1])+".audio");}
 MainBus::Memory memory;MainBus mainbus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);auto& bus=mainbus.sub;
 if(!f.read(reinterpret_cast<char*>(memory.bytes.data()),0x10000))throw std::runtime_error("missing initial work/shared RAM");
 if(mainprogress){
  if(!f.read(reinterpret_cast<char*>(memory.bytes.data()+0x10000),MainBus::MEMORY_BYTES-0x10000))throw std::runtime_error("missing initial video RAM");
  std::array<uint16_t,32> math;for(auto& v:math)v=number(f,2);
  mainbus.mul.load(math[0],math[1]);bus.mul.load(math[16],math[17]);
  for(unsigned base:{8u,24u}){uint16_t v[]={math[base],math[base+1],math[base+2],math[base+4],math[base+5],math[base+6]};if(base==8)mainbus.div.load(v);else bus.div.load(v);}
  const auto buttons=uint8_t(number(f,1));mainbus.io[1].input=[buttons](unsigned p){return p==0?buttons:uint8_t(0xff);};
 }
 unsigned reads=0,read_bad=0,writes=0,shared_leads=0,external_writes=0,native_shared_writes=0;std::array<uint64_t,SubBus::SHARED_BYTES> main_write_ns{};
 std::ifstream video(std::string(argv[1])+".video",std::ios::binary);unsigned video_banks=0,video_controls=0,video_frames=0;uint64_t video_start_ns=UINT64_MAX;
 HWSprites video_sprites;std::vector<uint16_t> sprite_pixels(320*224);
 if(video){char header[8];video.read(header,8);if(std::memcmp(header,"AB2VBN03",8))throw std::runtime_error("invalid joint video bank capture");
  video_sprites.init(roms.sprites.rom,roms.sprites.length);
  if(!video.read(reinterpret_cast<char*>(memory.sprite_buffer.data()),4096)||!video.read(reinterpret_cast<char*>(memory.road_buffer.data()),4096))throw std::runtime_error("missing initial video banks");
  memory.road_control=number(video,1);
 }else if(require_video_banks)throw std::runtime_error("missing joint video bank capture");
 HWPalette video_palette(8192); HWRoad video_road(0x1700,0x1720,0x1780,-166); HWTiles video_tiles;
 Screen video_screen(320,224,video_palette,video_road,video_sprites,video_tiles);
 std::vector<RGB888> video_rgb(320*224);
 std::ifstream pixel_capture(std::string(argv[1])+".pixels",std::ios::binary);
 struct LatchEvent{uint64_t ns;HWTiles::FrameRegisters values;};std::vector<LatchEvent> latch_events;size_t latch_cursor=0;
 {std::ifstream latches(std::string(argv[1])+".latches");LatchEvent e{};
  while(latches>>e.ns){for(auto& v:e.values){unsigned n;if(!(latches>>n)||n>65535)throw std::runtime_error("truncated video latch");v=n;}latch_events.push_back(e);}
 }
 if(require_video_rgb&&(!pixel_capture||latch_events.empty()))throw std::runtime_error("missing joint RGB/latch assertions");
 struct VideoFixture{uint64_t ns;bool display;std::vector<std::pair<unsigned,uint16_t>> words;};
 std::vector<VideoFixture> display_fixture;size_t fixture_cursor=0;
 {std::ifstream fixture(std::string(argv[1])+".fixture");uint64_t ns;unsigned enabled,count;
  while(fixture>>ns>>enabled>>count){VideoFixture e{ns,enabled!=0,{}};
   for(unsigned i=0;i<count;++i){unsigned offset,value;if(!(fixture>>offset>>value)||(!(offset<0x800||(offset>=0x1000&&offset<0x1800)||(offset>=0x2000&&offset<0x4000)))||value>65535)throw std::runtime_error("truncated directed video fixture");e.words.emplace_back(offset,value);}display_fixture.push_back(e);
  }
 }
 if(require_video_fixture&&display_fixture.empty())throw std::runtime_error("missing directed display fixture");
 const bool rgb_assertions=pixel_capture.is_open();uint64_t video_pixels=0;unsigned video_latches=0,video_disabled=0;
 if(rgb_assertions){video_road.init(roms.road.rom,roms.road.length);video_tiles.init_gfx(roms.tiles.rom,roms.tiles.length);}
 uint64_t effect_pixels=0,hilight_pixels=0;unsigned priority_masks=0,delayed_register_clips=0,row_scroll_clips=0,column_scroll_clips=0;
 auto video_text=[&]{for(unsigned i=0;i<0x800;++i)video_tiles.write_text(i,(memory.bytes[MainBus::TEXT+2*i]<<8)|memory.bytes[MainBus::TEXT+2*i+1]);};
 auto video_latch=[&](uint64_t t){
  if(!rgb_assertions||t<=video_start_ns)return;
  video_text();video_tiles.latch_frame_registers();
  if(latch_cursor>=latch_events.size()||latch_events[latch_cursor].ns!=t||latch_events[latch_cursor].values!=video_tiles.frame_registers())throw std::runtime_error("computed tile latch mismatch");
  ++latch_cursor;++video_latches;
 };
 auto video_compare_pixels=[&](uint64_t ns,unsigned top,unsigned bottom,bool display){
  if(!rgb_assertions)return;
  auto word=[&](unsigned offset){return uint16_t(memory.bytes[offset]<<8)|memory.bytes[offset+1];};
  video_text();HWTiles::FrameRegisters live{};
  for(unsigned i=0;i<4;++i){live[i]=video_tiles.read_text(0x740+i);live[4+i]=video_tiles.read_text(0x748+i);live[8+i]=video_tiles.read_text(0x74c+i);}
  const auto latched=video_tiles.frame_registers();
  if(display&&(latched[8]&0x8000||latched[9]&0x8000))++row_scroll_clips;
  if(display&&(latched[4]&0x8000||latched[5]&0x8000))++column_scroll_clips;
  if(display&&live!=video_tiles.frame_registers())++delayed_register_clips;
  for(unsigned i=0;i<0x8000;++i)video_tiles.write_tile(i,word(MainBus::TILE+2*i));
  for(unsigned i=0;i<8192;++i)video_palette.write(i,word(MainBus::PALETTE+2*i));
  for(unsigned i=0;i<0x800;++i)video_road.write16(2*i,(memory.road_buffer[2*i]<<8)|memory.road_buffer[2*i+1]);
  video_road.read_road_control();video_road.write_road_control(memory.road_control);
  video_screen.render_partial(video_rgb.data(),top,bottom,display,memory.sprite_buffer.data());
  for(;;){auto t=number(pixel_capture,8);auto a=number(pixel_capture,2),b=number(pixel_capture,2),enabled=number(pixel_capture,1);
   if(a>b||b>=224)throw std::runtime_error("invalid captured RGB clip");
   if(t<=video_start_ns){pixel_capture.seekg(320*(b-a+1)*6,std::ios::cur);continue;}
   if(t!=ns||a!=top||b!=bottom||bool(enabled)!=display)throw std::runtime_error("computed RGB frontier mismatch");
   for(unsigned y=top;y<=bottom;++y)for(unsigned x=0;x<320;++x){const auto index=number(pixel_capture,2),priority=number(pixel_capture,1),rgb=number(pixel_capture,3);const auto i=y*320+x;const auto color=video_rgb[i];const unsigned actual=(color.r<<16)|(color.g<<8)|color.b;
    if(rgb!=actual||(display&&(index!=video_screen.indices()[i]||priority!=video_screen.priorities()[i]))){std::cerr<<"RGB pixel ns="<<ns<<" xy="<<x<<','<<y<<" index="<<video_screen.indices()[i]<<'/'<<index<<" pri="<<unsigned(video_screen.priorities()[i])<<'/'<<priority<<" rgb="<<std::hex<<actual<<'/'<<rgb<<std::dec<<'\n';throw std::runtime_error("computed joint RGB pixel mismatch");}
    if(display){priority_masks|=1u<<video_screen.priorities()[i];if(index>=8192){++effect_pixels;if(word(MainBus::PALETTE+2*(index-8192))&0x8000)++hilight_pixels;}}
    ++video_pixels;
   }
   if(!display)++video_disabled;break;
  }
 };
 auto video_check=[&](unsigned kind,const Phase& p){
  if(!video.is_open()||!kind)return;
  const auto expected_kind=number(video,1),t=number(video,8),pc=number(video,4),control=number(video,1);
  bool ok=expected_kind==kind&&t==p.ns&&pc==p.pc;
  if(kind==3){ok&=control==memory.road_control;++video_controls;}
  else{std::array<uint8_t,4096> bytes{};if(!video.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("truncated joint video bank");
   const auto& actual=kind==2?memory.road_buffer:memory.sprite_buffer;
   for(unsigned i=0;i<bytes.size();++i)if(bytes[i]!=actual[i]){std::cerr<<"Video bank byte="<<i<<" data="<<unsigned(actual[i])<<'/'<<unsigned(bytes[i])<<'\n';ok=false;break;}if(kind==4)++video_frames;else ++video_banks;}
  if(!ok){std::cerr<<"Video bank phase kind="<<kind<<" pc="<<std::hex<<p.pc<<std::dec<<" ns="<<p.ns<<'\n';throw std::runtime_error("computed joint video bank mismatch");}
 };
 struct SoundEvent{uint64_t grant,ns;unsigned kind,pc,a,d;};std::vector<SoundEvent> sound_events;size_t sound_cursor=0;unsigned sound_acknowledges=0,sound_supplied=0;
 struct DrawEvent{uint64_t ns;unsigned left,right,top,bottom,display;};std::vector<DrawEvent> draw_events;size_t draw_cursor=0;bool drawing_assertions=false;
 {std::ifstream draw(std::string(argv[1])+".screen");drawing_assertions=draw.is_open();
  if(draw){DrawEvent e{};while(draw>>e.ns>>e.left>>e.right>>e.top>>e.bottom>>e.display)draw_events.push_back(e);}
  else if(require_video_frontiers)throw std::runtime_error("missing joint screen frontier capture");
 }
 {std::ifstream sound(std::string(argv[1])+".sound",std::ios::binary);
  if(sound){char header[8];sound.read(header,8);if(std::memcmp(header,"AB2SND01",8))throw std::runtime_error("invalid joint sound capture");
   while(sound.peek()!=EOF){SoundEvent e{};e.grant=number(sound,8);e.kind=number(sound,1);e.ns=number(sound,8);e.pc=number(sound,2);e.a=number(sound,2);e.d=number(sound,1);sound_events.push_back(e);}
  }else if(require_sound_handshake)throw std::runtime_error("missing joint sound capture");
 }
 std::vector<std::pair<uint64_t,uint8_t>> physical_buttons;
 std::vector<std::pair<uint64_t,std::array<uint8_t,8>>> physical_analog;
 if(mainprogress){std::ifstream input(std::string(argv[1])+".inputs",std::ios::binary);char header[8];
  if(!input.read(header,8)||(memcmp(header,"AB2PIN01",8)&&memcmp(header,"AB2PIN02",8)))throw std::runtime_error("missing joint physical inputs");
  const bool analog=!memcmp(header,"AB2PIN02",8);
  while(input.peek()!=EOF){auto t=number(input,8);auto b=uint8_t(number(input,1));if(!physical_buttons.empty()&&t<=physical_buttons.back().first)throw std::runtime_error("nonmonotonic physical inputs");physical_buttons.emplace_back(t,b);
   if(analog){std::array<uint8_t,8> values;for(auto& v:values)v=number(input,1);physical_analog.emplace_back(t,values);}
  }
  if(physical_buttons.empty())throw std::runtime_error("empty joint physical inputs");
 }
 XBoardCpuContinuation maincpu,subcpu;std::array<XBoardCpuContinuation*,2> native{&maincpu,&subcpu};
 std::array<bool,2> running{},linked{};std::array<OMainIrqService::Result,2> states{};
 OMainIrqService mainport;OSubIrq subport;
 OSubIrq::State substate{},interrupted{},live_sub{};bool in_irq=false,reset_ready=false;
 std::vector<uint64_t> reset_requests;unsigned reset_requests_sent=0,reset_deliveries=0,reset_sequences=0;
 std::function<void(uint64_t)> request_reset;
 uint32_t main_usp=0,main_ssp=0;uint64_t main_bias=0;bool main_bias_ready=false;
 unsigned main_returns=0,main_retained_returns=0,main_resumptions=0;bool main_return_pending=false;
 unsigned main_traps=0,main_startup_returns=0;
 std::array<unsigned,3> main_entry_levels{};
 unsigned main_pending=0,main_polls=0,main_selected_entries=0;
 unsigned main_entries=0,main_retained_entries=0,main_acknowledgements=0;
 unsigned sub_pending=0,irq_entries=0,retained_entries=0,irq_returns=0,sub_polls=0,acknowledgements=0;
 uint64_t delivered_ns=0,last_sub_poll=0,last_sub_delivery=0;unsigned last_sub_level=0;
 auto sub_level=[&](){constexpr auto first=XBoardTimeline::RESET_LINE0_NS+223*XBoardTimeline::LINE_NS;
  return delivered_ns>=first&&(delivered_ns-first)%XBoardTimeline::FRAME_NS<XBoardTimeline::LINE_NS?4u:0u;
 };
 std::array<unsigned,2> supplied_contexts{};
 unsigned sub_late_supplied=0;
 unsigned instructions=0,native_phases=0,native_writes=0,contexts=0,chains=0,state_bad=0,split_grants=0,bounded_returns=0,return_bad=0;
 std::map<std::pair<unsigned,uint32_t>,unsigned> coverage;
 std::map<std::pair<unsigned,uint32_t>,unsigned> supplied_phases,supplied_writes;
 std::map<uint32_t,std::array<unsigned,2>> branches,main_branches;
 std::function<void(unsigned,bool,uint32_t,uint16_t,uint16_t,uint32_t,uint64_t)> observe;
 MainBus::Trace maintrace;mainbus.trace=&maintrace;
 uint64_t main_phase_ns=0;unsigned native_space=0;
 bool video_display=false;
 auto video_vpos=[](uint64_t ns){return ((ns+80)/XBoardTimeline::LINE_NS+224)%262;}; // screen_device rounds to the nearest 160 ns pixel
 unsigned video_last_partial=224;uint64_t video_partial_reset=0;unsigned video_partial_updates=0;
 auto video_partial=[&](uint64_t ns,unsigned scanline){
  if(!video.is_open()||ns<=video_start_ns||scanline<video_last_partial||(video_last_partial==0&&video_partial_reset>ns))return;
  const auto bottom=std::min(scanline,223u);if(video_last_partial>bottom)return;
  if(drawing_assertions){while(draw_cursor<draw_events.size()&&draw_events[draw_cursor].ns<=video_start_ns)++draw_cursor;
   if(draw_cursor>=draw_events.size())throw std::runtime_error("unrecorded native screen update");const auto e=draw_events[draw_cursor++];
   if(e.ns!=ns||e.left!=0||e.right!=319||e.top!=video_last_partial||e.bottom!=bottom||e.display!=video_display){
    std::cerr<<"Video update phase ns="<<ns<<'/'<<e.ns<<" clip="<<video_last_partial<<'-'<<bottom<<'/'<<e.top<<'-'<<e.bottom<<" display="<<video_display<<'/'<<e.display<<'\n';throw std::runtime_error("computed screen frontier mismatch");}
  }
  if(rgb_assertions)video_compare_pixels(ns,video_last_partial,bottom,video_display);
  else if(video_display){std::fill(sprite_pixels.begin(),sprite_pixels.end(),0);video_sprites.render_bank(memory.sprite_buffer.data(),sprite_pixels.data(),320,224,video_last_partial,bottom);}
  video_last_partial=scanline+1;++video_partial_updates;
 };
 // CPU I/O and explicit device fixtures share the display transition policy.
 auto video_display_write=[&](uint64_t ns,bool enabled){
  if(enabled!=video_display)video_partial(ns,video_vpos(ns));
  video_display=enabled;
 };
 XBoardBusArbiter::Time device_deadline=0;
 // segaxbd_state::update_main_irqs queues input-line changes even when
 // clearing an already clear line. Synchronization aborts the executing
 // grant at the acknowledge clock, retaining the instruction step debt.
 XBoardTimeline main_board(mainbus.compare_timer);XBoardMainIrqLines main_lines;bool board_advancing=false;
 std::function<void(uint64_t)> native_boost;
 std::deque<std::pair<uint64_t,uint8_t>> sound_pending;unsigned sound_deliveries=0;
 unsigned analog_cursor=0;std::array<uint8_t,8> analog_values{};uint8_t mux=0;
 analog_values.fill(0x10);for(unsigned i=0;i<5;++i)analog_values[i]=0x80;
 main_board.adc.analog=[&]{return analog_values[(mux>>2)&7];};
 mainbus.endpoints.adc_read=[&]{return main_board.adc.read(main_board.now());};
 mainbus.endpoints.adc_write=[&](uint8_t){const bool starting=!main_board.adc.busy();main_board.adc.write(main_phase_ns);
  // emu_timer::adjust aborts the executing timeslice when the newly armed
  // conversion becomes the head timer, even under a shorter CPU quantum.
  if(starting&&running[0]&&XBoardBusArbiter::Time(main_board.adc.deadline())*1000000000ULL<device_deadline)maincpu.abort_grant(main_phase_ns);
 };
 mainbus.io[0].output=[&](unsigned p,uint8_t d,uint8_t){if(p==2){mux=d;const bool enabled=d&0x20;
  if(sound_attached)sound_cpu->queue_reset(main_phase_ns,!(d&1));
  video_display_write(main_phase_ns,enabled);if(running[0])maincpu.abort_grant(main_phase_ns);}};
 auto advance_board=[&](uint64_t ns,bool inclusive=true){
  board_advancing=true;
  while(analog_cursor<physical_analog.size()&&physical_analog[analog_cursor].first<=ns){
   const auto& input=physical_analog[analog_cursor++];
   main_board.advance_before(input.first);analog_values=input.second;
  }
  if(inclusive)main_board.advance(ns);else main_board.advance_before(ns);
  while(!sound_pending.empty()&&(inclusive?sound_pending.front().first<=ns:sound_pending.front().first<ns)){
   if(sound_attached)sound_cpu->external_time(sound_pending.front().first);
   mainbus.compare_timer.deliver_sound(sound_pending.front().second);sound_pending.pop_front();++sound_deliveries;
  }
  board_advancing=false;
 };
 mainbus.compare_timer.irq_line=[&](bool){
  if(physical_main)main_lines.update(mainbus.compare_timer.state().irq,main_board.vblank(),board_advancing?main_board.now():main_phase_ns);
  if(native_boost&&main_board.main_irq_level())native_boost(board_advancing?main_board.now():main_phase_ns);
  if(!board_advancing&&running[0])maincpu.abort_grant(main_phase_ns);
 };
 // 5250 sound writes synchronize at the CPU transaction clock and abort
 // the current grant. Sound CPU execution remains outside this CPU proof.
 mainbus.compare_timer.schedule_sound=[&](uint8_t value){sound_pending.emplace_back(main_phase_ns,value);if(running[0])maincpu.abort_grant(main_phase_ns);};
 main_board.scanline=[&](uint64_t t,unsigned line){
  if(line==223||line==224){main_lines.update(mainbus.compare_timer.state().irq,main_board.vblank(),t);if(native_boost&&main_board.main_irq_level())native_boost(t);}
  main_lines.deliver(t);
  if(fixture_cursor<display_fixture.size()&&display_fixture[fixture_cursor].ns==t){
   const auto& fixture=display_fixture[fixture_cursor++];
   video_display_write(t,fixture.display);
   for(auto [offset,value]:fixture.words){
    if(offset<0x800){video_partial(t,line);memory.bytes[MainBus::TEXT+2*offset]=value>>8;memory.bytes[MainBus::TEXT+2*offset+1]=value;}
    else if(offset<0x2000){const auto i=2*(offset-0x1000);memory.sprite_buffer[i]=value>>8;memory.sprite_buffer[i+1]=value;}
    else{const auto i=MainBus::PALETTE+2*(offset-0x2000);memory.bytes[i]=value>>8;memory.bytes[i+1]=value;}
   }
  }
  if(line==261)video_latch(t);
  if(line==0){video_last_partial=0;video_partial_reset=t;}
  if(line==224&&video.is_open()&&t>video_start_ns){
   video_partial(t,223);
   Phase p{};p.ns=t;video_check(4,p);
  }
 };
 if(physical_main){
  std::ifstream boot(std::string(argv[1])+".irqboot",std::ios::binary);char tag[8];
  if(!boot.read(tag,8)||(memcmp(tag,"AB2IRQ01",8)&&memcmp(tag,"AB2IRQ02",8)))throw std::runtime_error("missing physical main boot ports");
  board_advancing=true;
  for(;;){const auto base=number(boot,8),ns=number(boot,8);
   if(base==UINT64_MAX){advance_board(ns,false);main_lines.deliver(ns-1);video_start_ns=ns;
    // The initial RAM/banks are sampled by the frame notifier after the
    // visible frame has rendered, so its partial-render cursor is complete.
    if(rgb_assertions){
     while(latch_cursor<latch_events.size()&&latch_events[latch_cursor].ns<=ns)++latch_cursor;
     if(!latch_cursor)throw std::runtime_error("missing initial tile latch boundary");
     video_tiles.load_initial_frame_registers(latch_events[latch_cursor-1].values);
    }
    video_last_partial=224;break;}
   const bool w=number(boot,1);const auto a=number(boot,4);const auto d=number(boot,2),m=number(boot,2);
   advance_board(base);main_lines.deliver(base);main_phase_ns=ns;board_advancing=false;
   if(w){if(m==0xffff)mainbus.write16(a,d);else if(m==255)mainbus.write8(a+1,d);else if(m==0xff00)mainbus.write8(a,d>>8);else throw std::runtime_error("invalid boot port mask");}
   else mainbus.compare_timer.read((a&31)/2);
   board_advancing=true;
  }
  board_advancing=false;
 }
 auto main_poll=[&](uint64_t t){maincpu.clock(t);++main_polls;return physical_main?main_lines.level():0u;};
 if(mainprogress)mainbus.io[1].input=[&](unsigned p)->uint8_t{
  if(p!=0)return 0xff;
  auto i=std::upper_bound(physical_buttons.begin(),physical_buttons.end(),delivered_ns,[](uint64_t t,const auto& v){return t<v.first;});
  return i==physical_buttons.begin()?physical_buttons.front().second:std::prev(i)->second;
 };
 maintrace.before_access=[&](bool w,uint32_t a,uint16_t){
  // segaic16_video_device::textram_w renders before merging register RAM.
  if(w&&a>=0xd0000&&a<0xe0000&&(a&0xfff)>=0xe80)video_partial(main_phase_ns,video_vpos(main_phase_ns));
 };
 maintrace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m){observe(0,w,a,d,m,mainport.current_pc(),main_phase_ns);};
 auto begin=[&](const Phase& p){
  const auto& snap=p.snapshot;if(!snap.present||(snap.kind<2&&snap.pc!=p.pc))throw std::runtime_error("missing native entry snapshot");
  auto state=OMainIrqService::Result{snap.registers,snap.pc,snap.sp,snap.sr,snap.ns};
  // All CPU phases are captured. Only an actual phase from unported work
  // cuts continuity; disagreement with predicted time/PC cannot create a cut.
  if(linked[p.cpu]){
   ++chains;auto& r=states[p.cpu];if(r.ns!=state.ns||r.pc!=state.pc||r.sp!=state.sp||r.sr!=state.sr||r.registers.d!=state.registers.d||r.registers.a!=state.registers.a){++state_bad;std::cerr<<"Retained state CPU="<<p.cpu<<" pc="<<std::hex<<p.pc<<" sr="<<r.sr<<'/'<<state.sr<<" sp="<<r.sp<<'/'<<state.sp<<std::dec<<" ns="<<state.ns<<'\n';for(unsigned i=0;i<8;++i)if(r.registers.d[i]!=state.registers.d[i])std::cerr<<" D"<<i<<"="<<std::hex<<r.registers.d[i]<<'/'<<state.registers.d[i]<<std::dec<<'\n';for(unsigned i=0;i<7;++i)if(r.registers.a[i]!=state.registers.a[i])std::cerr<<" A"<<i<<"="<<std::hex<<r.registers.a[i]<<'/'<<state.registers.a[i]<<std::dec<<'\n';throw std::runtime_error("retained native instruction state mismatch");}state=r;
  }else {++contexts;++supplied_contexts[p.cpu];}
  if(p.cpu==1&&linked[1]&&(substate.usp!=snap.usp||substate.ssp!=snap.ssp||substate.cycle_bias!=snap.bias)){
   std::cerr<<"Sub banks pc="<<std::hex<<p.pc<<" usp="<<substate.usp<<'/'<<snap.usp<<" ssp="<<substate.ssp<<'/'<<snap.ssp<<std::dec<<" bias="<<substate.cycle_bias<<'/'<<snap.bias<<" kind="<<snap.kind<<'\n';
   throw std::runtime_error("retained sub stack banks or E-clock mismatch");}
  if(p.cpu==1&&snap.kind==2&&linked[1]){if(sub_pending!=4){std::cerr<<"Sub IRQ selection pc="<<std::hex<<p.pc<<" ret="<<state.pc<<" sr="<<state.sr<<std::dec<<" entry="<<state.ns<<" poll="<<last_sub_poll<<" delivered="<<last_sub_delivery<<" level="<<last_sub_level<<" now="<<delivered_ns<<"\n";throw std::runtime_error("joint sub IRQ without computed selection");}++retained_entries;}
  if(p.cpu==1&&snap.kind!=2&&linked[1]&&sub_pending)throw std::runtime_error("joint sub selection missed IRQ entry");
  if(p.cpu==1&&snap.kind==3&&!reset_ready)throw std::runtime_error("sub RESET without computed main producer");
  if(p.cpu==0){
   if(!main_bias_ready){main_bias=snap.bias;main_bias_ready=true;}
   else if(main_bias!=snap.bias)throw std::runtime_error("main E-clock origin changed without RESET");
   if(main_return_pending&&linked[0])++main_resumptions;main_return_pending=false;
   if(linked[0]&&main_rte(state.pc)&&state.pc!=0x14842)++main_retained_returns;
   if(linked[0]){if(main_usp!=snap.usp||main_ssp!=snap.ssp)throw std::runtime_error("retained main stack banks mismatch");}
   else {main_usp=snap.usp;main_ssp=snap.ssp;}
  }
  if(p.cpu==0&&physical_main&&linked[0]){
   if(snap.kind>=4){if(main_pending!=(snap.kind-3)*2){std::cerr<<"Main selection pc="<<std::hex<<p.pc<<std::dec<<" pending="<<main_pending<<" expected="<<(snap.kind-3)*2<<" delivered="<<delivered_ns<<"\n";throw std::runtime_error("main IRQ without computed physical selection");}++main_selected_entries;}
   else if(main_pending)throw std::runtime_error("main physical selection missed IRQ entry");
  }
  if(p.cpu==0&&snap.kind>=4&&linked[0])++main_retained_entries;
  ++instructions;++coverage[{p.cpu,(snap.kind==2||snap.kind>=4)?0xffffffff:p.pc}];running[p.cpu]=true;
  if(p.cpu==0)maincpu.start(state.ns,[&,state,snap,buspc=p.pc]{
   OMainIrqService::Context c{};c.registers=state.registers;c.boundary_ns=state.ns;c.interrupted_pc=state.pc;c.supervisor_sp=state.sp;c.sr=state.sr;
   if(snap.kind>=4){
    c.interrupted_pc=buspc;c.return_pc=state.pc;c.supervisor_sp=main_ssp;c.level=physical_main&&linked[0]?main_pending:(snap.kind-3)*2;++main_entries;++main_entry_levels[c.level/2-1];main_pending=0;
    states[0]=mainport.enter_joint(mainbus,c,[&](uint64_t t){maincpu.clock(t);main_phase_ns=t;},main_bias,[&](uint64_t ready){
     maincpu.hold_until(ready);native_space=1;observe(0,false,0xfffff0+c.level*2,24+c.level,0xffff,buspc,ready);native_space=0;
     maincpu.bus_completed(ready,5*OMainIrqService::CYCLE_NS);++main_acknowledgements;
    },[&](uint16_t sr,uint64_t t){const auto level=main_poll(t);main_pending=OMainIrqService::eligible(level,sr)?level:0;});
   }else if(state.pc==0x138ba){auto trapped=mainport.trap_joint(mainbus,state,main_ssp,[&](uint64_t t){maincpu.clock(t);main_phase_ns=t;},main_poll);states[0]=trapped.state;main_pending=trapped.irq;++main_traps;
   }else if(main_rte(state.pc)){
    auto returned=mainport.return_joint(mainbus,state,main_usp,[&](uint64_t t){maincpu.clock(t);main_phase_ns=t;},main_poll);states[0]=returned.state;main_pending=returned.irq;
    main_ssp=state.sp+6;if(state.pc==0x14842)++main_startup_returns;else ++main_returns;main_return_pending=state.pc!=0x14842;
   }else {auto instruction=mainport.caller_step(mainbus,c,[&](uint64_t t){maincpu.clock(t);main_phase_ns=t;},main_poll,request_reset);states[0]=instruction.state;main_pending=instruction.irq;}
   if(state.pc==0x145a8||state.pc==0x145b6||state.pc==0x1485c||state.pc==0x14866){
    const auto length=state.pc==0x145a8||state.pc==0x14866?4:2;
    ++main_branches[state.pc][states[0].pc!=state.pc+length];
   }
   if(mainprogress)if(const auto* i=main_irqwrapper_instruction(state.pc);i&&i->branch)
    ++main_branches[state.pc][states[0].pc!=state.pc+i->branch];
   if(states[0].sr&0x2000)main_ssp=states[0].sp;else main_usp=states[0].sp;
   maincpu.instruction_completed(states[0].ns);
  });
  else {
   OSubIrq::State c{};c.d=state.registers.d;c.a=state.registers.a;c.pc=state.pc;c.sp=state.sp;c.sr=state.sr;c.ns=state.ns;c.usp=snap.usp;c.ssp=snap.ssp;c.cycle_bias=snap.bias;
   if(linked[1])c=substate;
   const bool entry=snap.kind==2,reset=snap.kind==3;const auto buspc=p.pc;
   live_sub=c;
   subcpu.start(state.ns,[&,c,entry,reset,buspc]{
    auto clock=[&](uint64_t t){subcpu.clock(t);};
    auto phase=[&](bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t t){observe(1,w,a,d,m,pc,t);};
    OSubIrq::State end;
    if(reset){reset_ready=false;++reset_sequences;end=subport.reset(bus,c,buspc,clock,phase);}
    else if(entry){if(in_irq)throw std::runtime_error("nested joint sub IRQ");interrupted=c;in_irq=true;++irq_entries;sub_pending=0;end=subport.enter(bus,c,buspc,clock,phase,[&](uint64_t ready){subcpu.hold_until(ready);native_space=1;
     observe(1,false,0xfffff8,0x18+4,0xffff,buspc,ready);native_space=0;
     subcpu.bus_completed(ready,5*OSubPublication::CYCLE_NS);++acknowledgements;});}
    else {
     auto instruction=subport.instruction(bus,c,clock,phase,[&](uint64_t t){subcpu.clock(t);++sub_polls;last_sub_poll=t;last_sub_delivery=delivered_ns;last_sub_level=sub_level();return last_sub_level;},[&](const OSubIrq::State& partial){live_sub=partial;});
     end=instruction.state;sub_pending=instruction.irq;
     if(c.pc==0x592){
      if(!in_irq||end.pc!=interrupted.pc||end.sp!=interrupted.sp||end.sr!=interrupted.sr||end.d!=interrupted.d||end.a!=interrupted.a||end.ssp!=interrupted.ssp)
       {std::cerr<<"Sub RTE restore pc="<<std::hex<<end.pc<<'/'<<interrupted.pc<<" sp="<<end.sp<<'/'<<interrupted.sp<<" sr="<<end.sr<<'/'<<interrupted.sr<<std::dec<<"\n";throw std::runtime_error("joint sub RTE did not restore interrupted state");}
      ++irq_returns;in_irq=false;
     }
     if(const auto* m=sub_instruction(c.pc);m&&m->branch){
      const auto length=m->branch==-2?4:unsigned(m->branch);
      ++branches[c.pc][end.pc!=c.pc+length];
     }else if(c.pc==0x56e||c.pc==0x574||c.pc==0x57c||c.pc==0x58a){
      const auto length=c.pc==0x56e?2:4;
      ++branches[c.pc][end.pc!=c.pc+length];
     }
    }
    substate=end;states[1]={{end.d,end.a},end.pc,end.sp,end.sr,end.ns};subcpu.instruction_completed(end.ns);
   });
  }
 };
 // Stream one grant group. A full gameplay corpus contains tens of millions
 // of phases; retaining the entire capture would add gigabytes of unrelated
 // future oracle state and force the long acceptance run into swap.
 uint64_t capture_group=0,last_capture_group=0;bool capture_pending=false;
 std::vector<Phase> captured_phases;
 auto next_capture=[&](){
  captured_phases.clear();capture_pending=f.peek()!=EOF;if(!capture_pending)return;
  capture_group=number(f,8);number(f,8);auto count=number(f,4);
  if(capture_group<=last_capture_group)throw std::runtime_error("nonmonotonic captured grant");
  last_capture_group=capture_group;captured_phases.reserve(count);
  while(count--){Phase p;p.cpu=number(f,1)-1;p.ns=number(f,8);p.write=number(f,1);p.a=number(f,4);p.d=number(f,2);p.m=number(f,2);p.pc=number(f,4);p.space=number(f,1);p.snapshot.kind=number(f,1);p.snapshot.present=p.snapshot.kind!=0;
   if(p.cpu>1||p.snapshot.kind>(mainentries?6u:3u)||(p.cpu==1&&p.snapshot.kind>3)||(p.cpu==0&&(p.snapshot.kind==2||p.snapshot.kind==3)))throw std::runtime_error("invalid instruction snapshot kind or CPU");
   if(p.snapshot.present){auto& v=p.snapshot;v.ns=number(f,8);v.pc=number(f,4);v.sr=number(f,2);v.sp=number(f,4);for(auto& r:v.registers.d)r=number(f,4);for(auto& r:v.registers.a)r=number(f,4);v.usp=number(f,4);v.ssp=number(f,4);v.bias=number(f,8);}
   captured_phases.push_back(p);
  }
 };
 next_capture();if(!capture_pending)throw std::runtime_error("empty bus capture");
 std::ifstream trace(argc>2?argv[2]:"build/oracle/publication_schedule.txt");if(!trace)throw std::runtime_error("missing complete scheduler trace");
 std::unique_ptr<XBoardBusArbiter> port;Group g;bool active=false,outer=false;unsigned current=0;
 std::vector<Boost> pending;unsigned failures=0,phase_bad=0,budget_bad=0,grant_bad=0,groups=0,grants=0,phases=0,leads=0,boosts=0,empty=0;unsigned cpu_masks=0;
 auto boost=[&](const Boost& b){if(b.quantum==XBoardBusArbiter::DEFAULT_QUANTUM)return;if(b.quantum!=320000000000ULL||b.duration!=100000000000000ULL)throw std::runtime_error("unexpected board quantum request");port->boost(b.at);++boosts;};
 XBoardDeviceDeadlines device_timers;uint64_t native_heads=0;
 struct TimerAssertion{uint64_t delivered,expiry;};
 std::array<std::deque<TimerAssertion>,XBoardDeviceDeadlines::Count+2> timer_assertions;
 if(native_deadlines){
  const auto path=std::string(argv[1]);std::ifstream devices(path.substr(0,path.size()-4)+".devices");
  if(!devices)throw std::runtime_error("missing per-owner device callback assertions");
  const char* names[]={"segaic16_video_device::tilemap_16b_latch_values","screen_device::first_scanline_tick","screen_device::vblank_begin","screen_device::vblank_end","sound_manager::update","ym_generic_device::fm_mode_write","ym_generic_device::fm_timer_handler"};
  for(std::string line;std::getline(devices,line);){std::istringstream in(line);char kind;in>>kind;if(kind!='F')continue;
   uint64_t base,expiry;std::string name;if(!(in>>base>>expiry>>name))throw std::runtime_error("malformed device callback assertion");
   for(unsigned i=0;i<timer_assertions.size();++i)if(name==names[i])timer_assertions[i].push_back({base/1000000000ULL,expiry/1000000000ULL});
  }
 }
 if(native_deadlines)sound_cpu->audio->timer_callback=[&](bool mode,unsigned,uint64_t ns,uint64_t base){
  auto& expected=timer_assertions[XBoardDeviceDeadlines::Count+(mode?0:1)];
  if(expected.empty()||expected.front().expiry!=ns||expected.front().delivered!=base)throw std::runtime_error("native per-owner timer callback differs");
  expected.pop_front();
 };
 device_timers.callback=[&](XBoardDeviceDeadlines::Owner owner,uint64_t ns){
  if(!native_deadlines)return;
  auto& expected=timer_assertions[owner];
  if(expected.empty()||expected.front().expiry!=ns||expected.front().delivered!=delivered_ns)throw std::runtime_error("native per-owner timer callback differs");
  expected.pop_front();
  if(owner==XBoardDeviceDeadlines::SoundUpdate)sound_cpu->update_speaker(ns);
 };
 bool owns_boost=false;std::vector<Boost> produced_boosts;
 native_boost=[&](uint64_t ns){if(owns_boost){Boost b{ns*1000000000ULL,320000000000ULL,100000000000000ULL};boost(b);produced_boosts.push_back(b);}};
 auto next_device=[&](){
  auto ns=std::min({device_timers.deadline(),main_board.next_scanline(),main_board.adc.deadline(),main_lines.deadline(),sound_cpu->deadline()});
  if(!reset_requests.empty())ns=std::min(ns,reset_requests.front());
  if(!sound_pending.empty())ns=std::min(ns,sound_pending.front().first);
  return XBoardBusArbiter::Time(ns)*1000000000ULL;
 };
 auto finish=[&](){
  if(!port)port=std::make_unique<XBoardBusArbiter>(XBoardBusArbiter::State{g.base,0,g.starts});
  const bool owns_deadlines=native_deadlines&&g.base>=video_start_ns*XBoardBusArbiter::Time(1000000000ULL);
  owns_boost=owns_deadlines;produced_boosts.clear();
  const bool deliver=owns_deadlines?next_device()<=port->state().base:g.outer;
  if(owns_deadlines&&deliver!=g.outer)throw std::runtime_error("native outer timer delivery differs");
  if(!owns_boost)for(auto b:g.before)boost(b);if(deliver)port->timers_delivered();
  delivered_ns=uint64_t(port->state().base)/1000000000ULL;
  if(deliver){
   if(physical_main&&delivered_ns>=main_board.now()){advance_board(delivered_ns);main_lines.deliver(delivered_ns);}
   if(!reset_requests.empty()&&reset_requests.front()<=delivered_ns){
    if(!running[1]&&!linked[1])throw std::runtime_error("RESET crosses unported sub execution");
    const auto reset_ns=uint64_t(port->state().local[1])/1000000000ULL;
    subcpu.reset(reset_ns);substate=live_sub;substate.ns=reset_ns;
    // device_execute_interface::interface_pre_reset clears total_cycles;
    // local_time stays at this grant frontier, rephasing the VPA E clock.
    substate.cycle_bias=reset_ns/80;
    states[1]={{substate.d,substate.a},substate.pc,substate.sp,substate.sr,reset_ns};
    running[1]=false;linked[1]=true;sub_pending=0;in_irq=false;reset_ready=true;
    // interface_post_reset resets the input-line queues while the first
    // ASSERT/CLEAR pair is being drained. Later queued pulses disappear.
    reset_requests.clear();++reset_deliveries;
   }
  }
  bool ok=port->state().base==g.base&&port->state().local==g.starts&&port->quantum()==g.quantum;
  // Quantum values are assertions only; expiry/renewal are native.
  if(capture_pending&&capture_group<g.index)throw std::runtime_error("undelivered captured grant");
  const bool captured=capture_pending&&capture_group==g.index;
  if(sound_cpu){
   if(!sound_attached&&g.base>=video_start_ns*XBoardBusArbiter::Time(1000000000ULL)){
    if(sound_cpu->bootstrap_latch.state().regs[11]!=mainbus.compare_timer.state().regs[11]||sound_cpu->bootstrap_latch.state().nmi!=mainbus.compare_timer.state().nmi)throw std::runtime_error("sound latch bootstrap continuity differs");
    sound_cpu->bind_latch(mainbus.compare_timer);sound_attached=true;
   }
   if(!sound_attached)sound_cpu->supply_boot(g.index);
   if(deliver)sound_cpu->advance(delivered_ns);
  }
  if(native_deadlines&&deliver)device_timers.advance(delivered_ns);
  const auto native_head=owns_deadlines?next_device():XBoardBusArbiter::Time(g.deadline);
  if(owns_deadlines){++native_heads;if(native_head!=g.deadline){std::cerr<<"Native device head group="<<g.index<<" base="<<g.base<<" ns="<<uint64_t(native_head/1000000000ULL)<<'/'<<g.deadline/1000000000ULL<<'\n';throw std::runtime_error("native device deadline differs");}}
  std::vector<Phase> none;auto& events=captured?captured_phases:none;size_t cursor=0;uint64_t latest_main=0;
  if(events.empty())++empty;
  auto result=port->grant(native_head,[&](unsigned c,XBoardBusArbiter::Time target,unsigned budget){++grants;auto& r=g.runs[c];
   device_deadline=native_head;
   if(!r.present||target!=r.target||budget!=r.budget){++budget_bad;ok=false;if(budget_bad<5)std::cerr<<"Budget CPU="<<c<<" base="<<g.base<<" target="<<uint64_t(target)<<'/'<<r.target<<" cycles="<<budget<<'/'<<r.budget<<'\n';}
   const bool reset_grant=c==0&&mainprogress&&std::any_of(events.begin(),events.end(),[](const Phase& p){return p.cpu==0&&p.pc==0x14840&&p.snapshot.present;});
   r.present=false;if(!owns_boost&&!reset_grant)for(auto b:r.boosts)boost(b);
   if(c==0)request_reset=[&,expected=r.boosts,reset_grant,index=size_t(0)](uint64_t ns)mutable{
    if(!reset_grant||index>=expected.size()||expected[index].at!=ns*1000000000ULL){
     std::cerr<<"Main RESET callback ns="<<ns<<'/'<<(index<expected.size()?expected[index].at/1000000000ULL:0)<<'\n';
     throw std::runtime_error("main RESET callback/quantum timing mismatch");}
    // set_state_synced queues ASSERT/CLEAR on one input-line timer. A
    // second RESET callback before delivery appends to that existing queue;
    // it does not schedule another timer at its later private CPU clock.
    if(owns_boost){native_boost(ns);++index;}else boost(expected[index++]);reset_requests.push_back(reset_requests.empty()?ns:reset_requests.front());++reset_requests_sent;maincpu.abort_grant(ns);
   };
   auto shared_check=[&](const Phase& p,bool apply){
    if(sound_attached&&p.cpu==0&&p.write){
     if(p.a>=0x140000&&p.a<0x150000&&(p.m&255))sound_cpu->check_native_host(g.index,p.ns,5,(p.a/2)&7,p.d&255);
     else if(p.a>=0xe8000&&p.a<0xec000&&(((p.a/2)&15)==11||((p.a/2)&15)==15))sound_cpu->check_native_host(g.index,p.ns,4,0,p.d&255);
    }
    if(apply){++supplied_phases[{p.cpu,p.pc}];if(p.write)++supplied_writes[{p.cpu,p.pc}];}
    ++phases;if(c==0)latest_main=p.ns;else if(latest_main>p.ns)++leads;
    const bool shared=p.space==0&&(p.cpu==0?(p.a>=0x280000&&p.a<0x2c0000):(p.a>=0x80000&&p.a<0xc0000));
    if(shared){auto a=p.a&0xfffff;auto offset=(a<0xa0000?0:0x4000)+(a&0x3fff);
     if(p.write){++writes;if(apply)++external_writes;else ++native_shared_writes;for(unsigned lane=0;lane<2;++lane)if(p.m&(lane?255:0xff00))main_write_ns[offset+lane]=p.cpu==0?p.ns:0;}
     else{++reads;if(p.cpu==1&&(((p.m&0xff00)&&main_write_ns[offset]>p.ns)||((p.m&255)&&main_write_ns[offset+1]>p.ns)))++shared_leads;auto value=bus.read16(a);if((value&p.m)!=(p.d&p.m)){++read_bad;ok=false;if(read_bad<5)std::cerr<<"Arbitrated shared read CPU="<<p.cpu<<" a="<<std::hex<<a<<" value="<<value<<'/'<<p.d<<std::dec<<" ns="<<p.ns<<'\n';}}}
    if(apply&&p.write){
     if(p.cpu==0){auto o=MainBus::memory_offset(p.a);if(o!=MainBus::UNMAPPED){if(p.m&0xff00)memory.bytes[o]=p.d>>8;if(p.m&255)memory.bytes[o+1]=p.d;}}
     else {
      if(p.m==0xffff)bus.write16(p.a,p.d);
      else {if(p.m&0xff00)bus.write8(p.a,p.d>>8);if(p.m&255)bus.write8(p.a+1,p.d);}
     }
    }
    if(mainprogress&&apply&&p.cpu==0&&p.write&&p.a>=0xe0000&&p.a<0xe8000){
     auto* saved=mainbus.trace;mainbus.trace=nullptr;
     if(p.m==0xffff)mainbus.write16(p.a,p.d);else if(p.m==0xff00)mainbus.write8(p.a,p.d>>8);else if(p.m==255)mainbus.write8(p.a+1,p.d);else throw std::runtime_error("invalid supplied main math mask");
     mainbus.trace=saved;
    }
    if(physical_main&&apply&&p.cpu==0&&p.a>=0xe8000&&p.a<0xe8020){
     main_phase_ns=p.ns;
     if(p.write)mainbus.compare_timer.write((p.a&31)/2,p.d,p.m);else mainbus.compare_timer.read((p.a&31)/2);
    }
    if(physical_main&&apply&&p.cpu==0&&p.write&&((p.a>=0x140000&&p.a<0x140010)||p.a==0x130000)){
     main_phase_ns=p.ns;auto* saved=mainbus.trace;mainbus.trace=nullptr;
     if(p.m==0xffff)mainbus.write16(p.a,p.d);else if(p.m==255)mainbus.write8(p.a+1,p.d);else if(p.m==0xff00)mainbus.write8(p.a,p.d>>8);
     mainbus.trace=saved;
    }
    if(apply&&p.cpu==1&&!p.write&&p.a>=0xee000&&p.a<0xf0000)bus.read16(p.a);
   };
   observe=[&](unsigned cpu,bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t t){
    ++native_phases;if(w)++native_writes;native[cpu]->bus_completed(t);
    if(cursor>=events.size()||events[cursor].cpu!=cpu){std::cerr<<"Out-of-grant phase CPU="<<cpu<<" pc="<<std::hex<<pc<<" a="<<a<<std::dec<<" ns="<<t<<" grant="<<g.index<<" target="<<uint64_t(target)/1000000000ULL<<" cursor="<<cursor<<" events="<<events.size()<<" next="<<(cursor<events.size()?events[cursor].ns:0)<<" nextcpu="<<(cursor<events.size()?events[cursor].cpu:9)<<"\n";throw std::runtime_error("native phase outside granted CPU events");}
    auto p=events[cursor++];
    if(p.space!=native_space||p.ns!=t||p.pc!=pc||p.a!=a||p.write!=w||p.m!=m||(p.d&m)!=(d&m)){
     std::cerr<<"Native phase cpu="<<cpu<<" pc="<<std::hex<<pc<<" a="<<a<<'/'<<p.a<<" d="<<d<<'/'<<p.d<<std::dec<<" ns="<<t<<'/'<<p.ns<<" grant="<<g.index<<" space="<<native_space<<'/'<<p.space<<'\n';
     throw std::runtime_error("native publication phase mismatch");}
    shared_check(p,false);
    video_check(p.cpu==0&&p.write&&p.a>=0x110000&&p.a<0x120000?1:p.cpu==1&&p.a>=0xee000&&p.a<0xf0000?(!p.write?2:p.m&255?3:0):0,p);
   };
   const auto start_ns=uint64_t(port->state().local[c])/1000000000ULL;
   const auto target_ns=start_ns+uint64_t(budget)*(c==2?250:80);
   uint64_t end_ns=start_ns;bool all_native=c<2&&running[c]&&g.index<=last_capture_group,any_native=false;
   if(c<2&&running[c])++split_grants;
   while((cursor<events.size()&&events[cursor].cpu==c)||(c<2&&running[c]&&g.index<=last_capture_group)){
    if(c<2&&running[c]){
     any_native=true;end_ns=native[c]->advance(target_ns);
     // A completed instruction can still owe private internal-tail clocks.
     // Pay those from later grants before starting its successor.
     if(!native[c]->done()||end_ns<states[c].ns)break;
     running[c]=false;linked[c]=true;
     if(end_ns>=target_ns)break;
    }else if(cursor<events.size()&&events[cursor].cpu==c){
     const auto p=events[cursor];
     if(c<2&&p.snapshot.present&&(p.snapshot.kind>=2||bounded(c,p.pc))){begin(p);any_native=true;}
     else{all_native=false;if(c==1&&supplied_contexts[1])++sub_late_supplied;if(c<2)linked[c]=false;if(c==0)main_return_pending=false;shared_check(p,true);++cursor;end_ns=p.ns+320;}
    }else break;
   }
   const auto cycle=c==2?XBoardBusArbiter::SOUND_CYCLE:XBoardBusArbiter::CYCLE;
   auto supplied_end=port->state().local[c]+r.ran*cycle;
   if(c==2&&sound_cpu){
    sound_cpu->observer=[&](const XBoardSoundCpu::Event& e,uint64_t ns){
     unsigned kind=0;const auto k=unsigned(e.kind);
     if(k<2&&e.address>=0xf000&&e.address<0xf800)kind=k?5:4;
     else if(k==2&&e.address<0x80)kind=e.address<0x40?2:1;
     else if(k==3&&e.address<0x40)kind=3;
     if(!kind||sound_events.empty()||g.index<sound_events.front().grant||g.index>last_capture_group)return;
     if(sound_cursor>=sound_events.size())throw std::runtime_error("extra native joint sound device transaction");
     const auto& expected=sound_events[sound_cursor++];
     if(expected.grant!=g.index||expected.ns!=ns||expected.kind!=kind||expected.pc!=e.pc||expected.a!=e.address||expected.d!=e.data)throw std::runtime_error("native joint sound device phase differs");
     if(kind==1)++sound_acknowledges;
    };
    const auto ran=sound_cpu->run(g.index,start_ns,budget);
    const auto actual=port->state().local[c]+ran*cycle;
    if(actual!=supplied_end)throw std::runtime_error("native joint sound completion differs");
    return actual;
   }
   if(c==2){while(sound_cursor<sound_events.size()&&sound_events[sound_cursor].grant==g.index){const auto e=sound_events[sound_cursor++];
    if(e.ns<start_ns||XBoardBusArbiter::Time(e.ns)*1000000000ULL>supplied_end)throw std::runtime_error("sound transaction outside granted CPU interval");
    if(e.kind==1){const auto actual=mainbus.compare_timer.sound_read();
     if(actual!=e.d){std::cerr<<"Sound latch phase pc="<<std::hex<<e.pc<<" data="<<unsigned(actual)<<'/'<<e.d<<std::dec<<" ns="<<e.ns<<'\n';throw std::runtime_error("computed sound latch mismatch");}++sound_acknowledges;
    }else ++sound_supplied;
   }}
   // Only a grant wholly inside the retained bounded graph claims a native
   // completion. Other CPU work/zero-progress exits remain labelled inputs.
   if(all_native&&any_native){++bounded_returns;
    if(end_ns*1000000000ULL!=supplied_end){++return_bad;std::cerr<<"Native grant completion "<<g.index<<" CPU="<<c<<" ns="<<end_ns<<'/'<<uint64_t(supplied_end)/1000000000ULL<<'\n';}
   }
   return all_native&&any_native?XBoardBusArbiter::Time(end_ns)*1000000000ULL:supplied_end;
  },g.enabled);
  if(owns_boost){
   auto expected=g.before;for(const auto& r:g.runs)expected.insert(expected.end(),r.boosts.begin(),r.boosts.end());
   expected.erase(std::remove_if(expected.begin(),expected.end(),[](const Boost& b){return b.quantum==XBoardBusArbiter::DEFAULT_QUANTUM;}),expected.end());
   if(expected.size()!=produced_boosts.size())throw std::runtime_error("native boost owner count differs");
   for(size_t i=0;i<expected.size();++i)if(expected[i].at!=produced_boosts[i].at||expected[i].quantum!=produced_boosts[i].quantum||expected[i].duration!=produced_boosts[i].duration)throw std::runtime_error("native boost owner timestamp differs");
  }
  for(auto r:g.runs)if(r.present){++grant_bad;ok=false;}
  if(cursor!=events.size()){phase_bad+=events.size()-cursor;ok=false;}
  ok&=result==g.end;
  if(!ok){++failures;if(failures<8)std::cerr<<"Arbiter base="<<g.base<<" next="<<uint64_t(result)<<'/'<<g.end<<" phases="<<cursor<<'/'<<events.size()<<'\n';}
  if(captured)next_capture();
  ++groups;
  if(progress&&groups%100000==0)std::cerr<<"Joint replay progress: groups="<<groups<<" global_ns="<<delivered_ns<<" native_records="<<instructions<<" native_phases="<<native_phases<<'\n';
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
 if(active||cpu_masks!=3*groups||capture_pending)throw std::runtime_error("incomplete grant or bus capture");
 if(sound_cpu){sound_cpu->finish(groups);sound_cpu->report();}
 if(native_deadlines){if(!native_heads)throw std::runtime_error("no native device head coverage");for(const auto& events:timer_assertions)if(!events.empty())throw std::runtime_error("missing native per-owner timer callbacks");std::cout<<"Native device deadlines: "<<native_heads<<" head assertions identical; periodic callback timestamps identical ";for(auto n:device_timers.deliveries)std::cout<<n<<' ';std::cout<<'\n';}
 if(require_reset&&(!reset_sequences||reset_requests_sent!=2*reset_deliveries||!reset_requests.empty()))
  throw std::runtime_error("missing completed joint RESET coverage");
 std::cout<<"CPU grant groups: "<<groups-failures<<" of "<<groups<<" identical, "<<failures<<" different, 0 skipped; "<<grants<<" CPU grants, "<<empty<<" groups without captured program phases\nMain/sub bus arbitration: "<<phases<<" phases in computed CPU order, "<<phase_bad<<" undelivered; "<<leads<<" cross-CPU local-clock leads; "<<boosts<<" computed quantum requests; "<<budget_bad<<" budget differences, "<<grant_bad<<" missing grants\n";
 std::cout<<"Arbitrated shared reads: "<<reads-read_bad<<" of "<<reads<<" identical, "<<read_bad<<" different, 0 skipped; "<<native_shared_writes<<" computed and "<<external_writes<<" supplied shared writes, "<<shared_leads<<" reads after a main write with a later local timestamp, 0 read assertions seeded\n";
 // Unexercised publication paths are the missed-count ADDQ and optional normal
 // sprite BSR ($1486A/$43E); their independent directed oracles remain separate.
 // Reset startup has its own coverage gate below; short runs need not reset.
 for(unsigned c=0;c<2;++c)for(uint32_t pc=0;pc<0x14900;pc+=2)
  if(publication_bounded(c,pc)&&!(c==1&&(sub_instruction(pc)||OSubIrq::startup_instruction(pc)))&&pc!=0x1486a&&pc!=0x43e&&pc!=0x420&&pc!=0x428&&!coverage[{c,pc}])throw std::runtime_error("missing joint publication PC");
 std::cout<<"Capture stop: "<<unsigned(running[0])+unsigned(running[1])<<" native continuations parked outside accepted final grant; all later CPU completions supplied\n";
 for(auto [key,n]:coverage)std::cout<<"Native publication PC CPU="<<key.first<<" pc="<<std::hex<<key.second<<std::dec<<": "<<n<<'\n';
 unsigned progress_pcs=0;for(const auto& i:sub_instructions)if(coverage[{1,i.pc}])++progress_pcs;
 for(uint32_t pc:{0x45e,0x594,0x648,0xc0a,0xf02,0x1074})
  if(!reset_window&&!coverage[{1,pc}])throw std::runtime_error("missing joint sprite/road entry or raster writer");
 std::cout<<"Unexercised joint sprite/road PCs:";
 for(const auto& i:sub_instructions)if(!coverage[{1,i.pc}])std::cout<<' '<<std::hex<<i.pc<<std::dec;
 std::cout<<'\n';
 for(const auto& [pc,n]:branches)std::cout<<"Joint sub branch "<<std::hex<<pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
 for(const auto& [pc,n]:main_branches)std::cout<<"Joint main branch "<<std::hex<<pc<<std::dec<<" taken/untaken="<<n[1]<<'/'<<n[0]<<'\n';
 if(mainentries&&(!main_entry_levels[0]||!main_entry_levels[1]||!main_entry_levels[2]||main_acknowledgements!=main_entries||!main_retained_returns||!main_resumptions))
  throw std::runtime_error("missing joint main entry/RTE/resumption coverage");
 std::cout<<"Physical main IRQ selection: "<<main_selected_entries<<" retained entries, "<<main_polls<<" native IPL polls\n";
 std::cout<<"Joint main IRQ levels 2/4/6: "<<main_entry_levels[0]<<'/'<<main_entry_levels[1]<<'/'<<main_entry_levels[2]<<"\n";
 std::cout<<"Joint main IRQ entries: "<<main_entries<<"; retained caller entries: "<<main_retained_entries<<"; VPA acknowledges: "<<main_acknowledgements<<"; RTE records: "<<main_returns<<"; retained RTE: "<<main_retained_returns<<"; retained resumptions: "<<main_resumptions<<(physical_main?" (physical main selection computed)\n":" (selection level supplied)\n");
 std::cout<<"Joint sprite/road PC coverage: "<<progress_pcs<<" of "<<sub_instructions.size()<<"; IRQ entry/services execute jointly\n";
 if(mainprogress){unsigned n=0;for(auto pc:main_joint_pcs)if(coverage[{0,pc}])++n;
  std::cout<<"Joint existing main caller PC coverage: "<<n<<" of "<<main_joint_pcs.size()<<"\nUnexercised joint main caller PCs:";
  for(auto pc:main_joint_pcs)if(!coverage[{0,pc}])std::cout<<' '<<std::hex<<pc<<std::dec;std::cout<<'\n';
  unsigned wrappers=0;for(const auto& i:main_irqwrapper_instructions)if(coverage[{0,i.pc}])++wrappers;
  std::cout<<"Joint main IRQ wrapper PC coverage: "<<wrappers<<" of "<<main_irqwrapper_instructions.size()<<"; ADC/mux/output transactions execute through joint endpoints\n";
  for(uint32_t pc:{0x14622,0x14632,0x14666,0x14686,0x14750,0x14730,0x14786,0x147c2,0xb7f8,0xb834})
   if(!coverage[{0,pc}])throw std::runtime_error("missing joint main IRQ RAM-wrapper representative PC");
  for(uint32_t pc:{0x14682,0x14730,0x14786})
   if(!main_branches[pc][0]||!main_branches[pc][1])throw std::runtime_error("missing joint main IRQ logic/display or delay branch direction");
 }
 std::array<unsigned,2> supplied_phase_counts{},supplied_write_counts{},supplied_pc_counts{};
 for(auto [key,n]:supplied_phases){supplied_phase_counts[key.first]+=n;++supplied_pc_counts[key.first];}
 for(auto [key,n]:supplied_writes)supplied_write_counts[key.first]+=n;
 if(!display_fixture.empty()&&(fixture_cursor!=display_fixture.size()||!video_disabled||!delayed_register_clips||!row_scroll_clips||!column_scroll_clips||!effect_pixels||!hilight_pixels||effect_pixels==hilight_pixels))throw std::runtime_error("directed video fixture lacks blanking, pending registers, scroll modes or both effect variants");
 if(rgb_assertions&&(latch_cursor!=latch_events.size()||pixel_capture.peek()!=EOF||!video_pixels||!video_latches))throw std::runtime_error("unconsumed joint RGB/latch assertions or empty coverage");
 if(rgb_assertions)std::cout<<"Joint video coverage: priority masks="<<std::hex<<priority_masks<<std::dec<<" effect pixels="<<effect_pixels<<" hilight pixels="<<hilight_pixels<<" clips with pending global registers="<<delayed_register_clips<<" row/column scroll clips="<<row_scroll_clips<<'/'<<column_scroll_clips<<'\n';
 if(rgb_assertions)std::cout<<"Joint RGB: "<<video_pixels<<" pixels identical, 0 different, 0 skipped; "<<video_latches<<" native line-261 latches; "<<video_disabled<<" disabled clips; one initial latch boundary supplied; "<<fixture_cursor<<" directed video fixture events supplied\n";
 std::cout<<"Joint supplied execution census: main="<<supplied_phase_counts[0]<<" phases at "<<supplied_pc_counts[0]<<" PCs, "<<supplied_write_counts[0]<<" writes; sub="<<supplied_phase_counts[1]<<" phases at "<<supplied_pc_counts[1]<<" PCs, "<<supplied_write_counts[1]<<" writes\n";
 if(require_main_closed&&(supplied_phase_counts[0]||supplied_contexts[0]!=1||main_selected_entries!=main_entries))
  throw std::runtime_error("joint main closure requires zero supplied main phases, one initial context and every physical IRQ selected natively");
 if(require_sub_continuity&&(sub_late_supplied||supplied_contexts[1]!=1))
  throw std::runtime_error("joint sub continuity requires one initial context and zero later supplied phases");
 if(require_sub_continuity&&require_reset){for(uint32_t pc=0x402;pc<=0x63e;pc+=2)
  if(OSubIrq::startup_instruction(pc)&&!coverage[{1,pc}])throw std::runtime_error("missing joint sub startup instruction");}
 std::cout<<"Joint sub continuity: "<<sub_late_supplied<<" supplied phases after initial context\n";
 if(video.is_open()&&video.peek()!=EOF)throw std::runtime_error("unconsumed joint video bank assertions");
 if(drawing_assertions&&draw_cursor!=draw_events.size())throw std::runtime_error("unconsumed joint screen frontier assertions");
 std::cout<<"Joint video ownership: "<<video_banks<<" computed bank swaps, "<<video_controls<<" computed road controls, "<<video_frames<<" physical-frame hidden sprite bank writebacks checked; "<<video_partial_updates<<" computed partial screen updates\n";
 if(sound_cursor!=sound_events.size()||(require_sound_handshake&&!sound_acknowledges))throw std::runtime_error("incomplete joint sound handshake coverage");
 std::cout<<"Joint sound handshake: "<<sound_deliveries<<" synchronized command deliveries, "<<sound_acknowledges<<" computed latch reads/NMI acknowledges; "<<sound_supplied<<" YM/PCM transactions at supplied Z80 clocks\n";
 if(mainprogress){for(auto [key,n]:supplied_phases)
  std::cout<<"Supplied execution CPU="<<key.first<<" pc="<<std::hex<<key.second<<std::dec<<" phases="<<n<<" writes="<<supplied_writes[key]<<'\n';}
 if(!irq_entries||retained_entries!=irq_entries||irq_returns!=irq_entries||acknowledgements!=irq_entries||in_irq)
  {std::cerr<<"Joint IRQ closure entries="<<irq_entries<<" retained="<<retained_entries<<" returns="<<irq_returns<<" VPA="<<acknowledgements<<" active="<<in_irq<<"\n";throw std::runtime_error("incomplete retained joint sub IRQ coverage");}
 std::cout<<"Joint entry contexts: main="<<supplied_contexts[0]<<" sub="<<supplied_contexts[1]<<"; initial partial ISR remains supplied\n";
 std::cout<<"Joint RESET: "<<reset_requests_sent<<" computed main callback requests, "<<reset_deliveries<<" synchronized sub cancellations, "<<reset_sequences<<" native vector/prefetch sequences\n";
 std::cout<<"Joint main startup: "<<main_traps<<" TRAP #12 entries, "<<main_startup_returns<<" RESET-handler RTEs\n";
 std::cout<<"Joint sub IRQ: "<<irq_entries<<" entries, "<<retained_entries<<" from retained caller state; "<<irq_returns<<" exact RTE restorations, "<<sub_polls<<" computed selections, "<<acknowledgements<<" computed VPA waits, "<<in_irq<<" active at capture stop\n";
 std::cout<<"Joint instruction/IRQ records: "<<instructions<<" of "<<instructions<<" identical, 0 different, 0 skipped; "<<native_phases<<" of "<<native_phases<<" ordered phases identical, 0 different, 0 skipped\n";
 std::cout<<"Joint native publication/progress: "<<instructions<<" instructions, "<<native_phases<<" phases, "<<native_writes<<" writes computed; "<<chains<<" retained instruction contexts, "<<contexts<<" supplied entry contexts; "<<split_grants<<" grants resume an active instruction; "<<bounded_returns-return_bad<<" of "<<bounded_returns<<" bounded grant completions identical, "<<return_bad<<" different, 0 skipped\n";
 return failures||phase_bad||budget_bad||grant_bad||read_bad||state_bad||return_bad||!bounded_returns?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

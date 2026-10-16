#pragma once
#include "engine/mainbus.hpp"
#include "engine/omainirqservice.hpp"
#include "engine/osubirq.hpp"
#include "hwmath/xboard_bus_arbiter.hpp"
#include "hwmath/xboard_cpu_continuation.hpp"
#include "hwmath/xboard_device_deadlines.hpp"
#include "hwmath/xboard_irq_lines.hpp"
#include "hwvideo/xboard_video_timing.hpp"
#include "hwvideo/screen.hpp"
#include "roms.hpp"
#include <deque>

// Autonomous, literal ROM program host. The sound type is an independently
// clocked board owner; an oracle may attach passive observers to that owner.
// No capture, instruction list, completion time or state seed enters this API.
template<class Sound> class XBoardHost {
public:
    using Time=XBoardBusArbiter::Time;
    struct Entry {OMainIrqService::Result state{};uint32_t usp{},ssp{};uint64_t bias{};unsigned kind{};};
    struct Phase {unsigned cpu{};bool write{};uint32_t address{};uint16_t data{},mask{};uint32_t pc{};uint64_t ns{};unsigned space{};const Entry* entry{};};
    struct Input {uint64_t ns{};uint8_t buttons=0xff;std::array<uint8_t,8> analog{};};
    MainBus::Memory memory;MainBus bus;
    XBoardBusArbiter arbiter{XBoardBusArbiter::State{}};
    XBoardTimeline board;XBoardMainIrqLines lines;XBoardDeviceDeadlines devices;XBoardVideoTiming video;
    std::vector<RGB888> pixels=std::vector<RGB888>(320*224);
    std::function<void(const Phase&)> phase;
    bool presentation_phase{};
    Screen::LayerObserver layers;
    // Optional passive profiling boundary; never owns emulated time.
    std::function<void()> rendering_started;
    std::function<void(uint64_t,unsigned,unsigned,bool)> rendered;
    std::function<void(uint64_t)> latched,frame;
    std::function<void(Time)> boosted;
    std::function<void(unsigned,Time,unsigned,Time)> granted;
    std::array<uint8_t,3> switches{0xff,0xff,0xdd};
    uint64_t group{},delivered_ns{},instructions{},reset_deliveries{},reset_sequences{},frames{};
    uint64_t tile_gen{1},palette_gen{1};
    XBoardHost(Roms& rom,HWPalette& palette,HWRoad& road,HWSprites& sprites,HWTiles& tiles,Sound& sound)
        :bus(rom.maincpu.rom,rom.maincpu.length,rom.subcpu.rom,rom.subcpu.length,memory),board(bus.compare_timer),
         palette(palette),road(road),sprites(sprites),tiles(tiles),screen(320,224,palette,road,sprites,tiles),sound(sound){
        analog.fill(0x10);for(unsigned i=0;i<5;++i)analog[i]=0x80;
        sound.bind_latch(bus.compare_timer);video.begin_from_reset();
        trace.before_access=[&](bool w,uint32_t a,uint16_t){
            if(w){
                const uint32_t m=a&0x3fffff;
                if(m>=0xd0000&&m<0xe0000)video.before_text_write(main_ns,m&0xfff);
                else if(m>=0xc0000&&m<0xd0000)++tile_gen;
                else if(m>=0x120000&&m<0x130000)++palette_gen;
            }
        };
        trace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m){observe(0,w,a,d,m,mainport.current_pc(),main_ns);};bus.trace=&trace;
        board.adc.analog=[&]{return analog[(mux>>2)&7];};
        bus.endpoints.adc_read=[&]{return board.adc.read(board.now());};
        bus.endpoints.adc_write=[&](uint8_t){const bool starting=!board.adc.busy();board.adc.write(main_ns);
            if(starting&&running[0]&&Time(board.adc.deadline())*AS_PER_NS<device_head)maincpu.abort_grant(main_ns);};
        bus.io[1].input=[&](unsigned p){return p==0?buttons:p<=3?switches[p-1]:uint8_t(0xff);};
        bus.io[0].output=[&](unsigned p,uint8_t d,uint8_t){if(p==2){mux=d;sound.queue_reset(main_ns,!(d&1));video.display_write(main_ns,d&0x20);if(running[0])maincpu.abort_grant(main_ns);}};
        bus.compare_timer.irq_line=[&](bool){const auto ns=advancing?board.now():main_ns;lines.update(bus.compare_timer.state().irq,board.vblank(),ns);
            if(board.main_irq_level())boost(ns);if(!advancing&&running[0])maincpu.abort_grant(main_ns);};
        bus.compare_timer.schedule_sound=[&](uint8_t d){commands.emplace_back(main_ns,d);if(running[0])maincpu.abort_grant(main_ns);};
        board.scanline=[&](uint64_t ns,unsigned line){if(line==223||line==224){lines.update(bus.compare_timer.state().irq,board.vblank(),ns);if(board.main_irq_level())boost(ns);}lines.deliver(ns);video.scanline(ns,line);};
        video.latch=[&](uint64_t ns){sync_text();tiles.latch_frame_registers();if(latched)latched(ns);};
        screen.layers_rendered=[&](auto top,auto bottom,auto indices,auto priority,const HWPalette& pens){
            if(layers)layers(top,bottom,indices,priority,pens);
        };
        video.render=[&](uint64_t ns,unsigned top,unsigned bottom,bool display){if(rendering_started)rendering_started();render(top,bottom,display);if(rendered)rendered(ns,top,bottom,display);};
        video.frame=[&](uint64_t ns){++frames;if(frame)frame(ns);};
        devices.callback=[&](XBoardDeviceDeadlines::Owner owner,uint64_t ns){if(owner==XBoardDeviceDeadlines::SoundUpdate)sound.update_speaker(ns);};
        main_clock_fn=[this](uint64_t ns){maincpu.clock(ns);main_ns=ns;};
        main_poll_fn=[this](uint64_t ns){return main_poll(ns);};
        main_reset_fn=[this](uint64_t ns){boost(ns);resets.push_back(resets.empty()?ns:resets.front());maincpu.abort_grant(ns);};
        sub_clock_fn=[this](uint64_t ns){subcpu.clock(ns);};
        sub_phase_fn=[this](bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t ns){observe(1,w,a,d,m,pc,ns);};
        sub_poll_fn=[this](uint64_t ns){return sub_poll(ns);};
        sub_checkpoint_fn=[this](const OSubIrq::State& st){live_sub=st;};
        sub_ack_fn=[this](uint64_t ready){subcpu.hold_until(ready);observe(1,false,0xfffff8,28,0xffff,bus_pc[1],ready,1);subcpu.bus_completed(ready,400);};
    }
    std::span<const uint16_t> sprite_words() const { return screen.sprite_words(); }
    void input(Input value){if(!inputs.empty()&&value.ns<inputs.back().ns)throw std::runtime_error("nonmonotonic physical input");inputs.push_back(value);}
    static uint64_t ns_of(Time t){return t<=UINT64_MAX?uint64_t(t)/AS_PER_NS:uint64_t(t/AS_PER_NS);}
    uint64_t time()const{return ns_of(arbiter.state().base);}
    Time deadline()const{
        auto ns=std::min({devices.deadline(),board.next_scanline(),board.adc.deadline(),lines.deadline(),sound.deadline()});
        if(!resets.empty())ns=std::min(ns,resets.front());if(!commands.empty())ns=std::min(ns,commands.front().first);return Time(ns)*AS_PER_NS;
    }
    // One independently selected scheduler group. Observer traces can assert
    // its before/after state, but cannot change its deadline or CPU dispatch.
    void step(){
        deliver();device_head=deadline();++group;
        arbiter.grant(device_head,[&](unsigned c,Time target,unsigned budget){
            const auto start=c==0?maincpu.time():c==1?subcpu.time():ns_of(arbiter.state().local[2]);
            Time end;
            if(c==2)end=arbiter.state().local[c]+Time(sound.run(group,start,budget))*XBoardBusArbiter::SOUND_CYCLE;
            else end=Time(run(c,start+uint64_t(budget)*80))*AS_PER_NS;
            if(granted)granted(c,target,budget,end);return end;
        });
    }
    void deliver(){
        delivered_ns=time();
        // Digital inputs are observed at the scheduler base. ADC samples are
        // latched chronologically in advance_board, before their beam edge.
        for(const auto& v:inputs){if(v.ns>delivered_ns)break;buttons=v.buttons;}
        if(deadline()>arbiter.state().base)return;
        arbiter.timers_delivered();advance_board(delivered_ns);lines.deliver(delivered_ns);
        if(!resets.empty()&&resets.front()<=delivered_ns){
            const auto ns=subcpu.time();subcpu.reset(ns);substate=live_sub;substate.ns=ns;substate.cycle_bias=ns/80;
            states[1]={{substate.d,substate.a},substate.pc,substate.sp,substate.sr,ns};running[1]=false;pending[1]=0;reset_ready[1]=true;resets.clear();++reset_deliveries;
        }
        sound.advance(delivered_ns);devices.advance(delivered_ns);
    }
private:
    static constexpr uint64_t AS_PER_NS=1000000000;
    HWPalette& palette;HWRoad& road;HWSprites& sprites;HWTiles& tiles;Screen screen;Sound& sound;
    MainBus::Trace trace;OMainIrqService mainport;OSubIrq subport;
    OMainIrqService::Clock main_clock_fn,main_reset_fn;OMainIrqService::Poll main_poll_fn;
    OSubIrq::Clock sub_clock_fn,sub_ack_fn;OSubIrq::Phase sub_phase_fn;OSubIrq::Poll sub_poll_fn;
    OSubIrq::Checkpoint sub_checkpoint_fn,sub_empty_checkpoint_fn;
    std::array<OMainIrqService::Result,2> states{};std::array<Entry,2> entries{};
    std::array<bool,2> running{},first_phase{},reset_ready{true,true};std::array<unsigned,2> pending{},entry_kind{};std::array<uint32_t,2> bus_pc{};
    uint32_t usp{},ssp{};OSubIrq::State substate{},live_sub{};
    uint64_t main_ns{};Time device_head{};bool advancing{};uint8_t mux{},buttons=0xff;std::array<uint8_t,8> analog{};
    std::deque<Input> inputs;std::deque<uint64_t> resets;std::deque<std::pair<uint64_t,uint8_t>> commands;
    // Continuations are last: other members stay alive while their stacks unwind.
    XBoardCpuContinuation maincpu,subcpu;
    static bool rte(uint32_t pc){return pc==0x1466a||pc==0x14672||pc==0x147c6||pc==0x147e6||pc==0x14842||pc==0x144ea||pc==0x14828;}
    void boost(uint64_t ns){const auto at=Time(ns)*AS_PER_NS;arbiter.boost(at);if(boosted)boosted(at);}
    void observe(unsigned c,bool w,uint32_t a,uint16_t d,uint16_t m,uint32_t pc,uint64_t ns,unsigned space=0){
        auto& worker=c?subcpu:maincpu;if(!space)worker.bus_completed(ns);
        const bool fp=first_phase[c];first_phase[c]=false;
        if(phase){
            if(presentation_phase){
                const auto entry_pc=states[c].pc;
                const bool match=c==0
                    ? ((w&&(pc==0x9ef6||pc==0x9ebc||pc==0x9c20||(a>=0x110000&&a<0x120000))) ||
                       (fp&&(entry_pc==0xb628||entry_pc==0x8bb0||entry_pc==0x8bb8||entry_pc==0xb684||entry_pc==0x10fba||entry_pc==0xb7c2||entry_pc==0xb7d2||entry_pc==0x13ce8)))
                    : (fp&&(entry_pc==0x648||entry_pc==0x7b8));
                if(!match)return;
            }
            if(fp)entries[c]={states[c],c?substate.usp:usp,c?substate.ssp:ssp,c?substate.cycle_bias:0,entry_kind[c]};
            phase({c,w,a,d,m,pc,ns,space,fp?&entries[c]:nullptr});
        }
    }
    void advance_board(uint64_t ns){
        video.advance_initial_latch(ns);advancing=true;
        while(!inputs.empty()&&inputs.front().ns<=ns){const auto v=inputs.front();inputs.pop_front();board.advance_before(v.ns);analog=v.analog;buttons=v.buttons;}
        board.advance(ns);
        while(!commands.empty()&&commands.front().first<=ns){sound.external_time(commands.front().first);bus.compare_timer.deliver_sound(commands.front().second);commands.pop_front();}
        advancing=false;
    }
    unsigned main_poll(uint64_t ns){maincpu.clock(ns);return lines.level();}
    unsigned sub_poll(uint64_t ns){subcpu.clock(ns);constexpr auto first=XBoardTimeline::RESET_LINE0_NS+223*XBoardTimeline::LINE_NS;
        return delivered_ns>=first&&(delivered_ns-first)%XBoardTimeline::FRAME_NS<XBoardTimeline::LINE_NS?4u:0u;}
    uint64_t run(unsigned c,uint64_t target){
        auto& worker=c?subcpu:maincpu;uint64_t end=worker.time();
        while(end<target){
            if(!running[c])begin(c);
            end=worker.advance(target);
            if(!worker.done()||end<states[c].ns)break;
            running[c]=false;
        }
        return end;
    }
    void step_instruction(unsigned c){
        const auto state=states[c];const bool reset=reset_ready[c];const auto irq=pending[c];
        if(!reset&&!irq)bus_pc[c]=state.pc;
        entry_kind[c]=reset?(c?3u:7u):irq?(c?2u:3+irq/2):1u;
        first_phase[c]=true;running[c]=true;reset_ready[c]=false;++instructions;
        if(c==0){
            OMainIrqService::Context ctx{};ctx.registers=state.registers;ctx.boundary_ns=state.ns;ctx.interrupted_pc=state.pc;ctx.supervisor_sp=state.sp;ctx.sr=state.sr;ctx.exception_supervisor_sp=ssp;
            if(reset)states[0]=mainport.reset_joint(bus,state,bus_pc[0],main_clock_fn);
            else if(irq){ctx.interrupted_pc=bus_pc[0];ctx.return_pc=state.pc;ctx.supervisor_sp=ssp;ctx.level=irq;pending[0]=0;
                states[0]=mainport.enter_joint(bus,ctx,main_clock_fn,0,[&](uint64_t ready){maincpu.hold_until(ready);observe(0,false,0xfffff0+irq*2,24+irq,0xffff,bus_pc[0],ready,1);maincpu.bus_completed(ready,400);},
                    [&](uint16_t sr,uint64_t ns){const auto level=main_poll(ns);pending[0]=OMainIrqService::eligible(level,sr)?level:0;});
            }else if(state.pc==0x138ba||state.pc==0x13820||state.pc==0x13884||state.pc==0x144f4){const auto r=mainport.trap_joint(bus,state,ssp,main_clock_fn,main_poll_fn);states[0]=r.state;pending[0]=r.irq;}
            else if(rte(state.pc)){const auto r=mainport.return_joint(bus,state,usp,main_clock_fn,main_poll_fn,ssp);states[0]=r.state;pending[0]=r.irq;if(state.sr&0x2000)ssp=state.sp+6;}
            else{const auto r=mainport.caller_step(bus,ctx,main_clock_fn,main_poll_fn,main_reset_fn);states[0]=r.state;pending[0]=r.irq;}
            if(states[0].sr&0x2000)ssp=states[0].sp;else usp=states[0].sp;
        }else{
            const auto s=substate;
            const bool need_cp=reset_deliveries<2||!resets.empty();
            if(need_cp)live_sub=s;
            if(reset){substate=subport.reset(bus.sub,s,bus_pc[1],sub_clock_fn,sub_phase_fn);++reset_sequences;}
            else if(irq){pending[1]=0;substate=subport.enter(bus.sub,s,bus_pc[1],sub_clock_fn,sub_phase_fn,sub_ack_fn);}
            else{const auto r=subport.instruction(bus.sub,s,sub_clock_fn,sub_phase_fn,sub_poll_fn,need_cp?sub_checkpoint_fn:sub_empty_checkpoint_fn);substate=r.state;pending[1]=r.irq;}
            states[1]={{substate.d,substate.a},substate.pc,substate.sp,substate.sr,substate.ns};
        }
    }
    void begin(unsigned c){
        running[c]=true;
        if(c==0)maincpu.start(states[0].ns,[this]{do{step_instruction(0);}while(maincpu.continue_grant(states[0].ns));},false);
        else{live_sub=substate;subcpu.start(substate.ns,[this]{do{step_instruction(1);}while(subcpu.continue_grant(states[1].ns));},false);}
    }
    uint64_t rendered_tile_gen{},rendered_palette_gen{};
    void sync_text(){for(unsigned i=0;i<0x800;++i)tiles.write_text(i,word(MainBus::TEXT+2*i));}
    uint16_t word(unsigned offset)const{return (memory.bytes[offset]<<8)|memory.bytes[offset+1];}
    void render(unsigned top,unsigned bottom,bool display){
        if(!presentation_phase){++tile_gen;++palette_gen;}
        sync_text();
        if(rendered_tile_gen!=tile_gen){for(unsigned i=0;i<0x8000;++i)tiles.write_tile(i,word(MainBus::TILE+2*i));rendered_tile_gen=tile_gen;}
        if(rendered_palette_gen!=palette_gen){for(unsigned i=0;i<8192;++i)palette.write(i,word(MainBus::PALETTE+2*i));rendered_palette_gen=palette_gen;}
        for(unsigned i=0;i<0x800;++i)road.write16(2*i,(memory.road_buffer[2*i]<<8)|memory.road_buffer[2*i+1]);road.read_road_control();road.write_road_control(memory.road_control);
        screen.render_partial(pixels.data(),top,bottom,display,memory.sprite_buffer.data());
    }
public:
    const Screen& compositor()const{return screen;}
};

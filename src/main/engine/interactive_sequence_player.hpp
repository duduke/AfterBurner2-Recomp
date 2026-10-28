#pragma once
#include "engine/native_stage_skip.hpp"
#include "engine/game_controls.hpp"
#include "engine/interactive_scene_capture.hpp"
#include <functional>
#include <deque>

// Isolated native presentation session for the standalone flight school. It
// executes the existing board, never invents animation frames or writes sequence
// RAM. Runway entry reuses the established directed Practice startup only.
class InteractiveSequencePlayer {
public:
    enum class Surface { carrier, runway, jungle, refuel };
    enum class Phase { rollout, rearming, ready, takeoff, complete };
    InteractiveSequencePlayer(Roms& rom,Surface surface,const std::function<void()>& pump={})
        :game(rom),capture(game,rom,surface==Surface::refuel),surface(surface) {
        auto step=[&](GameControls input={}){tick(input);if(pump)pump();};
        bool prepared=false;
        for(unsigned f=0;f<600;++f){if(surface==Surface::carrier && word(0x29c09c)==15)capture.enable(true);GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;step(c);
            if(surface==Surface::carrier && word(0x29c09c)==15 && word(0xffff90b0)==0 && f%2==0)save_approach();
            if(surface==Surface::carrier && word(0x29c09c)==15 && word(0xffff90b0)==0 && word(0xffff9084)<0x350){prepared=true;break;}}
        if(surface==Surface::carrier){
            for(unsigned f=0;f<1800&&!prepared;++f){step();if(word(0x29c09c)==15 && f%2==0)save_approach();prepared=word(0x29c09c)==15 && word(0xffff90b0)==0 && word(0xffff9084)<0x350;}
        }else if(surface==Surface::refuel){
            NativeStageSkip::enter(game.board(),rom,3,[&]{step();});
            capture.enable(true);
            for(unsigned f=0;f<700&&!prepared;++f){step();
                const auto state=word(0xffff90e0);if(state>=1 && state<=3 && f%2==0)save_approach();
                prepared=state==3;}
        }else{
            NativeStageSkip::enter(game.board(),rom,surface==Surface::runway?5:13,[&]{step();});
            capture.enable(true);
            // $114B2..$114C4 enters state 8's parked/service countdown.
            // The earlier state-4 gear pose is still an aerial camera view.
            // Find its active owner rather than assuming an object-pool slot.
            for(unsigned f=0;f<1800&&!prepared;++f){step();
                for(unsigned i=0;i<256;++i){const auto p=0xffffa000u+i*64;
                    if(!active(p))continue;
                    const auto pc=NativeStageSkip::peek32(game.board(),p+16);
                    if((pc==0x111be || pc==0x1122e) && f%2==0)save_approach();
                    if(pc==0x1122e && word(p+40)==8){owner=p;prepared=true;break;}}}
        }
        if(!prepared)throw std::runtime_error("Native landing presentation handoff not reached");
        maximum=rom.maincpu.read16(0x3218a);
        missiles=word(0xffff8d66);
        save_approach();capture.enable(false);visible={game.board().frames,game.pixels()};
    }
    void advance(const GameControls& input) {
        if(state==Phase::complete)return;
        if(state==Phase::ready){
            if(input.throttle<.95f)return;
            state=Phase::takeoff;
        }
        if(state==Phase::takeoff && !withheld.empty()){
            visible=std::move(withheld.front());withheld.pop_front();
            return;
        }
        if(surface==Surface::carrier && state==Phase::rearming){
            // The arcade carrier startup has no service/reload animation.
            // This explicit practice extension reuses $16CF4's four-physical-
            // frame cadence and the ROM maximum, while holding the deck scene.
            if(++reload_clock%4==0 && missiles<maximum)++missiles;
            if(missiles>=maximum)state=Phase::ready;
            return;
        }
        GameControls physical;
        if(state==Phase::takeoff)physical.throttle=1;
        tick(physical);
        if(surface!=Surface::carrier && (capture.burner_seen() || burner_frame)){
            // The ROM starts its burner while reloading. Keep its last clean
            // image on screen, finish the real native reload, then replay ALL
            // withheld native frames only after the user requests full power.
            if(!burner_frame)burner_frame=game.board().frames;
            if(state==Phase::takeoff)visible={game.board().frames,game.pixels()};
            else{
                if(withheld.size()>=600)throw std::runtime_error("Native pre-takeoff frame buffer exceeded its bounded window");
                withheld.push_back({game.board().frames,game.pixels()});
            }
        }else visible={game.board().frames,game.pixels()};
        if(surface==Surface::carrier){
            if(state==Phase::rollout && word(0xffff90b0)>=1)state=Phase::rearming;
            if(state==Phase::takeoff && word(0x29c09c)==16)state=Phase::complete;
        }else{
            missiles=word(0xffff8d66);
            if(word(0xffff9100)>0){seen_reload=true;state=Phase::rearming;}
            if(seen_reload && state==Phase::rearming && !word(0xffff9100) && !byte(0xffff90ff))state=Phase::ready;
            if(state==Phase::takeoff && !active(owner))state=Phase::complete;
        }
        if(++frames>2400)throw std::runtime_error("Native landing continuation exceeded its bounded window");
    }
    Phase phase()const{return state;}
    unsigned missile_count()const{return missiles;}
    unsigned missile_maximum()const{return maximum;}
    std::span<const RGB888> pixels()const{return visible.pixels;}
    std::span<const RGB888> native_pixels()const{return game.pixels();}
    uint64_t presentation_frame()const{return visible.frame;}
    uint64_t first_burner_frame()const{return burner_frame;}
    size_t buffered_frames()const{return withheld.size();}
    const std::vector<InteractiveSceneCapture::Shot>& approach()const{return approach_shots;}
    uint64_t native_frames()const{return game.board().frames;}
private:
    XBoardGame game;
    InteractiveSceneCapture capture;
    Surface surface;
    Phase state=Phase::rollout;
    uint32_t owner{};
    unsigned missiles{},maximum{},reload_clock{},frames{};
    bool seen_reload{};
    uint64_t burner_frame{};
    struct Frame{uint64_t frame{};std::vector<RGB888> pixels;};
    Frame visible;
    std::deque<Frame> withheld;
    std::vector<InteractiveSceneCapture::Shot> approach_shots;
    uint16_t word(uint32_t a)const{return NativeStageSkip::peek16(game.board(),a);}
    uint8_t byte(uint32_t a)const{return game.board().memory.bytes.at(MainBus::memory_offset(a));}
    bool active(uint32_t p)const{return byte(p)&0x80;}
    void save_approach(){auto shot=capture.shot();if(shot.has_aircraft()){
        if(approach_shots.size()>=256)approach_shots.erase(approach_shots.begin()+1);
        approach_shots.push_back(std::move(shot));}}
    void tick(const GameControls& c){capture.begin_frame();auto pins=arcade_inputs(c);XBoardGame::Host::Input in;in.buttons=pins.buttons;in.analog=pins.analog;game.tick(in);game.take_audio();}
};

#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace Feedback {
enum class SequenceCue { touchdown, launch, coupling, refill };
struct SequenceEvent {SequenceCue cue;uint64_t ns{},sequence{};};
enum class Motion { none, approach, rollout, parked, spool, roll, climb };
struct SequenceState {Motion motion{};uint16_t speed{},timer{};bool refueling{};};
// Read-only presentation observations. Enhancement envelopes live in the frontend.
class SequenceCapture {
public:
    uint32_t base_record{},finale_record{};
    template<class Phase> static bool interesting(const Phase& p){
        const auto a=p.address&0x3fffff;
        return !p.cpu&&p.write&&!p.space && (p.pc==0x111a6||p.pc==0x11406||p.pc==0x114e8||
            p.pc==0x165a6||p.pc==0x11b18||p.pc==0x11f4c||a==0x3f90b0||a==0xe8016||a==0xe8017);
    }
    template<class Phase> void observe(const Phase& p,uint16_t mode,bool refueling){
        if(!interesting(p))return;
        const auto a=p.address&0x3fffff;
        // $11B18 starts the finale controller; its adjacent +64 record is
        // the aircraft ($11E3E then $11F16). $11F4C marks deck contact
        // with TAS +24; repeated TAS writes must produce only one kick.
        if(mode==16 && p.pc==0x11b18 && p.mask==0xffff && (a&63)==18 && p.data==0x1b30){finale_record=a-18+64;finale_landed=false;}
        if(mode==16 && finale_record && p.pc==0x11f4c && a==finale_record+24 && (p.mask&0xff00) && (p.data&0x8000) && !finale_landed){finale_landed=true;emit(SequenceCue::touchdown,p.ns);}
        if(mode==16 && p.pc==0x111a6 && p.mask==0xffff && (a&63)==18 && p.data==0x11be)base_record=a-18;
        // $11406 marks landing-surface arrival (Y > $84) and starts $12540 scenery.
        if(mode==16 && base_record && p.pc==0x11406 && a==base_record+60 && (p.mask&0xff00)&&(p.data&0x100))emit(SequenceCue::touchdown,p.ns);
        // $114E8 moves the airbase controller from parked (8) to departure (12).
        if(mode==16 && base_record && p.pc==0x114e8 && a==base_record+40 && p.mask==0xffff&&p.data==12)emit(SequenceCue::launch,p.ns);
        // Carrier state 3 begins the verified departure acceleration at $104F8.
        if(mode==15 && a==0x3f90b0 && p.mask==0xffff && p.data==3)emit(SequenceCue::launch,p.ns);
        // $165A6: nozzle animation reaches the transfer pose and requests refill.
        if(mode==16 && refueling && p.pc==0x165a6 && a==0x3f9102 && (p.mask&0xff00)&&(p.data&0x100))emit(SequenceCue::coupling,p.ns);
        // Actual $9B delivery to the sound latch, after queueing by $16D00.
        if(mode==16 && refueling && p.pc==0xb81e && (a&~1u)==0xe8016 && (p.mask&255) && (p.data&255)==0x9b)emit(SequenceCue::refill,p.ns);
    }
    static SequenceState state(uint16_t mode,uint16_t carrier,uint16_t speed,uint16_t timer,bool refueling,
                               uint32_t handler,uint16_t base_state,bool landed,uint16_t base_speed,
                               uint32_t finale_handler=0,uint16_t finale_flags=0){
        SequenceState s{Motion::none,speed,timer,mode==16&&refueling};
        if(mode==15){
            if(carrier==2)s.motion=Motion::spool;
            else if(carrier==3||carrier==4)s.motion=Motion::roll;
            else if(carrier>=5&&carrier<=10)s.motion=Motion::climb;
        } else if(mode==16 && handler==0x111be)s.motion=Motion::approach;
        else if(mode==16 && handler==0x1122e){
            s.speed=base_speed;
            if(base_state==0)s.motion=Motion::approach;
            else if(base_state==4)s.motion=landed?Motion::rollout:Motion::approach;
            else if(base_state==8)s.motion=Motion::parked;
            else if(base_state==12)s.motion=base_speed<0x140?Motion::roll:Motion::climb;
            else if(base_state==16)s.motion=Motion::climb;
        }
        // The finale lands across modes $10/$11/$12. Bit 7 at aircraft
        // +24 marks deck contact; bit 6 ends the rollout/attached phase.
        // $120AA is the settled ending display, not continuing wheel motion.
        if(mode>=16 && mode<=20 && (finale_handler==0x11e3e||finale_handler==0x11f16||finale_handler==0x120aa)){
            s.motion=Motion::none;
            if(finale_handler==0x11e3e)s.motion=Motion::approach;
            else if(finale_handler==0x11f16 && !(finale_flags&0x4000))s.motion=(finale_flags&0x8000)?Motion::rollout:Motion::approach;
        }
        // Publish only operands used by this phase. For example, startup RAM
        // speed before a carrier controller exists is not haptic telemetry.
        if(s.motion!=Motion::rollout&&s.motion!=Motion::roll&&s.motion!=Motion::climb)s.speed=0;
        if(s.motion!=Motion::spool)s.timer=0;
        return s;
    }
    std::vector<SequenceEvent> take(){auto out=std::move(events);events.clear();return out;}
    void clear(){base_record=finale_record=0;finale_landed=false;sequence=0;events.clear();}
private:
    bool finale_landed{};uint64_t sequence{};std::vector<SequenceEvent> events;
    void emit(SequenceCue cue,uint64_t ns){if(events.size()<128)events.push_back({cue,ns,++sequence});}
};
}

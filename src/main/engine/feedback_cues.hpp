#pragma once
#include <cstdint>
#include <vector>
#include "engine/feedback_sequences.hpp"

namespace Feedback {
enum class Cue { cannon, missile, impact };
struct Event { Cue cue; uint64_t ns{}, sequence{}; bool right{}; };
struct Frame { uint64_t number{}, ns{}; bool flight{}, boost{}; std::vector<Event> events; bool damage{}; SequenceState motion;std::vector<SequenceEvent> sequence_events; };
// Passive completed bus-write observations, never controls or sound amplitude.
// ROM producer PCs commit the low word of a successfully allocated handler.
class Capture {
public:
    SequenceCapture sequences;
    template<class Phase> void observe(const Phase& p, bool flight, uint16_t launches=0) {
        const auto address=p.address&0x3fffff;
        if(!flight || p.cpu || !p.write || p.space || p.mask!=0xffff ||
           address<0x3fa000 || address>=0x3fe000 || (address&63)!=18)return;
        if(p.pc==0x9ef6 && p.data==0xa030)emit(Cue::cannon,p.ns);
        // $9EC4 increments the successful-launch count before $9FC2 selects
        // -32 (left) for odd counts, +32 (right) for even counts.
        if(p.pc==0x9ebc && p.data==0x9f7c)emit(Cue::missile,p.ns,!(uint16_t(launches+1)&1));
        if(p.pc==0x9c20 && p.data==0xe662){damage_record=address-18;emit(Cue::impact,p.ns);}
    }
    Frame take(uint64_t number,uint64_t ns,uint16_t mode,uint16_t throttle, uint32_t handler=0, uint16_t state=0) {
        Frame f{number,ns,mode==0x10,mode==0x10 && int16_t(throttle)>=0xfd,std::move(events),false,{}, {}};
        // $E8D0 intact states 0..4 draw the falling/rolling jet; state 5 hides it.
        // $E94E breakup state 0 draws fuselage; later states are hidden decay.
        f.damage=f.flight && (handler==0xe662 || (handler==0xe8d0 && state<=4) || (handler==0xe94e && state==0));
        events.clear();return f;
    }
    uint32_t player_record() const {return damage_record;}
    void clear(){events.clear();sequence=0;damage_record=0;sequences.clear();}
private:
    uint32_t damage_record{};uint64_t sequence{};std::vector<Event> events;
    void emit(Cue cue,uint64_t ns,bool right=false){if(events.size()<128)events.push_back({cue,ns,++sequence,right});}
};
}

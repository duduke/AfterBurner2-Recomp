#pragma once
#include <cstdint>
#include <vector>

namespace Feedback {
enum class Cue { cannon, missile, impact };
struct Event { Cue cue; uint64_t ns{}, sequence{}; };
struct Frame { uint64_t number{}, ns{}; bool flight{}, boost{}; std::vector<Event> events; };
// Passive completed bus-write observations, never controls or sound amplitude.
// ROM producer PCs commit the low word of a successfully allocated handler.
class Capture {
public:
    template<class Phase> void observe(const Phase& p, bool flight) {
        const auto address=p.address&0x3fffff;
        if(!flight || p.cpu || !p.write || p.space || p.mask!=0xffff ||
           address<0x3fa000 || address>=0x3fe000 || (address&63)!=18)return;
        if(p.pc==0x9ef6 && p.data==0xa030)emit(Cue::cannon,p.ns);
        if(p.pc==0x9ebc && p.data==0x9f7c)emit(Cue::missile,p.ns);
        if(p.pc==0x9c20 && p.data==0xe662)emit(Cue::impact,p.ns);
    }
    Frame take(uint64_t number,uint64_t ns,uint16_t mode,uint16_t throttle) {
        Frame f{number,ns,mode==0x10,mode==0x10 && int16_t(throttle)>=0xfd,std::move(events)};
        events.clear();return f;
    }
    void clear(){events.clear();sequence=0;}
private:
    uint64_t sequence{};std::vector<Event> events;
    void emit(Cue cue,uint64_t ns){if(events.size()<128)events.push_back({cue,ns,++sequence});}
};
}

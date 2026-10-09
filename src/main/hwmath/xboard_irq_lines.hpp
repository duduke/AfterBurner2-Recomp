#pragma once
#include <array>
#include <cstdint>
#include <limits>

// segaxbd_state::update_main_irqs and device_input::set_state_synced:
// each line has one pending synchronization timer. Later queued changes
// drain with its first deadline, rather than scheduling a second timer.
class XBoardMainIrqLines {
public:
    uint64_t deadline() const {
        uint64_t result=std::numeric_limits<uint64_t>::max();
        for(const auto& l:lines)if(l.pending&&l.deadline<result)result=l.deadline;
        return result;
    }
    void update(bool timer,bool blank,uint64_t ns) {
        const unsigned irq=(timer?2:0)|(blank?4:0);
        if(!timer)queue(0,false,ns);
        if(!blank)queue(1,false,ns);
        if(irq!=6)queue(2,false,ns);
        if(irq)queue(irq/2-1,true,ns);
    }
    void deliver(uint64_t ns) {
        for(auto& l:lines)if(l.pending&&l.deadline<=ns){l.asserted=l.final;l.pending=false;}
    }
    unsigned level() const {
        for(unsigned i=3;i--;)if(lines[i].asserted)return (i+1)*2;
        return 0;
    }
private:
    struct Line {bool asserted=false,pending=false,final=false;uint64_t deadline=0;};
    std::array<Line,3> lines{};
    void queue(unsigned i,bool state,uint64_t ns) {
        auto& l=lines[i];if(!l.pending)l.deadline=ns;
        l.pending=true;l.final=state;
    }
};

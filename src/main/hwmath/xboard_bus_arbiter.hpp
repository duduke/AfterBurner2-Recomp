#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>

// MAME schedule.cpp execution grants (BSD-3-Clause, Aaron Giles). Times are attoseconds; retaining the
// fractional timer deadline matters when converting a grant to CPU cycles.
// CPU callbacks stop at their actual bus/internal completion yield. Their
// local clocks can exceed the target; shared writes take effect in grant
// execution order, including main writes ahead of the sub local clock.
class XBoardBusArbiter {
public:
    using Time=unsigned __int128; // long-running host clock, without attosecond wrap
    static constexpr Time CYCLE=80000000000ULL;
    static constexpr Time SOUND_CYCLE=250000000000ULL;
    static constexpr Time DEFAULT_QUANTUM=1000000000000000000ULL/6000;
    static constexpr Time IRQ_QUANTUM=4*CYCLE;
    static constexpr Time IRQ_DURATION=100000000000000ULL;
    struct State { Time base{},boost_expiry{};std::array<Time,3> local{}; };
    using Run=std::function<Time(unsigned,Time,unsigned)>;
    explicit XBoardBusArbiter(State initial):s(initial),boosted(initial.base<initial.boost_expiry){}
    Time quantum()const{return boosted?IRQ_QUANTUM:DEFAULT_QUANTUM;}
    const State& state()const{return s;}
    void boost(Time at){s.boost_expiry=std::max(s.boost_expiry,at+IRQ_DURATION);boosted=true;}
    // The deadline is an explicit boundary for devices outside this bounded
    // main/sub execution port. CPU order and budget conversion are computed.
    void timers_delivered(){boosted=s.base<s.boost_expiry;}
    Time grant(Time deadline,const Run& run,const std::array<bool,3>& enabled={true,true,true}){
        if(deadline<=s.base)throw std::runtime_error("undelivered arbiter timer");
        Time target=std::min(deadline,s.base+quantum());
        for(unsigned cpu=0;cpu<3;++cpu){const Time cycle=cpu==2?SOUND_CYCLE:CYCLE;
            if(enabled[cpu]&&target>=s.local[cpu]+cycle){auto cycles=(target-s.local[cpu])/cycle;
                auto end=run(cpu,target,unsigned(cycles));
                if(end<s.local[cpu]||(end-s.local[cpu])%cycle)throw std::runtime_error("invalid CPU completion yield");
                s.local[cpu]=end;if(end<target)target=std::max(end,s.base);
            }
        }
        s.base=target;return target;
    }
private:
    State s;bool boosted=false;
};

#pragma once
#include "engine/osubprogress.hpp"

// Sub VBLANK exception entry, literal $55E..$592 service, and retained
// supervisor/user stack banks. Board delivery is selected by the scheduler.
class OSubIrq {
public:
    struct State : OSubProgress::State { uint32_t usp{},ssp{}; uint64_t cycle_bias{}; };
    using Clock=OSubProgress::Clock;
    using Phase=OSubProgress::Phase;
    State enter(SubBus&,State,uint32_t interrupted_bus_pc,const Clock&,const Phase&) const;
    using Poll=std::function<unsigned(uint64_t)>;
    struct Instruction { State state; unsigned irq{}; uint64_t poll_ns{}; };
    State step(SubBus&,State,const Clock&,const Phase&,const OSubProgress::Select& = {}) const;
    Instruction instruction(SubBus&,State,const Clock&,const Phase&,const Poll&) const;
};

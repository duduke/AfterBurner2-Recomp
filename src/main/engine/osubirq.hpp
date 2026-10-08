#pragma once
#include "engine/osubprogress.hpp"

// Sub VBLANK exception entry, literal $55E..$592 service, and retained
// supervisor/user stack banks. Board delivery is selected by the scheduler.
class OSubIrq {
public:
    static bool startup_instruction(uint32_t pc) {
        switch(pc){case 0x402:case 0x408:case 0x40c:case 0x40e:
        case 0x414:case 0x418:case 0x41a:case 0x41c:case 0x42a:case 0x42e:
        case 0x62e:case 0x632:case 0x634:case 0x636:case 0x63a:case 0x63c:case 0x63e:return true;}
        return false;
    }
    struct State : OSubProgress::State { uint32_t usp{},ssp{}; uint64_t cycle_bias{}; };
    using Clock=OSubProgress::Clock;
    using Phase=OSubProgress::Phase;
    // Optional scheduler checkpoint at the computed VPA-ready E-clock.
    // CPU-space acknowledgement costs five clocks; no program bus phase.
    State enter(SubBus&,State,uint32_t interrupted_bus_pc,const Clock&,const Phase&,const Clock& acknowledge = {}) const;
    // Hardware RESET state, not the main CPU's RESET opcode. General
    // registers/USP survive; SSP/PC are read from the ROM reset vectors.
    State reset(SubBus&,State,uint32_t interrupted_bus_pc,const Clock&,const Phase&) const;
    using Poll=std::function<unsigned(uint64_t)>;
    struct Instruction { State state; unsigned irq{}; uint64_t poll_ns{}; };
    using Checkpoint=std::function<void(const State&)>;
    State step(SubBus&,State,const Clock&,const Phase&,const OSubProgress::Select& = {},const Checkpoint& = {}) const;
    Instruction instruction(SubBus&,State,const Clock&,const Phase&,const Poll&,const Checkpoint& = {}) const;
};

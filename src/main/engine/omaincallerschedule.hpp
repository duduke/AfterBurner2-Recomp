#pragma once
#include "engine/omainirqservice.hpp"
#include <stdexcept>

// Independently advance the verified caller graph and exception entry from
// computed CPU state. Poll is a physical board scheduler, not an IRQ transcript.
// Initial state, external handler/ISR returns and user-mode SSP are explicit
// boundaries. Unknown instruction PCs remain OMainIrqService errors.
class OMainCallerSchedule {
public:
    using Result = OMainIrqService::Result;
    using Instruction = OMainIrqService::Instruction;
    void resume(const Result& state) { current = {state,0,0}; }
    const Instruction& state() const { return current; }
    Instruction step(MainBus& bus, const OMainIrqService::Clock& clock,
                     const OMainIrqService::Poll& physical) {
        if(current.irq) throw std::runtime_error("caller IRQ entry is pending");
        completed_pc=current.state.pc;
        current = service.caller_step(bus,context(),clock,physical);
        return current;
    }
    // A user-mode interrupted caller cannot establish the supervisor stack.
    Result enter(MainBus& bus,uint32_t supervisor_sp,const OMainIrqService::Clock& clock) {
        if(!current.irq) throw std::runtime_error("caller has no selected IRQ");
        auto c=context();c.interrupted_pc=completed_pc;c.level=current.irq;c.return_pc=current.state.pc;
        if(!(c.sr&0x2000))c.supervisor_sp=supervisor_sp;
        auto result=service.enter(bus,c,clock);current={result,0,0};return result;
    }
    uint32_t current_pc() const { return service.current_pc(); }
private:
    OMainIrqService service;
    Instruction current{};
    uint32_t completed_pc{};
    OMainIrqService::Context context() const {
        OMainIrqService::Context c{};c.registers=current.state.registers;
        c.boundary_ns=current.state.ns;c.interrupted_pc=current.state.pc;
        c.supervisor_sp=current.state.sp;c.sr=current.state.sr;return c;
    }
};

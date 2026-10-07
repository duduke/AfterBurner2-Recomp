#pragma once
#include "engine/omainirqservice.hpp"
#include <stdexcept>
#include <optional>

// Independently advance the verified caller graph and exception entry from
// computed CPU state. Poll is a physical board scheduler, not an IRQ transcript.
// Initial state, unported handler progress and initial user-mode SSP are
// explicit boundaries. Returns retain SSP/USP and may select another IRQ.
// Unknown instruction PCs remain OMainIrqService errors.
class OMainCallerSchedule {
public:
    using Result = OMainIrqService::Result;
    using Instruction = OMainIrqService::Instruction;
    void resume(const Result& state,std::optional<uint32_t> usp = {}) {
        current = {state,0,0};entered=false;ssp_known=false;
        usp_known=!(state.sr&0x2000)||usp.has_value();
        if(usp_known)user_sp=(state.sr&0x2000)?*usp:state.sp;
    }
    // Initial unported instruction completion is an explicit boundary; a
    // chained instruction or RTE must use step()/finish() instead.
    void resume_instruction(const Instruction& state,uint32_t interrupted_pc,
                            std::optional<uint32_t> usp = {}) {
        resume(state.state,usp);current=state;completed_pc=interrupted_pc;
    }
    const Instruction& state() const { return current; }
    Instruction step(MainBus& bus, const OMainIrqService::Clock& clock,
                     const OMainIrqService::Poll& physical) {
        if(entered) throw std::runtime_error("caller handler return is pending");
        if(current.irq) throw std::runtime_error("caller IRQ entry is pending");
        completed_pc=current.state.pc;
        current = service.caller_step(bus,context(),clock,physical);
        return current;
    }
    // A user-mode interrupted caller needs an initial supervisor stack.
    // A linked RTE already computed that stack; retain it for immediate entry.
    Result enter(MainBus& bus,uint32_t supervisor_sp,const OMainIrqService::Clock& clock) {
        if(!current.irq) throw std::runtime_error("caller has no selected IRQ");
        auto c=context();c.interrupted_pc=completed_pc;c.level=current.irq;c.return_pc=current.state.pc;
        if(!(c.sr&0x2000))c.supervisor_sp=ssp_known?returned_ssp:supervisor_sp;
        if(!(c.sr&0x2000)){user_sp=current.state.sp;usp_known=true;}
        auto result=service.enter(bus,c,clock);current={result,0,0};entered=true;return result;
    }
    // All three handlers restore SR before selecting physical IPL. IRQ4
    // game-flow callbacks remain explicit boundaries until their timing is ported.
    Result finish(MainBus& bus,const OMainIrqService::Clock& clock,
                  const OMainIrqService::Poll& physical = {},
                  const OMainIrqService::Boundary& boundary = {},
                  const OMainIrqService::LeafObserver& observer = {}) {
        if(!entered) throw std::runtime_error("caller has no entered handler");
        unsigned selected=0;uint64_t selection=0;
        auto result=service.finish(bus,current.state,clock,boundary,observer,
            [&](uint16_t sr,uint64_t ns) {
                if(!physical)return;
                selection=ns;auto level=physical(ns);
                if(level!=0&&level!=2&&level!=4&&level!=6)
                    throw std::runtime_error("unsupported RTE IPL");
                selected=OMainIrqService::eligible(level,sr)?level:0;
            });
        returned_ssp=result.sp;ssp_known=true;
        // An immediate exception retains the RTE CURPC: the returned
        // instruction has not executed or changed the bus attribution.
        completed_pc=service.current_pc();
        // RTE switches back to the USP retained across exception entry.
        if(!(result.sr&0x2000)){
            if(!usp_known)throw std::runtime_error("RTE user stack was not initialized");
            result.sp=user_sp;
        }
        current={result,selected,selection};entered=false;return result;
    }
    uint32_t supervisor_stack() const { return returned_ssp; }
    uint32_t current_pc() const { return service.current_pc(); }
private:
    OMainIrqService service;
    Instruction current{};
    uint32_t completed_pc{},user_sp{},returned_ssp{};
    bool entered=false,ssp_known=false,usp_known=false;
    OMainIrqService::Context context() const {
        OMainIrqService::Context c{};c.registers=current.state.registers;
        c.boundary_ns=current.state.ns;c.interrupted_pc=current.state.pc;
        c.supervisor_sp=current.state.sp;c.sr=current.state.sr;return c;
    }
};

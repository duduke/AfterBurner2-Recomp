#pragma once
#include <functional>
#include "engine/omainobjects.hpp"

// Bounded 12.5 MHz 68000 service port: selected IRQ2/IRQ4/IRQ6 exception entry,
// Main ROM $1461C/$1466C/$14674, frame/road/cycle/fade/animation palettes,
// reload/HUD/sound, scenery, transitions and MOVEM/JSR/RTS/RTE bus phases.
// IRQ4 game-flow callees compute their instruction timing and bus phases.
// The interrupted instruction's completion boundary and latched level are
// caller inputs; this is not an interpreter for the unported main program.
class OMainIrqService {
public:
    struct Context {
        OMainObjects::Registers registers;
        uint64_t boundary_ns;
        uint32_t interrupted_pc, return_pc, supervisor_sp;
        uint16_t sr;
        unsigned level;
    };
    struct Result { OMainObjects::Registers registers; uint32_t pc, sp; uint16_t sr; uint64_t ns; };
    using Clock = std::function<void(uint64_t)>;
    static constexpr uint64_t CYCLE_NS = 80;
    // Internal tail after an interrupted instruction's FINAL bus phase.
    // Register shifts and register/address long arithmetic do not finish
    // at their final prefetch. Memory RMW writes are already the final bus.
    static unsigned completion_tail(uint16_t opcode, uint16_t shift_source);
    // Board levels 0/2/4/6 only; level-seven NMI is outside this port.
    static bool eligible(unsigned level,uint16_t sr) { return level>((sr>>8)&7); }
    // Legacy callback type retained for caller API compatibility. The closed
    // IRQ4 callee graph executes without invoking a timing/state boundary.
    using Boundary = std::function<unsigned(uint32_t, OMainObjects::Registers&, uint32_t, uint64_t)>;
    using LeafObserver = std::function<void(bool, uint32_t, const OMainObjects::Registers&, uint64_t)>;
    Result run(MainBus&, const Context&, const Clock&, const Boundary& = {}, const LeafObserver& = {});
    // Continue an already entered handler from computed entry outputs. No
    // second exception entry or captured handler context is consumed.
    // RTE restores SR before its final return prefetch selects a pending IRQ.
    using ReturnSelection = std::function<void(uint16_t, uint64_t)>;
    Result finish(MainBus&, const Result&, const Clock&, const Boundary& = {}, const LeafObserver& = {}, const ReturnSelection& = {});
    // One uninterrupted $14856 spin iteration, or its ready/re-arm/RTS
    // path (Context.supervisor_sp is the active user/supervisor stack here).
    // Waiting returns PC=$14856 so a scheduler can service an IRQ.
    Result logic_step(MainBus&, const Context&, const Clock&);
    // One address-labelled wait/dispatcher, sprite clear/publication,
    // follower, ready-bit, world projection, sound-ring, input, flight,
    // shadow/exhaust, weapon controller/fire/allocation, HUD or missile-init instruction,
    // including ROM prefetches. Poll samples physical IPL at the CPU's selection phase;
    // the returned level is latched, not reselected at instruction completion.
    using Poll = std::function<unsigned(uint64_t)>;
    struct Instruction { Result state; unsigned irq; uint64_t poll_ns; };
    Instruction caller_step(MainBus&, const Context&, const Clock&, const Poll&, const Clock& reset = {});
    // Exception entry only, ending after both handler prefetch phases. The
    // instruction result supplies return PC/SR/registers and completion time.
    Result enter(MainBus&, const Context&, const Clock&);
    // Complete program-bus entry for the joint scheduler. The acknowledge
    // hook owns the VPA frontier and its five CPU-space clocks. Selection
    // samples with the handler SR before the second handler prefetch.
    Result enter_joint(MainBus&, const Context&, const Clock&, uint64_t cycle_bias, const Clock& acknowledge, const ReturnSelection& = {});
    // Literal RTE at the main IRQ and RESET return sites. USP is retained by
    // the scheduler; the completed supervisor stack advances by six bytes.
    Instruction return_joint(MainBus&, const Result&, uint32_t usp, const Clock&, const Poll&);
    Instruction trap_joint(MainBus&, const Result&, uint32_t ssp, const Clock&, const Poll&);
    uint32_t current_pc() const { return pc; }
private:
    Result enter_impl(MainBus&, const Context&, const Clock&, uint64_t, const Clock&, bool, const ReturnSelection& = {});
    MainBus* bus{};
    Clock clock;
    LeafObserver observe_leaf;
    uint64_t ns{};
    uint32_t pc{};
    bool callee_x=false;
    Result irq4(OMainObjects::Registers, uint32_t, const Boundary&, const ReturnSelection&);
    uint32_t frame_complex(uint32_t, OMainObjects::Registers&, uint32_t);
    uint32_t frame_leaf(uint32_t, OMainObjects::Registers&, uint32_t);
    void idle(unsigned cycles);
    uint16_t read16(uint32_t);
    uint32_t read32(uint32_t);
    uint8_t read8(uint32_t);
    void write16(uint32_t,uint16_t);
    void write8(uint32_t,uint8_t);
};

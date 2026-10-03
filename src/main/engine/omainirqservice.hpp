#pragma once
#include <functional>
#include "engine/omainobjects.hpp"

// Bounded 12.5 MHz 68000 service port: selected IRQ2/IRQ4/IRQ6 exception entry,
// main ROM $1461C/$1466C/$14674, selected frame leaves, the $B7F8
// consumer and MOVEM/JSR/RTS/RTE bus phases. Complex IRQ4 callees
// retain explicit timing/state boundaries.
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
    // Complex IRQ4 callees remain explicit timing/state boundaries. The
    // callback receives the predicted callee start and actual return stack;
    // it returns a duration in CPU clocks, never an absolute clock reset.
    using Boundary = std::function<unsigned(uint32_t, OMainObjects::Registers&, uint32_t, uint64_t)>;
    using LeafObserver = std::function<void(bool, uint32_t, const OMainObjects::Registers&, uint64_t)>;
    Result run(MainBus&, const Context&, const Clock&, const Boundary& = {}, const LeafObserver& = {});
    // One uninterrupted $14856 spin iteration, or its ready/re-arm/RTS
    // path (Context.supervisor_sp is the active user/supervisor stack here).
    // Waiting returns PC=$14856 so a scheduler can service an IRQ.
    Result logic_step(MainBus&, const Context&, const Clock&);
    uint32_t current_pc() const { return pc; }
private:
    MainBus* bus{};
    Clock clock;
    LeafObserver observe_leaf;
    uint64_t ns{};
    uint32_t pc{};
    Result irq4(OMainObjects::Registers, uint32_t, const Boundary&);
    uint32_t frame_leaf(uint32_t, OMainObjects::Registers&, uint32_t);
    void idle(unsigned cycles);
    uint16_t read16(uint32_t);
    uint32_t read32(uint32_t);
    uint8_t read8(uint32_t);
    void write16(uint32_t,uint16_t);
    void write8(uint32_t,uint8_t);
};

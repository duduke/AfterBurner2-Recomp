#pragma once
#include "engine/omainobjects.hpp"

// Host composition of independently verified main-CPU routines. CPU/board
// eligibility, interrupts, stage/credit callers and paired video acceptance
// remain separate contracts; this is not a main-program interpreter.
class OMainHost {
public:
    using Registers = OMainObjects::Registers;
    using Call = OMainObjects::Call;
    // False means an unknown entry, with no writes or register changes.
    // A known routine may still stop at its named unported 3e callee.
    static bool execute(MainBus&, Registers&, uint32_t, const Call& boundary = {});
    static void call(MainBus&, Registers&, uint32_t, const Call& boundary = {});
    using Observe = std::function<void(bool entering, uint32_t, const Registers&)>;
    static void dispatch(MainBus&, Registers&, const Call& boundary = {}, const Observe& = {});
};

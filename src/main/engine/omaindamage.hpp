#pragma once
#include "engine/omainobjects.hpp"

// Independently translated damage controllers (states 0..6) and debris,
// including the companion emission/animation/pool-reset graph.
// Life/HUD/credit flow remains an explicit Phase 3e boundary. These routines
// are not yet connected to the host dispatcher.
class OMainDamage {
public:
    static void run(MainBus&, OMainObjects::Registers&, uint32_t entry);
};

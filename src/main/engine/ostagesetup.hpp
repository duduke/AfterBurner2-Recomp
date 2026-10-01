#pragma once
#include "engine/mainbus.hpp"

// Road/scenery setup slices; entry points identify the literal ROM routines.
// Stage scripts and the full credit/game-flow dispatcher are Phase 3e.
class OStageSetup
{
public:
    void run(MainBus& bus, uint32_t entry);
};

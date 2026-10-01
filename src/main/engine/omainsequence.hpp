#pragma once
#include "engine/omainobjects.hpp"

// ROM sequence controller, carrier parts, horizon and refuel mechanics. Independent
// routine ports; stage/credit callers and host scheduling remain separate.
class OMainSequence {
public:
    static void run(MainBus&, OMainObjects::Registers&, uint32_t entry);
};

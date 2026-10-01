#pragma once
#include "engine/omainobjects.hpp"

// $00B0AC: sampled arcade axes -> road targets. Registers and history stay
// in their original slots; the host ADC is a separate hardware boundary.
class OMainInput {
public:
    static void update(MainBus&, OMainObjects::Registers&);
    static void buttons(MainBus&, OMainObjects::Registers&); // $014976
};

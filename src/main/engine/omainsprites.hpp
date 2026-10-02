#pragma once
#include "engine/mainbus.hpp"
#include "engine/omainobjects.hpp"
// Literal main sprite request/publication/blit routines. Waiting for the
// interrupt is a host scheduling boundary, never a C++ spin loop.
class OMainSprites {
public:
    void initialize(MainBus&);
    void append(MainBus&,uint32_t object);
    bool publish(MainBus&);
    static bool publish(MainBus&, OMainObjects::Registers&);
    void blit(MainBus&,bool interpolated);
};

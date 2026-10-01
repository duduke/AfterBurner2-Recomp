#pragma once
#include "engine/mainbus.hpp"
// Literal main sprite request/publication/blit routines. Waiting for the
// interrupt is a host scheduling boundary, never a C++ spin loop.
class OMainSprites {
public:
    void initialize(MainBus&);
    void append(MainBus&,uint32_t object);
    bool publish(MainBus&);
    void blit(MainBus&,bool interpolated);
};

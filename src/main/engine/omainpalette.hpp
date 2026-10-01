#pragma once
#include "engine/mainbus.hpp"
// Literal main ROM palette routines. All tables are read through MainBus.
class OMainPalette {
public:
    void run(MainBus&, uint32_t entry);
private:
    void fade(MainBus&);
    void animate(MainBus&);
    void blend(MainBus&);
    void upload(MainBus&);
};

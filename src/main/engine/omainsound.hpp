#pragma once
#include "engine/omainobjects.hpp"

// Original command ring and timer-IRQ consumer. Producer timing/callers must
// be ported separately before replacing the native game's sound events.
class OMainSound {
public:
    static void consume(MainBus&, OMainObjects::Registers&); // $00B7F8
    static void enqueue(MainBus&, OMainObjects::Registers&, bool gameplay); // $00B840/$00B84C
};

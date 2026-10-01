#pragma once
#include "engine/mainbus.hpp"

// Literal main-ROM scenery routines. Kept off the reconstruction's frame
// loop until stage setup and road inputs are ported and verified too.
class OScenery
{
public:
    void stage_events(MainBus& bus); // $018444
    void stage_view(MainBus& bus);   // $018798
private:
    void init_view(MainBus& bus);    // $01871A
    void byte_tiles(MainBus& bus, uint32_t src, uint32_t dst); // $018902, includes header loads
    void tile_list(MainBus& bus, uint32_t src, uint32_t dst);  // $01866C
    void scroll(MainBus& bus);       // $01854C
};

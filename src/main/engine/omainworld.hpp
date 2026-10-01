#pragma once
#include "engine/omainobjects.hpp"

class OMainWorld {
public:
    static void initialize(MainBus&, OMainObjects::Registers&); // $0088C2
    static void update(MainBus&, OMainObjects::Registers&); // $008AF2
    static void explosion(MainBus&, OMainObjects::Registers&); // $0091C0
    static void effect_initialize(MainBus&, OMainObjects::Registers&, bool); // $008D44/$008D58
    static void effect_update(MainBus&, OMainObjects::Registers&, bool); // $008E18/$008ECE
    static void player_collision(MainBus&, OMainObjects::Registers&); // $008CC8
    static void release(MainBus&, OMainObjects::Registers&); // $009556
    // Remaining world/explosion graph. Score request $E542 is a named 3e
    // boundary; the callback must be supplied by a verified caller.
    static void run(MainBus&, OMainObjects::Registers&, uint32_t,
                    const OMainObjects::Call& boundary = {});
};

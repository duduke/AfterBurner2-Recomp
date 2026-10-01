#pragma once
#include "engine/omainobjects.hpp"

// Independently translated damage/debris callees. The $E662 controller and
// life/credit flow remain unported; this class is not a host damage model.
class OMainDamage {
public:
    static void run(MainBus&, OMainObjects::Registers&, uint32_t entry);
};

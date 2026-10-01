#pragma once
#include "engine/mainbus.hpp"
// $014674 VBLANK scheduler. Callees are explicit boundaries: verified palette,
// scenery and sprite ports can be bound by the host; 3d/3e hooks remain external.
// Stack/register preservation belongs to a CPU interpreter, not these RAM ports.
class OMainFrame {
public:
    using Call = std::function<void(uint32_t)>;
    struct Result { bool logic; bool service_redirect; };
    Result interrupt(MainBus&, const Call&);
    bool resume_logic(MainBus&); // $014856, wait exposed to host
    void bookkeeping(MainBus&); // $0148B8
    void timers(MainBus&);       // $0148BE
    void buttons(MainBus&);      // $01495C
    void service_edge(MainBus&); // $0147E8
};

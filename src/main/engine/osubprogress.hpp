#pragma once
#include "engine/osubpublication.hpp"
// Literal bounded sprite/road instructions. IRQ services and external producer
// delivery remain explicit boundaries; this class does not invent a frame clock.
class OSubProgress {
public:
    using State=OSubPublication::State;
    using Clock=OSubPublication::Clock;
    using Phase=OSubPublication::Phase;
    using Select=std::function<void(uint64_t,uint16_t)>;
    using Checkpoint=std::function<void(const State&)>;
    State step(SubBus&,State,const Clock&,const Phase&,const Select& = {},const Checkpoint& = {}) const;
};

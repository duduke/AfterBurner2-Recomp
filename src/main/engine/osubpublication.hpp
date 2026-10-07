#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include "engine/subbus.hpp"

// Bounded sub CPU wait/release instructions. Entry/IRQ/callee frontiers remain
// explicit; this does not assign a frame duration to untimed road/sprite work.
class OSubPublication {
public:
    static constexpr uint64_t CYCLE_NS=80;
    struct State { std::array<uint32_t,8> d{};std::array<uint32_t,7> a{};
        uint64_t ns{};uint32_t pc{},sp{};uint16_t sr{}; };
    using Clock=std::function<void(uint64_t)>;
    using Phase=std::function<void(bool,uint32_t,uint16_t,uint16_t,uint32_t,uint64_t)>;
    State step(SubBus&,State,const Clock&,const Phase&) const;
    // Values of already verified main signal writers, at supplied write
    // frontiers. Operand is retained from the actual RMW read frontier.
    // Addresses are main aliases of the same shared RAM.
    static bool main_signal(SubBus&,uint32_t pc,uint16_t operand);
};

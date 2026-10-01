#pragma once
#include <array>
#include <functional>
#include <vector>
#include "engine/mainbus.hpp"

// Arcade records stay in MainBus. This is the dispatcher's register contract,
// not a C++ replacement for the 64-byte object or a CPU emulator.
class OMainObjects
{
public:
    struct Registers {
        std::array<uint32_t,8> d{};
        std::array<uint32_t,7> a{}; // A7/exception frames are outside this port.
    };
    struct Template {
        uint32_t record, handler;
        uint8_t flags;
    };
    using Call = std::function<void(uint32_t, Registers&)>;
    static std::vector<Template> inventory(MainBus&);
    static void initialize(MainBus&, Registers&); // $01488E
    static void dispatch(MainBus&, Registers&, const Call&); // $014594
    static void player_initialize(MainBus&, Registers&); // $016892
    static void player_prepare(MainBus&, Registers&); // $016976
    static void player_hide(MainBus&, Registers&); // $0169B2
    static void random(MainBus&, Registers&); // $00C5A8, D0/D1 + $8802
};

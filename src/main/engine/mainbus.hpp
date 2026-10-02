#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include "engine/subbus.hpp"
#include "hwmath/io_expander.hpp"

// Main 68000's 22-bit map, from segaxbd_state::main_map (MAME, BSD-3-Clause).
// RAM is big-endian bytes for oracle replay. Shared RAM/road and the sub's
// three math chips are the actual SubBus instance, not independent copies.
// Rendering and the ADC's timed conversion are explicit host endpoints.
// No CPU loop or ADC timing is invented here; gameplay ports come later.
class MainBus
{
public:
    enum : uint32_t
    {
        WORK = 0, SHARED = 0x8000, TILE = 0x10000, TEXT = 0x20000,
        SPRITE = 0x21000, PALETTE = 0x22000, ROAD = 0x26000,
        MEMORY_BYTES = 0x27000, UNMAPPED = 0xffffffff
    };
    struct Memory
    {
        std::array<uint8_t, MEMORY_BYTES> bytes{};
        std::array<uint8_t, 0x1000> sprite_buffer{}, road_buffer{};
        uint8_t road_control = 0;
    };
    struct Trace
    {
        uint8_t* read = nullptr;
        uint8_t* written = nullptr;
        // One transaction per bus access, including byte-lane mask. Useful
        // for comparing I/O side effects as well as final RAM write sets.
        std::function<void(bool, uint32_t, uint16_t, uint16_t)> access;
        // An externally established CPU boundary may service an IRQ before
        // the next operand access. This supplies no invented CPU timing.
        std::function<void(bool, uint32_t, uint16_t)> before_access{};
    };
    struct Endpoints
    {
        std::function<uint8_t()> adc_read;
        std::function<void(uint8_t)> adc_write;
        std::function<void(uint32_t, uint16_t)> video_write;
        std::function<void()> sprite_draw, road_swap;
        std::function<void(uint8_t)> road_control;
    } endpoints;

    MainBus(const uint8_t* main_rom, uint32_t main_length,
            const uint8_t* sub_rom, uint32_t sub_length, Memory& memory);
    MainBus(const MainBus&) = delete;
    MainBus& operator=(const MainBus&) = delete;

    uint8_t read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint32_t read32(uint32_t addr);
    void write8(uint32_t addr, uint8_t data);
    void write16(uint32_t addr, uint16_t data);
    void write32(uint32_t addr, uint32_t data);
    static uint32_t memory_offset(uint32_t addr);

    Mul5248 mul;
    Div5249 div;
    CompareTimer5250 compare_timer;
    std::array<IOExpander, 2> io;
    SubBus sub;
    Trace* trace = nullptr;

private:
    const uint8_t* rom;
    uint32_t rom_length;
    Memory& m;
    uint16_t read_word(uint32_t a, uint16_t mask);
    void write_word(uint32_t a, uint16_t data, uint16_t mask);
};

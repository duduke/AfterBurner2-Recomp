/***************************************************************************
    The sub CPU's view of memory, for routines ported from its program.

    Ported sub CPU code keeps the arcade's memory layout and addresses
    (Cannonball's approach), so its results can be compared byte-for-byte
    against MAME captures (tools/oracle/). Covers what those routines touch:
    program ROM, the 32KB block shared with the main CPU, road RAM, and the
    sub CPU's own multiplier/divider. See docs/HARDWARE.md, "Sub CPU memory
    map".

    The shared block is laid out as the sub CPU's A6-relative view
    ($09C000-$0A3FFF): subram0 at offset 0, subram1 at offset $4000. Main CPU
    pointers stored in it ($29xxxx/$2Axxxx) work unchanged because the sub
    CPU's 20-bit bus drops bit 21.

    Road RAM ($0EC000-$0ECFFF, mirrored at $0ED000) is the CPU-visible half
    of the road chip's double buffer — the same bytes HWRoad::write16 takes.
***************************************************************************/

#pragma once

#include <cstdint>
#include "hwmath/sega_math.hpp"

class SubBus
{
public:
    static constexpr uint32_t SHARED_BYTES = 0x8000;
    static constexpr uint32_t ROAD_BYTES = 0x1000;
    static constexpr uint32_t A6 = 0x09C000; // the sub CPU's base for shared RAM

    // `road` may be null for routines that don't touch road RAM.
    SubBus(const uint8_t* rom, uint32_t rom_length, uint8_t* shared, uint8_t* road = nullptr);

    uint8_t read8(uint32_t addr) const;
    uint16_t read16(uint32_t addr) const;
    uint32_t read32(uint32_t addr) const;
    void write8(uint32_t addr, uint8_t data);
    void write16(uint32_t addr, uint16_t data);
    void write32(uint32_t addr, uint32_t data);

    Mul5248 mul;
    Div5249 div;

    // Test instrumentation: when set, every byte of shared/road RAM a
    // routine reads or writes is marked (1) in these byte-per-byte arrays.
    struct Trace
    {
        uint8_t* shared_read = nullptr;
        uint8_t* shared_written = nullptr;
        uint8_t* road_written = nullptr;
    };
    Trace* trace = nullptr;

private:
    const uint8_t* rom;
    uint32_t rom_length;
    uint8_t* shared;
    uint8_t* road;
};

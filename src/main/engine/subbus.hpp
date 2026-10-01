/***************************************************************************
    The sub CPU's view of memory, for routines ported from its program.

    Ported sub CPU code keeps the arcade's memory layout and addresses
    (Cannonball's approach), so its results can be compared byte-for-byte
    against MAME captures (tools/oracle/). Covers what those routines touch:
    program ROM, the 32KB block shared with the main CPU, and the sub CPU's
    own multiplier/divider. See docs/HARDWARE.md, "Sub CPU memory map".

    The shared block is laid out as the sub CPU's A6-relative view
    ($09C000-$0A3FFF): subram0 at offset 0, subram1 at offset $4000. Main CPU
    pointers stored in it ($29xxxx/$2Axxxx) work unchanged because the sub
    CPU's 20-bit bus drops bit 21.
***************************************************************************/

#pragma once

#include <cstdint>
#include "hwmath/sega_math.hpp"

class SubBus
{
public:
    static constexpr uint32_t SHARED_BYTES = 0x8000;
    static constexpr uint32_t A6 = 0x09C000; // the sub CPU's base for shared RAM

    SubBus(const uint8_t* rom, uint32_t rom_length, uint8_t* shared);

    uint8_t read8(uint32_t addr) const;
    uint16_t read16(uint32_t addr) const;
    uint32_t read32(uint32_t addr) const;
    void write8(uint32_t addr, uint8_t data);
    void write16(uint32_t addr, uint16_t data);
    void write32(uint32_t addr, uint32_t data);

    Mul5248 mul;
    Div5249 div;

private:
    const uint8_t* rom;
    uint32_t rom_length;
    uint8_t* shared;
};

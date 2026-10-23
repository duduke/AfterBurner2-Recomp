#pragma once

#include "roms.hpp"
#include <stdexcept>

// Read-only names for the original ordered stage slots. A stage number is
// one-based; its ROM selector is a separate, nonsequential identifier.
namespace StageCatalog {
struct Stage {
    unsigned number;
    uint8_t selector;
    uint32_t descriptor;
    uint32_t events;
};

namespace Detail {
inline void range(const Roms& rom, uint32_t address, uint32_t bytes) {
    if (!rom.maincpu.rom || address > rom.maincpu.length
        || bytes > rom.maincpu.length - address)
        throw std::runtime_error("Stage catalog ROM address out of range");
}
inline uint16_t word(const Roms& rom, uint32_t address) {
    range(rom, address, 2);
    return (uint16_t(rom.maincpu.rom[address]) << 8) | rom.maincpu.rom[address + 1];
}
inline uint32_t pointer(const Roms& rom, uint32_t address) {
    range(rom, address, 4);
    return (uint32_t(word(rom, address)) << 16) | word(rom, address + 2);
}
}

inline unsigned count(const Roms& rom) {
    // $18918..$18942 copies this count and ordered byte selector list to RAM.
    const unsigned result = Detail::word(rom, 0x32480);
    if (!result || result > 64)
        throw std::runtime_error("Invalid runtime ROM stage count");
    Detail::range(rom, 0x32482, result);
    return result;
}

inline Stage get(const Roms& rom, unsigned number) {
    if (!number || number > count(rom))
        throw std::out_of_range("Stage number out of range");
    const uint8_t selector = rom.maincpu.rom[0x32482 + number - 1];
    // $13CEE/$13DBA/$13E06 select a long descriptor pointer through $3249A.
    const uint32_t descriptor = Detail::pointer(rom, 0x3249a + uint32_t(selector) * 4);
    Detail::range(rom, descriptor, 20);
    // $142D0 loads the stage event program from descriptor byte offset 16.
    const uint32_t events = Detail::pointer(rom, descriptor + 16);
    Detail::range(rom, events, 8);
    return {number, selector, descriptor, events};
}
}

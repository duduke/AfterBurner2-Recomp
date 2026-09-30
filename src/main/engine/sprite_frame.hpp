#pragma once
#include <cstdint>

struct SpriteFrame
{
    uint16_t width{120}, height{47}, bank{}, offset{0x398}, pitch{15};
};
// Sub CPU entries are 16 bytes: width, last row, bank, offset, stride, size.
inline SpriteFrame read_sprite_frame(const uint8_t* rom, uint32_t length, uint32_t address)
{
    if (!rom || address > length || length - address < 16) return {};
    const auto word = [rom, address](unsigned offset) {
        return static_cast<uint16_t>((rom[address + offset] << 8) | rom[address + offset + 1]);
    };
    return {word(0), static_cast<uint16_t>(word(2) + 1),
            static_cast<uint16_t>((word(4) >> 9) & 7), word(6), word(8)};
}

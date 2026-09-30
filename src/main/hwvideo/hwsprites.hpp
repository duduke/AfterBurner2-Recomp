/***************************************************************************
    Video Emulation: X-Board Hardware Sprite Scaler.

    Faithful reimplementation of sega_outrun_sprite_device in its X-Board
    configuration (MAME src/mame/sega/sega16sp.cpp) — a genuine hardware
    line-buffer scaler: for every scanline of every active sprite RAM
    entry, it walks the sprite graphics ROM with a Bresenham-style zoom
    accumulator and draws directly from ROM data. There is no pre-scaled
    tile set; all scaling happens at draw time. See docs/HARDWARE.md
    section 3.1 for the full entry bit layout.

    This produces raw hardware pixel words (8-bit color/palette index,
    3-bit priority, 4-bit pixel), matching MAME's bitmap_ind16 pen values
    exactly. Resolving those to actual palette entries (including how the
    3-bit priority field interacts with shadow rendering) is deliberately
    left to the top-level compositor, since that's a screen_update-level
    concern, not something the sprite hardware itself resolves.
***************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

class HWSprites
{
public:
    HWSprites();

    // Decode the raw sprite ROM (as loaded, `rom_length` bytes, 4-way
    // byte-interleaved per docs/HARDWARE.md section 6) into 32-bit words
    // matching the real ROM_REGION32_LE layout.
    void init(const uint8_t* rom, uint32_t rom_length);

    // CPU-side write into live sprite RAM (byte address within the 0x1000
    // sprite RAM window, per docs/HARDWARE.md section 2).
    void write16(uint32_t byte_addr, uint16_t data);

    // A CPU write to the draw-trigger address (110000-11FFFF) latches the
    // current sprite list for rendering — call this from that handler.
    void swap();

    // Render all active sprites into a `width`x`height` buffer of raw
    // pixel words (0 = transparent). Pixel format: bits 0-3 = 4-bit pixel
    // index (1-14 opaque, 0/15 transparent), bits 4-11 = 8-bit color
    // field, bits 12-14 = 3-bit priority.
    void render(uint16_t* pixels, int width, int height, int origin_x = 0, int origin_y = 0,
                unsigned priority_mask = 0xf) const;

private:
    static constexpr int SPRITE_ENTRIES = 256;
    static constexpr int SPRITE_WORDS = SPRITE_ENTRIES * 8;
    static constexpr uint32_t BANK_WORDS = 0x10000;

    uint16_t ram[SPRITE_WORDS] = {};
    uint16_t ram_buffer[SPRITE_WORDS] = {};

    std::vector<uint32_t> sprites;
    uint32_t numbanks = 0;
};

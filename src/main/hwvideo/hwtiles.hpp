/***************************************************************************
    Video Emulation: X-Board "System 16B"-style Tilemap Hardware.

    Reimplemented from segaic16_video_device's TILEMAP_16B mode
    (src/mame/sega/segaic16.cpp, Aaron Giles, MAME, BSD-3-Clause) —
    algorithm only, no code copied. See docs/HARDWARE.md section 3.3 and
    the phase-2 hardware notes for the exact register/bit layout this is
    derived from.

    Tile RAM (0x10000 bytes, main-map 0C0000-0CFFFF) holds 16 independent
    64x32-tile pages (512x256px each). Two logical layers — foreground and
    background — each present a scrolling 1024x512px *virtual* surface
    built by tiling up to 4 of those 16 pages in a 2x2 arrangement (so the
    game can scroll seamlessly across page boundaries); which 4 pages, and
    the scroll position within that virtual surface, come from text RAM
    registers. A separate, non-paged 64x28 text layer sits on top.

    Known simplification: only the *global* per-layer x/y scroll registers
    (E90-E9E) are implemented. X-Board also supports per-16px-column and
    per-8px-row fine scroll tables (with a per-row "use the alternate
    tilemap" override) for raster effects; those are not yet implemented
    (see docs/HARDWARE.md) since there's no ported game logic yet to
    exercise or verify them against.
***************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

class HWTiles
{
public:
    HWTiles();

    // Decode the raw 8x8x3-planar tile graphics ROM (`gfx1`, 3 equal
    // sequential bitplane thirds) into a fast per-pixel lookup cache.
    void init_gfx(const uint8_t* rom, uint32_t rom_length);

    // CPU-side tile RAM access (word offset within the 0x10000-byte window).
    uint16_t read_tile(uint32_t word_offset) const;
    void write_tile(uint32_t word_offset, uint16_t data);

    // CPU-side text RAM access (word offset within the 0x1000-byte window;
    // covers both the 64x28 text tilemap at the start and the scroll/page
    // registers from 0xE80 on).
    uint16_t read_text(uint32_t word_offset) const;
    void write_text(uint32_t word_offset, uint16_t data);

    // The FG/BG page-select and global scroll registers are latched once
    // per frame (real hardware: at scanline 261) rather than taking effect
    // immediately when written — call this at the same point in your frame
    // loop (e.g. once per vblank).
    void latch_frame_registers();

    // Draw one tile layer. `which`: 0 = foreground, 1 = background.
    // `category`: matches the tile entry's priority-vs-sprite bit (0 or 1)
    // — call twice per layer, once per category, each with its own
    // `priority_mark` to OR into the priority buffer on every opaque pixel
    // drawn (see docs/HARDWARE.md's compositing notes for the exact
    // per-layer priority_mark values to use).
    void draw_layer(uint16_t* pixels, uint8_t* priority, int width, int height,
                     int which, int category, uint8_t priority_mark) const;

    // Draw the (non-paged, non-scrolling-surface) text layer.
    void draw_text(uint16_t* pixels, uint8_t* priority, int width, int height,
                    int category, uint8_t priority_mark) const;

private:
    static constexpr int COLOR_BASE = 0x1c00;
    static constexpr int NUM_PAGES = 16;
    static constexpr int PAGE_COLS = 64;
    static constexpr int PAGE_ROWS = 32;
    static constexpr int PAGE_WORDS = PAGE_COLS * PAGE_ROWS;
    static constexpr int TEXT_COLS = 64;
    static constexpr int TEXT_ROWS = 28;
    static constexpr int TEXT_RAM_WORDS = 0x1000 / 2;

    uint16_t tile_ram[NUM_PAGES * PAGE_WORDS] = {};
    uint16_t text_ram[TEXT_RAM_WORDS] = {};

    // Latched per-frame registers, indices 0=FG, 1=BG, 2=ALT-FG, 3=ALT-BG.
    uint16_t latched_pageselect[4] = {};
    uint16_t latched_yscroll[4] = {};
    uint16_t latched_xscroll[4] = {};

    std::vector<uint8_t> tile_pixels; // 64 bytes (8x8, 1 byte/pixel) per tile
    uint32_t num_tiles = 0;

    inline uint8_t tile_pixel(uint32_t tile_index, int x, int y) const
    {
        if (num_tiles == 0) return 0;
        return tile_pixels[(tile_index % num_tiles) * 64 + static_cast<uint32_t>(y * 8 + x)];
    }

    // Draw a single 512x256px page at screen offset (ox, oy), filtering by
    // category and clipping to width/height. Used by draw_virtual() to
    // composite the 2x2 quadrant arrangement.
    void draw_page(uint16_t* pixels, uint8_t* priority, int width, int height,
                    int page, int ox, int oy, int category, uint8_t priority_mark) const;

    // The generic "virtual tilemap" compositor: given a page-select word and
    // scroll position, draws up to 4 quadrant pages tiled 2x2 so scrolling
    // wraps seamlessly across page boundaries.
    void draw_virtual(uint16_t* pixels, uint8_t* priority, int width, int height,
                       uint16_t pageselect, int xscroll, int yscroll,
                       int category, uint8_t priority_mark) const;
};

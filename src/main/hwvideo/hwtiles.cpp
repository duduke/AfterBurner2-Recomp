#include <cstring>
#include "hwvideo/hwtiles.hpp"

/*******************************************************************************************
 *  Sega "System 16B"-style tilemap hardware (segaic16_video_device, TILEMAP_16B mode).
 *  Reimplemented from src/mame/sega/segaic16.cpp (Aaron Giles, MAME, BSD-3-Clause) —
 *  algorithm only, no code copied. See docs/HARDWARE.md and this file's header comment.
 *
 *  Text RAM register word offsets (byte offset / 2), FG/BG/ALT-FG/ALT-BG order:
 *    page-select : 0x740 0x741 0x742 0x743   (byte 0xE80 0xE82 0xE84 0xE86)
 *    y-scroll    : 0x748 0x749 0x74A 0x74B   (byte 0xE90 0xE92 0xE94 0xE96)
 *    x-scroll    : 0x74C 0x74D 0x74E 0x74F   (byte 0xE98 0xE9A 0xE9C 0xE9E)
 *******************************************************************************************/

HWTiles::HWTiles() = default;

void HWTiles::init_gfx(const uint8_t* rom, uint32_t rom_length)
{
    const uint32_t third = rom_length / 3;
    num_tiles = third / 8; // 8 bytes (rows) per tile per bitplane
    tile_pixels.assign(static_cast<size_t>(num_tiles) * 64, 0);

    for (uint32_t tile = 0; tile < num_tiles; tile++)
    {
        for (int y = 0; y < 8; y++)
        {
            // Plane array order [last third, middle third, first third] maps to
            // decreasing bit significance [bit2 (MSB), bit1, bit0 (LSB)] — this
            // is MAME's gfx_element decode convention (drawgfx.cpp), verified
            // directly against src/emu/drawgfx.cpp rather than assumed.
            const uint8_t byte_msb = rom[2 * third + tile * 8 + y];
            const uint8_t byte_mid = rom[1 * third + tile * 8 + y];
            const uint8_t byte_lsb = rom[0 * third + tile * 8 + y];

            for (int x = 0; x < 8; x++)
            {
                const int shift = 7 - x; // MSB-first within each byte
                const uint8_t pixel = static_cast<uint8_t>((((byte_msb >> shift) & 1) << 2) |
                                                            (((byte_mid >> shift) & 1) << 1) |
                                                            (((byte_lsb >> shift) & 1) << 0));
                tile_pixels[static_cast<size_t>(tile) * 64 + static_cast<size_t>(y) * 8 + x] = pixel;
            }
        }
    }
}

uint16_t HWTiles::read_tile(uint32_t word_offset) const
{
    return tile_ram[word_offset % (NUM_PAGES * PAGE_WORDS)];
}

void HWTiles::write_tile(uint32_t word_offset, uint16_t data)
{
    tile_ram[word_offset % (NUM_PAGES * PAGE_WORDS)] = data;
}

uint16_t HWTiles::read_text(uint32_t word_offset) const
{
    return text_ram[word_offset % TEXT_RAM_WORDS];
}

void HWTiles::write_text(uint32_t word_offset, uint16_t data)
{
    text_ram[word_offset % TEXT_RAM_WORDS] = data;
}

void HWTiles::latch_frame_registers()
{
    for (int i = 0; i < 4; i++)
    {
        latched_pageselect[i] = text_ram[0x740 + i];
        latched_yscroll[i] = text_ram[0x748 + i];
        latched_xscroll[i] = text_ram[0x74C + i];
    }
}

// One tile layer, as tilemap_16b_draw_layer + draw_virtual_tilemap in
// segaic16.cpp. The layer is a 1024x512 virtual surface of four 512x256
// pages (one nibble of the page-select word per quadrant); each screen pixel
// samples it at (x + xscroll, y + yscroll), wrapping.
//
// Per 8-pixel row, the table at text RAM $F80 (FG) / $FC0 (BG) can override
// the X scroll (when the X scroll register's bit 15 is set) and, with its own
// bit 15, switch that row to the alternate register set (ALT-FG/ALT-BG:
// another page-select word and scroll pair). Per 16-pixel column (offset by
// 8), the table at $F16 / $F56 overrides the Y scroll when the Y scroll
// register's bit 15 is set. Both tables are read live. The X scroll is
// mirrored about the hardware's $C0 origin.
void HWTiles::draw_layer(uint16_t* pixels, uint8_t* priority, int width, int height,
                          int which, int category, uint8_t priority_mark, int clip_top, int clip_bottom) const
{
    const uint16_t xscroll = latched_xscroll[which];
    const uint16_t yscroll = latched_yscroll[which];
    const uint16_t pages = latched_pageselect[which];
    const bool column_scroll = (yscroll & 0x8000) != 0;
    const uint32_t row_table = 0xf80 / 2 + 0x20 * static_cast<uint32_t>(which);
    const uint32_t column_table = 0xf16 / 2 + 0x20 * static_cast<uint32_t>(which);

    for (int y = std::max(0, clip_top); y <= std::min(height-1, clip_bottom < 0 ? height-1 : clip_bottom); y++)
    {
        const uint16_t rowscroll = text_ram[row_table + static_cast<uint32_t>(y / 8)];
        const bool alternate = (rowscroll & 0x8000) != 0;
        uint16_t* prow = pixels + y * width;
        uint8_t* prirow = priority + y * width;

        for (int x = 0; x < width; x++)
        {
            uint16_t ex, ey, ep;
            if (alternate)
            {
                ex = latched_xscroll[which + 2];
                ey = latched_yscroll[which + 2];
                ep = latched_pageselect[which + 2];
            }
            else
            {
                ex = (xscroll & 0x8000) ? rowscroll : xscroll;
                ey = column_scroll ? text_ram[column_table + static_cast<uint32_t>((x + 8) / 16)] : yscroll;
                ep = pages;
            }
            ex = static_cast<uint16_t>((0xc0 - ex) & 0x3ff);
            ey = static_cast<uint16_t>(ey & 0x1ff);

            const uint32_t vx = (static_cast<uint32_t>(x) + ex) & 0x3ff;
            const uint32_t vy = (static_cast<uint32_t>(y) + ey) & 0x1ff;
            const uint32_t page = (ep >> ((((vy >> 8) * 2) + (vx >> 9)) * 4)) & 0xf;
            const uint16_t entry = tile_ram[page * PAGE_WORDS + ((vy & 255) >> 3) * PAGE_COLS + ((vx & 511) >> 3)];
            const int entry_category = (entry >> 15) & 1;
            if (category >= 0 && entry_category != category)
                continue;

            const uint8_t pix = tile_pixel(entry & 0x1fff, static_cast<int>(vx & 7), static_cast<int>(vy & 7));
            if (pix == 0)
                continue; // transparent
            prow[x] = static_cast<uint16_t>(COLOR_BASE + ((entry >> 6) & 0x7f) * 8 + pix);
            prirow[x] |= category < 0 ? priority_mark << entry_category : priority_mark;
        }
    }
}

void HWTiles::draw_text(uint16_t* pixels, uint8_t* priority, int width, int height,
                         int category, uint8_t priority_mark, int clip_top, int clip_bottom) const
{
    for (int ty = std::max(0, clip_top) / 8; ty < TEXT_ROWS; ty++)
    {
        const int py0 = ty * 8;
        if (py0 >= height || (clip_bottom >= 0 && py0 > clip_bottom))
            break;

        for (int tx = 0; tx < TEXT_COLS; tx++)
        {
            const int px0 = tx * 8 - 192;
            if (px0 >= width)
                break;

            const uint16_t entry = text_ram[ty * TEXT_COLS + tx];
            const int entry_category = (entry >> 15) & 1;
            if (category >= 0 && entry_category != category)
                continue;

            const int color = (entry >> 9) & 0x07;
            const uint32_t code = entry & 0x1ff; // always bank 0 (identity remap on X-Board)

            for (int y = 0; y < 8; y++)
            {
                const int py = py0 + y;
                if (py >= height || py < clip_top || (clip_bottom >= 0 && py > clip_bottom))
                    continue;
                uint16_t* prow = pixels + py * width;
                uint8_t* prirow = priority + py * width;

                for (int x = 0; x < 8; x++)
                {
                    const int px = px0 + x;
                    if (px < 0 || px >= width)
                        continue;
                    const uint8_t pix = tile_pixel(code, x, y);
                    if (pix == 0)
                        continue;
                    prow[px] = static_cast<uint16_t>(COLOR_BASE + color * 8 + pix);
                    prirow[px] |= category < 0 ? priority_mark << entry_category : priority_mark;
                }
            }
        }
    }
}

HWTiles::FrameRegisters HWTiles::frame_registers() const
{
    FrameRegisters values{};
    for (unsigned i=0;i<4;++i) {
        values[i]=latched_pageselect[i]; values[4+i]=latched_yscroll[i]; values[8+i]=latched_xscroll[i];
    }
    return values;
}
void HWTiles::load_initial_frame_registers(const FrameRegisters& values)
{
    for (unsigned i=0;i<4;++i) {
        latched_pageselect[i]=values[i]; latched_yscroll[i]=values[4+i]; latched_xscroll[i]=values[8+i];
    }
}

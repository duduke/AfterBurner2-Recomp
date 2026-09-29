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

void HWTiles::draw_page(uint16_t* pixels, uint8_t* priority, int width, int height,
                         int page, int ox, int oy, int category, uint8_t priority_mark) const
{
    // Cheap reject: this page's 512x256px block doesn't touch the screen at all.
    if (ox + 512 <= 0 || ox >= width || oy + 256 <= 0 || oy >= height)
        return;

    const uint16_t* page_ram = tile_ram + static_cast<uint32_t>(page) * PAGE_WORDS;

    for (int ty = 0; ty < PAGE_ROWS; ty++)
    {
        const int py0 = oy + ty * 8;
        if (py0 + 8 <= 0 || py0 >= height)
            continue;

        for (int tx = 0; tx < PAGE_COLS; tx++)
        {
            const int px0 = ox + tx * 8;
            if (px0 + 8 <= 0 || px0 >= width)
                continue;

            const uint16_t entry = page_ram[ty * PAGE_COLS + tx];
            if (static_cast<int>((entry >> 15) & 1) != category)
                continue;

            const int color = (entry >> 6) & 0x7f;
            const uint32_t code = entry & 0x1fff;

            for (int y = 0; y < 8; y++)
            {
                const int py = py0 + y;
                if (py < 0 || py >= height)
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
                        continue; // transparent
                    prow[px] = static_cast<uint16_t>(COLOR_BASE + color * 8 + pix);
                    prirow[px] |= priority_mark;
                }
            }
        }
    }
}

void HWTiles::draw_virtual(uint16_t* pixels, uint8_t* priority, int width, int height,
                            uint16_t pageselect, int xscroll, int yscroll,
                            int category, uint8_t priority_mark) const
{
    // The virtual surface is 1024x512px: 2x2 quadrants of one 512x256px page
    // each, so scrolling wraps seamlessly across page boundaries. xscroll is
    // 10 bits (0-1023), yscroll 9 bits (0-511).
    const int leftcol = (xscroll >> 9) & 1;
    const int topcol = (yscroll >> 8) & 1;
    const int sx0 = -(xscroll & 511);
    const int sx1 = sx0 + 512;
    const int sy0 = -(yscroll & 255);
    const int sy1 = sy0 + 256;

    for (int qcy = 0; qcy < 2; qcy++)
    {
        const int oy = (qcy == topcol) ? sy0 : sy1;
        for (int qcx = 0; qcx < 2; qcx++)
        {
            const int nibble = qcy * 2 + qcx;
            const int page = (pageselect >> (nibble * 4)) & 0xf;
            const int ox = (qcx == leftcol) ? sx0 : sx1;
            draw_page(pixels, priority, width, height, page, ox, oy, category, priority_mark);
        }
    }
}

void HWTiles::draw_layer(uint16_t* pixels, uint8_t* priority, int width, int height,
                          int which, int category, uint8_t priority_mark) const
{
    // NOTE: uses only the flat global x/y scroll (ignores the column-scroll and
    // row-scroll fine tables and the ALT-tilemap-per-row override — see the
    // "known simplification" note in hwtiles.hpp).
    const uint16_t pageselect = latched_pageselect[which];
    const int yscroll = latched_yscroll[which] & 0x1ff;
    const int xscroll = latched_xscroll[which] & 0x3ff;
    draw_virtual(pixels, priority, width, height, pageselect, xscroll, yscroll, category, priority_mark);
}

void HWTiles::draw_text(uint16_t* pixels, uint8_t* priority, int width, int height,
                         int category, uint8_t priority_mark) const
{
    for (int ty = 0; ty < TEXT_ROWS; ty++)
    {
        const int py0 = ty * 8;
        if (py0 >= height)
            break;

        for (int tx = 0; tx < TEXT_COLS; tx++)
        {
            const int px0 = tx * 8;
            if (px0 >= width)
                break;

            const uint16_t entry = text_ram[ty * TEXT_COLS + tx];
            if (static_cast<int>((entry >> 15) & 1) != category)
                continue;

            const int color = (entry >> 9) & 0x07;
            const uint32_t code = entry & 0x1ff; // always bank 0 (identity remap on X-Board)

            for (int y = 0; y < 8; y++)
            {
                const int py = py0 + y;
                if (py >= height)
                    continue;
                uint16_t* prow = pixels + py * width;
                uint8_t* prirow = priority + py * width;

                for (int x = 0; x < 8; x++)
                {
                    const int px = px0 + x;
                    if (px >= width)
                        continue;
                    const uint8_t pix = tile_pixel(code, x, y);
                    if (pix == 0)
                        continue;
                    prow[px] = static_cast<uint16_t>(COLOR_BASE + color * 8 + pix);
                    prirow[px] |= priority_mark;
                }
            }
        }
    }
}

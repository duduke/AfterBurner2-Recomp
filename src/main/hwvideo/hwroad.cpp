#include <algorithm>
#include <cstring>
#include "hwvideo/hwroad.hpp"

/*******************************************************************************************
 *
 *  Sega X-Board "road" chip (segaic16_road_device, ROAD_XBOARD in MAME — shares its
 *  decode/draw algorithm with OutRun's ROAD_OUTRUN mode).
 *
 *  Road control register:
 *      Bits               Usage
 *      -------- -----d--  Direct scanline mode (1) or indirect mode (0)
 *      -------- ------pp  Road enable/priorities:
 *                            0 = road 0 only visible
 *                            1 = both roads visible, road 0 has priority
 *                            2 = both roads visible, road 1 has priority
 *                            3 = road 1 only visible
 *
 *  Road RAM (word offsets):
 *      000-0FF  ----s--- --------  Road 0: solid fill (1) or ROM fill
 *               -------- -ccccccc  Road 0: solid color (if solid fill)
 *               -------i iiiiiiii  Road 0: index for other tables (indirect mode)
 *               -------r rrrrrrr-  Road 0: road ROM line select
 *      100-1FF  (as above, for road 1)
 *      200-3FF  ----hhhh hhhhhhhh  Road 0: horizontal scroll, indexed as above
 *      400-5FF  (as above, for road 1)
 *      600-7FF  ----bbbb --------  Background color index
 *               -------- s-------  Road 1: stripe color index
 *               -------- -a------  Road 1: pixel value 2 color index
 *               -------- --b-----  Road 1: pixel value 1 color index
 *               -------- ---c----  Road 1: pixel value 0 color index
 *               -------- ----s---  Road 0: stripe color index
 *               -------- -----a--  Road 0: pixel value 2 color index
 *               -------- ------b-  Road 0: pixel value 1 color index
 *               -------- -------c  Road 0: pixel value 0 color index
 *
 *  Logic:
 *      The scanline indexes into 000-0FF/100-1FF. If solid fill, the background is
 *      filled with the given color index. Otherwise: in indirect mode, the low 9
 *      bits of that table value give an index into the remaining tables; in direct
 *      scanline mode, the index is just the scanline (road 0) or scanline+256 (road 1).
 *      That index looks up horizontal scroll (200-3FF/400-5FF) and color info (600-7FF,
 *      shared by both roads).
 *
 *  Reimplemented from src/mame/sega/segaic16_road.cpp (Aaron Giles, MAME, BSD-3-Clause) —
 *  algorithm only, no code copied; see docs/HARDWARE.md section 3.2.
 *******************************************************************************************/

HWRoad::HWRoad(uint16_t color_base1, uint16_t color_base2, uint16_t color_base3, int32_t x_off)
    : color_offset1(color_base1), color_offset2(color_base2), color_offset3(color_base3), x_offset(x_off)
{
    std::memset(ram, 0, sizeof(ram));
    std::memset(ram_buffer, 0, sizeof(ram_buffer));
}

// Decode two 512x256, 2-bits-per-pixel road maps (bit 0 plane at rom[0x0000-0x3FFF],
// bit 1 plane at rom[0x4000-0x7FFF], repeating every `rom_length / 2` bytes for however
// many distinct road patterns the ROM holds — one road pattern per 0x8000 source bytes).
//
// Decoded value: 0 = road surface, 1 = inner stripe, 2 = outer stripe, 3 = exterior.
// The last 8 pixels of the central-stripe area are pre-marked with bit 2 set (value 7)
// when they'd otherwise read as "exterior", matching the real chip's stripe-priority PAL.
void HWRoad::decode_road(const uint8_t* rom, uint32_t rom_length)
{
    for (int y = 0; y < 512; y++)
    {
        const uint32_t src = ((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % rom_length;
        uint8_t* dst = roads + y * 512;

        for (int x = 0; x < 512; x++)
        {
            dst[x] = static_cast<uint8_t>(
                (((rom[src + (x / 8)] >> (~x & 7)) & 1) << 0) |
                (((rom[src + (x / 8 + 0x4000)] >> (~x & 7)) & 1) << 1));

            if (x >= 256 - 8 && x < 256 && dst[x] == 3)
                dst[x] |= 4;
        }
    }

    // Dummy "all exterior" scanline, used when a road entry is solid-fill.
    std::memset(roads + 512 * 512, 3, 512);
}

void HWRoad::init(const uint8_t* rom, uint32_t rom_length)
{
    road_control = 0;
    decode_road(rom, rom_length);
}

void HWRoad::read_road_control()
{
    // Swap the live/buffered halves of road RAM: the CPU builds next frame's
    // table in `ram` while the renderer keeps reading last frame's from
    // `ram_buffer` until this swap happens (real hardware ties this to a
    // CPU read of the control register, which is how the game synchronizes
    // road updates to vblank).
    std::swap_ranges(ram, ram + ROAD_RAM_WORDS, ram_buffer);
}

void HWRoad::write_road_control(uint8_t control)
{
    road_control = control;
}

void HWRoad::render_background(uint16_t* pixels, int width, int height, int clip_top, int clip_bottom) const
{
    const uint16_t* roadram = ram_buffer;

    for (int y = std::max(0, clip_top); y <= std::min(height-1, clip_bottom < 0 ? height-1 : clip_bottom); y++)
    {
        const int data0 = roadram[0x000 + y];
        const int data1 = roadram[0x100 + y];
        int color = -1;

        switch (road_control & 3)
        {
            case 0:
                if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 1:
                if (data0 & 0x800) color = data0 & 0x7f;
                else if (data1 & 0x800) color = data1 & 0x7f;
                break;
            case 2:
                if (data1 & 0x800) color = data1 & 0x7f;
                else if (data0 & 0x800) color = data0 & 0x7f;
                break;
            case 3:
                if (data1 & 0x800) color = data1 & 0x7f;
                break;
        }

        if (color != -1)
        {
            const uint16_t c = static_cast<uint16_t>(color | color_offset3);
            uint16_t* row = pixels + y * width;
            for (int x = 0; x < width; x++)
                row[x] = c;
        }
    }
}

void HWRoad::render_foreground(uint16_t* pixels, int width, int height, int clip_top, int clip_bottom, int native_left) const
{
    const uint16_t* roadram = ram_buffer;

    for (int y = std::max(0, clip_top); y <= std::min(height-1, clip_bottom < 0 ? height-1 : clip_bottom); y++)
    {
        const uint32_t data0 = roadram[0x000 + y];
        const uint32_t data1 = roadram[0x100 + y];

        // if both roads are low priority (solid-fill), the background pass already
        // handled this scanline
        if ((data0 & 0x800) && (data1 & 0x800))
            continue;

        const uint32_t control = road_control & 3;
        if ((control == 0 && (data0 & 0x800)) || (control == 3 && (data1 & 0x800)))
            continue;

        const bool direct = (road_control & 4) != 0;

        const uint8_t* src0 = (data0 & 0x800) ? (roads + 512 * 512)
                                               : (roads + (0x000 + ((data0 >> 1) & 0xff)) * 512);
        const uint8_t* src1 = (data1 & 0x800) ? (roads + 512 * 512)
                                               : (roads + (0x100 + ((data1 >> 1) & 0xff)) * 512);

        const uint32_t idx0 = direct ? static_cast<uint32_t>(y) : (data0 & 0x1ff);
        const uint32_t idx1 = direct ? static_cast<uint32_t>(0x100 + y) : (data1 & 0x1ff);

        uint32_t hpos0 = roadram[0x200 + idx0] & 0xfff;
        uint32_t hpos1 = roadram[0x400 + idx1] & 0xfff;
        const uint32_t color0 = roadram[0x600 + idx0];
        const uint32_t color1 = roadram[0x600 + idx1];

        uint16_t color_table[24];
        uint32_t bgcolor = (color0 >> 8) & 0xf;
        color_table[0x00] = static_cast<uint16_t>(color_offset1 ^ 0x00 ^ ((color0 >> 0) & 1));
        color_table[0x01] = static_cast<uint16_t>(color_offset1 ^ 0x02 ^ ((color0 >> 1) & 1));
        color_table[0x02] = static_cast<uint16_t>(color_offset1 ^ 0x04 ^ ((color0 >> 2) & 1));
        color_table[0x03] = (data0 & 0x200) ? color_table[0x00]
                                             : static_cast<uint16_t>(color_offset2 ^ 0x00 ^ bgcolor);
        color_table[0x07] = static_cast<uint16_t>(color_offset1 ^ 0x06 ^ ((color0 >> 3) & 1));

        bgcolor = (color1 >> 8) & 0xf;
        color_table[0x10] = static_cast<uint16_t>(color_offset1 ^ 0x08 ^ ((color1 >> 4) & 1));
        color_table[0x11] = static_cast<uint16_t>(color_offset1 ^ 0x0a ^ ((color1 >> 5) & 1));
        color_table[0x12] = static_cast<uint16_t>(color_offset1 ^ 0x0c ^ ((color1 >> 6) & 1));
        color_table[0x13] = (data1 & 0x200) ? color_table[0x10]
                                             : static_cast<uint16_t>(color_offset2 ^ 0x10 ^ bgcolor);
        color_table[0x17] = static_cast<uint16_t>(color_offset1 ^ 0x0e ^ ((color1 >> 7) & 1));

        // Priority between the two roads where they overlap (values 0-3, see the
        // road-priority PAL note in docs/HARDWARE.md): road 1 wins whenever road 0's
        // pixel is background/exterior (3) or inner-stripe-under-road-1 (1/2 with
        // road 1 opaque), road 0 wins otherwise.
        static const uint8_t priority_road1_wins[8][8] =
        {
            { 0, 0, 0, 0, 0, 0, 0, 1 },
            { 1, 0, 0, 0, 0, 0, 0, 1 },
            { 1, 0, 0, 0, 0, 0, 0, 1 },
            { 1, 1, 1, 0, 0, 0, 0, 1 },
            { 0, 0, 0, 0, 0, 0, 0, 0 },
            { 0, 0, 0, 0, 0, 0, 0, 0 },
            { 0, 0, 0, 0, 0, 0, 0, 0 },
            { 0, 0, 0, 0, 0, 0, 0, 0 },
        };
        static const uint8_t priority_map_ctrl2[8] = { 0x81, 0x81, 0x81, 0x8f, 0, 0, 0, 0x80 };

        uint16_t* row = pixels + y * width;
        // $5F8 is the chip's fixed horizontal origin, on top of the per-game offset.
        int32_t h0 = ((static_cast<int32_t>(hpos0) - (0x5f8 + x_offset)) & 0xfff) + native_left;
        int32_t h1 = ((static_cast<int32_t>(hpos1) - (0x5f8 + x_offset)) & 0xfff) + native_left;

        for (int x = 0; x < width; x++)
        {
            unsigned p0 = (h0 >= 0 && h0 < 0x200) ? src0[h0] : 3u;
            unsigned p1 = (h1 >= 0 && h1 < 0x200) ? src1[h1] : 3u;
            uint16_t pixel;

            switch (control)
            {
                case 0:
                    pixel = color_table[p0];
                    break;
                case 3:
                    pixel = color_table[0x10 + p1];
                    break;
                case 1:
                    pixel = priority_road1_wins[p0][p1] ? color_table[0x10 + p1] : color_table[p0];
                    break;
                default: // case 2
                    pixel = ((priority_map_ctrl2[p0] >> p1) & 1) ? color_table[0x10 + p1] : color_table[p0];
                    break;
            }

            row[x] = pixel;
            h0 = (h0 + 1) & 0xfff;
            h1 = (h1 + 1) & 0xfff;
        }
    }
}

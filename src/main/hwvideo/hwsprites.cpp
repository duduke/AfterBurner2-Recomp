#include <algorithm>
#include <cstring>
#include "hwvideo/hwsprites.hpp"

/*******************************************************************************************
 *  Out Run/X-Board-style sprites — see docs/HARDWARE.md section 3.1 for the full per-entry
 *  bit layout. Reimplemented from sega_outrun_sprite_device::draw(), X-Board branch
 *  (src/mame/sega/sega16sp.cpp, Aaron Giles, MAME, BSD-3-Clause) — algorithm only, no code
 *  copied.
 *******************************************************************************************/

HWSprites::HWSprites() = default;

void HWSprites::init(const uint8_t* rom, uint32_t rom_length)
{
    refresh_rom(rom, rom_length);

    std::memset(ram, 0, sizeof(ram));
    std::memset(ram_buffer, 0, sizeof(ram_buffer));
}

void HWSprites::refresh_rom(const uint8_t* rom, uint32_t rom_length)
{
    sprites.assign(rom_length / 4, 0);
    for (uint32_t i = 0; i < sprites.size(); i++)
    {
        // Reconstruct the little-endian 32-bit word from 4 interleaved bytes
        // (ROM_REGION32_LE + ROM_LOAD32_BYTE, per docs/HARDWARE.md section 6):
        // the file loaded at interleave position 0 supplies the LSB.
        const uint8_t b0 = rom[i * 4 + 0];
        const uint8_t b1 = rom[i * 4 + 1];
        const uint8_t b2 = rom[i * 4 + 2];
        const uint8_t b3 = rom[i * 4 + 3];
        sprites[i] = (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16) |
                     (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b0);
    }
    numbanks = static_cast<uint32_t>(sprites.size()) / BANK_WORDS;
}

void HWSprites::write16(uint32_t byte_addr, uint16_t data)
{
    ram[(byte_addr >> 1) & (SPRITE_WORDS - 1)] = data;
}

// The two halves are exchanged, as on hardware (MAME draw_write): the CPU
// then sees the list that was being displayed.
void HWSprites::swap()
{
    std::swap_ranges(ram, ram + SPRITE_WORDS, ram_buffer);
}

void HWSprites::render(uint16_t* pixels, int width, int height, int origin_x, int origin_y, unsigned priority_mask,int clip_top,int clip_bottom) const
{
    if(clip_bottom<0)clip_bottom=height-1;
    for (uint16_t* data = ram_buffer; data < ram_buffer + SPRITE_WORDS; data += 8)
    {
        // stop when we hit the end of sprite list
        if (data[0] & 0x8000)
            break;

        const int hide = data[0] & 0x5000;
        int bank = (data[0] >> 9) & 7;
        const int top = (data[0] & 0x1ff) - 0x100 + origin_y;
        uint16_t addr = data[1]; // 16-bit register: wraps within the current bank, matching hardware
        const int pitch = static_cast<int16_t>((data[2] >> 1) | ((data[4] & 0x1000) << 3)) >> 8;
        int xpos = data[2] & 0x1ff;
        int vzoom = data[3] & 0x7ff;
        const int ydelta = (data[4] & 0x8000) ? 1 : -1;
        const int flip = (~data[4] >> 14) & 1;
        const int xdelta = (data[4] & 0x2000) ? 1 : -1;
        int hzoom = data[4] & 0x7ff;
        // X-Board packing: full 12-bit height at +A, 8-bit color at +C (word
        // indices 5 and 6) — differs from OutRun's packing, which shares
        // word 5 between an 8-bit height and a 7-bit color.
        const int height_rows = (data[5] & 0xfff) + 1;
        const int colpri = ((data[6] & 0xff) << 4) | (((data[3] >> 12) & 7) << 12);

        if (!(priority_mask & (1u << ((data[3] >> 12) & 3))))
            continue;
        // sega_outrun_sprite_device::draw initializes this register even
        // for hidden entries, then updates it for each visible row.
        data[7]=addr;
        if (hide)
            continue;

        // adjust X coordinate (guards against wraparound; matches MAME)
        if (xpos < 0x80 && xdelta < 0)
            xpos += 0x200;
        xpos += origin_x - 0xBE;

        if (numbanks)
            bank = static_cast<int>(bank % numbanks);
        const uint32_t* spritedata = sprites.data() + BANK_WORDS * static_cast<uint32_t>(bank);

        // clamp to a maximum of 8x zoom in either direction (matches MAME; not confirmed
        // against real hardware but harmless since games shouldn't hit this range anyway)
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;

        const int yend = top + ydelta * (height_rows - 1);
        const int ymin = ydelta > 0 ? top : yend;
        const int ymax = ydelta > 0 ? yend : top;
        if (ymax < clip_top || ymin > clip_bottom)
            continue;

        const int row_begin = ydelta > 0 ? std::max(0, clip_top - top) : std::max(0, top - clip_bottom);
        const int row_end = ydelta > 0 ? std::min(height_rows - 1, clip_bottom - top) : std::min(height_rows - 1, top - clip_top);

        for (int row = row_begin; row <= row_end; ++row)
        {
            const int y = top + ydelta * row;
            uint16_t* dest = pixels + y * width;
            int xacc = 0;
            int x;

            // walking read pointer for this row; starts at the row's base address
            uint16_t read_addr = static_cast<uint16_t>(addr + pitch * ((row * vzoom) >> 9));

            for (x = xpos; (xdelta > 0 && x < width) || (xdelta < 0 && x >= 0);)
            {
                uint32_t word = spritedata[read_addr];
                if (flip)
                {
                    read_addr--;
                }
                else
                {
                    read_addr++;
                    if (word)
                    {
                        // reverse nibble order: forward reads walk ROM address ascending but
                        // pixel-within-word from MSB to LSB (see docs/HARDWARE.md section 3.1)
                        word = ((word << 28) & 0xf0000000u) | ((word << 20) & 0x0f000000u) |
                               ((word << 12) & 0x00f00000u) | ((word << 4) & 0x000f0000u) |
                               ((word >> 4) & 0x0000f000u) | ((word >> 12) & 0x00000f00u) |
                               ((word >> 20) & 0x000000f0u) | ((word >> 28) & 0x0000000fu);
                    }
                }
                if (!word)
                {
                    while (xacc < 0x1000)
                    {
                        x += xdelta;
                        xacc += hzoom;
                    }
                    xacc -= 0x1000;
                    continue;
                }
                const bool last_word = (word & 0x0f000000u) == 0x0f000000u;

                for (int k = 0; k < 8; k++)
                {
                    const int pix = static_cast<int>(word & 0xf);
                    if (pix != 0 && pix != 15)
                    {
                        const uint16_t raw_pixel = static_cast<uint16_t>(colpri | pix);
                        while (xacc < 0x200)
                        {
                            if (static_cast<unsigned>(x) < static_cast<unsigned>(width))
                                dest[x] = raw_pixel;
                            x += xdelta;
                            xacc += hzoom;
                        }
                    }
                    else
                    {
                        while (xacc < 0x200)
                        {
                            x += xdelta;
                            xacc += hzoom;
                        }
                    }
                    xacc -= 0x200;
                    word >>= 4;
                }

                if (last_word)
                    break;
            }
            data[7]=read_addr;
        }
    }
}

void HWSprites::render_bank(uint8_t* bank,uint16_t* pixels,int width,int height,int clip_top,int clip_bottom) const
{
    for(unsigned i=0;i<SPRITE_WORDS;++i)ram_buffer[i]=(uint16_t(bank[2*i])<<8)|bank[2*i+1];
    render(pixels,width,height,0,0,0xf,clip_top,clip_bottom);
    for(unsigned i=0;i<SPRITE_WORDS;++i){bank[2*i]=ram_buffer[i]>>8;bank[2*i+1]=ram_buffer[i];}
}

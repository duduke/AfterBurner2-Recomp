#include "engine/osprites.hpp"
#include <algorithm>

OSprites::OSprites()
{
    sprite_queue.reserve(MAX_SPRITES);
}

void OSprites::begin_frame()
{
    sprite_queue.clear();
}

void OSprites::append_sprite(const OEntry& obj, uint16_t rom_bank, uint16_t rom_offset,
                            uint16_t pitch, uint16_t zoom_x, uint16_t zoom_y,
                            uint16_t height, uint8_t palette, uint8_t priority)
{
    if (sprite_queue.size() >= MAX_SPRITES || height == 0)
        return;

    SpriteDesc desc;
    desc.z = obj.z;

    // Format into X-Board 8-word descriptor format (docs/HARDWARE.md section 3.1):
    // Word 0: [15] End-of-list, [14/12] Hide, [11-9] Bank, [8-0] Top scanline + 256
    uint16_t top_scanline = static_cast<uint16_t>((obj.y + 256) & 0x1FF);
    desc.words[0] = static_cast<uint16_t>(((rom_bank & 7) << 9) | top_scanline);

    // Word 1: Offset within selected ROM bank (when flipped, start at row end)
    bool is_flipped = (obj.control & 0x2000) != 0;
    uint16_t start_offset = rom_offset;
    if (is_flipped && pitch > 0)
        start_offset += (pitch - 1);
    desc.words[1] = start_offset;

    // Word 2: [15-9] Pitch, [8-0] X position (hardware origin is 0xBE = 190)
    uint16_t x_pos = static_cast<uint16_t>((obj.x + 0xBE) & 0x1FF);
    desc.words[2] = static_cast<uint16_t>(((pitch & 0x7F) << 9) | x_pos);

    // Word 3: [14] Shadow, [13-12] Priority, [10-0] Vertical Zoom
    desc.words[3] = static_cast<uint16_t>(((priority & 3) << 12) | (zoom_y & 0x7FF));

    // Word 4: [15] Top-to-bottom (1 = down), [14] H-flip (~bit 14: 1 = normal, 0 = flipped), [13] Left-to-right (1 = right), [10-0] Horizontal Zoom
    uint16_t flip_bit = is_flipped ? 0x0000 : 0x4000;
    desc.words[4] = static_cast<uint16_t>(0xA000 | flip_bit | (zoom_x & 0x7FF));

    // Word 5 stores destination scanlines, while callers supply source rows.
    // Match the scaler's minimum zoom so downscaling never reads past the frame.
    const uint32_t effective_zoom = std::max<uint32_t>(0x40, zoom_y & 0x7FF);
    const uint32_t screen_height = std::max<uint32_t>(1, (height * 0x200u + effective_zoom - 1) / effective_zoom);
    desc.words[5] = static_cast<uint16_t>(std::min<uint32_t>(0x1000, screen_height) - 1);

    // Word 6: Palette index
    desc.words[6] = palette;

    // Word 7: Scratch / read address
    desc.words[7] = 0;

    sprite_queue.push_back(desc);
}

void OSprites::append_raw(int16_t z, const uint16_t words[8])
{
    if (sprite_queue.size() >= MAX_SPRITES)
        return;

    SpriteDesc desc;
    desc.z = z;
    for (int i = 0; i < 8; ++i)
        desc.words[i] = words[i];

    sprite_queue.push_back(desc);
}

void OSprites::flush_to_hardware(HWSprites& hw_sprites)
{
    // Depth-sort far-to-near (arcade counting sort behavior)
    std::stable_sort(sprite_queue.begin(), sprite_queue.end(),
                     [](const SpriteDesc& a, const SpriteDesc& b) {
                         return a.z > b.z;
                     });

    // Blit into Hardware Sprite RAM (up to 256 entries)
    uint32_t byte_addr = 0;
    for (size_t i = 0; i < sprite_queue.size() && i < 256; ++i)
    {
        for (int w = 0; w < 8; ++w)
        {
            hw_sprites.write16(byte_addr, sprite_queue[i].words[w]);
            byte_addr += 2;
        }
    }

    // Set end-of-list marker on the next entry if buffer has room
    if (sprite_queue.size() < 256)
    {
        hw_sprites.write16(byte_addr, 0x8000);
    }

    // Swap buffers and trigger hardware draw
    hw_sprites.swap();
}

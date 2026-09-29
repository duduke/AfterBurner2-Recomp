#include "engine/oroad.hpp"
#include <algorithm>
#include <cmath>

ORoad::ORoad()
{
}

void ORoad::set_stage(uint8_t stage)
{
    current_stage = stage;
}

void ORoad::set_horizon(int16_t horizon_y)
{
    horizon = std::clamp<int16_t>(horizon_y, 0, 223);
}

void ORoad::set_roll(int16_t roll_angle)
{
    roll = roll_angle;
}

void ORoad::set_curve(int16_t curve_val)
{
    curve = curve_val;
}

void ORoad::update(HWRoad& hw_road)
{
    // 224 scanlines for After Burner II
    // Road control RAM layout (docs/HARDWARE.md section 3.2):
    // Word 0x000 + y: Control/position 1
    // Word 0x100 + y: Control/position 2
    // Word 0x200 + y: Horizontal position 1
    // Word 0x400 + y: Horizontal position 2 (0x800 byte offset)
    // Word 0x600 + y: Color select table (0xC00 byte offset)

    for (int y = 0; y < 224; ++y)
    {
        uint32_t addr_ctrl1 = static_cast<uint32_t>((0x000 + y) * 2);
        uint32_t addr_ctrl2 = static_cast<uint32_t>((0x100 + y) * 2);
        uint32_t addr_hpos1 = static_cast<uint32_t>((0x200 + y) * 2);
        uint32_t addr_hpos2 = static_cast<uint32_t>((0x400 + y) * 2);
        uint32_t addr_color = static_cast<uint32_t>((0x600 + y) * 2);

        if (y < horizon)
        {
            // Sky portion: above horizon
            // Control word: bit 11 (0x800) enables solid background fill with color in low 7 bits
            uint16_t sky_col = static_cast<uint16_t>((y * 31 / std::max(1, (int)horizon)) & 0x1F);
            uint16_t solid_sky = static_cast<uint16_t>(0x0800 | sky_col);
            hw_road.write16(addr_ctrl1, solid_sky);
            hw_road.write16(addr_ctrl2, solid_sky);

            // Straight horizontal offset
            hw_road.write16(addr_hpos1, 0x0100);
            hw_road.write16(addr_hpos2, 0x0100);

            // Palette bank for sky
            hw_road.write16(addr_color, 0x0000);
        }
        else
        {
            // Ground / sea portion: below horizon
            int depth = y - horizon + 1;

            // Perspective perspective pitch factor (matching arcade ASL/ASR scaling)
            uint16_t road_index = static_cast<uint16_t>((depth * 2) & 0x1FF);
            hw_road.write16(addr_ctrl1, road_index);
            hw_road.write16(addr_ctrl2, road_index);

            // Roll / banking displacement
            int32_t roll_offset = (roll * depth) / 64;
            int32_t curve_offset = (curve * depth * depth) / 2048;
            int16_t hpos = static_cast<int16_t>(0x0100 + roll_offset + curve_offset);

            hw_road.write16(addr_hpos1, static_cast<uint16_t>(hpos));
            hw_road.write16(addr_hpos2, static_cast<uint16_t>(hpos));

            // Alternate road strip coloring based on distance
            uint16_t color_bank = (depth & 8) ? 0x0001 : 0x0000;
            hw_road.write16(addr_color, color_bank);
        }
    }

    // Flip double-buffer and latch priority
    hw_road.read_road_control();
    hw_road.write_road_control(priority_control);
}

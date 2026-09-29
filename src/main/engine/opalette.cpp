#include "engine/opalette.hpp"

namespace
{
    // Palette cycling color tables (derived from arcade ROM tables at $1413A, $14196, $141F2)
    // 4 phases for the jet exhaust flame flickering effect
    constexpr uint16_t FLAME_COLORS[4][7] = {
        { 0x00FF, 0x00DF, 0x00BF, 0x009F, 0x007F, 0x005F, 0x003F },
        { 0x01FF, 0x01EF, 0x01CF, 0x01AF, 0x018F, 0x016F, 0x014F },
        { 0x03FF, 0x03EF, 0x03DF, 0x03CF, 0x03BF, 0x03AF, 0x039F },
        { 0x02FF, 0x02DF, 0x02BF, 0x029F, 0x027F, 0x025F, 0x023F },
    };
}

OPalette::OPalette()
{
}

void OPalette::trigger_flash(uint8_t duration)
{
    fade_duration = duration;
    fade_active = true;
    fade_channels = 0x07;
}

void OPalette::update(HWPalette& hw_palette)
{
    frame_counter++;

    // 1. Color cycling player ($013F92):
    // Advances every 4 frames ((frame >> 2) & 3)
    if (!fade_active)
    {
        uint32_t phase = (frame_counter >> 2) & 3;
        for (int i = 0; i < 7; ++i)
        {
            // Stream into fixed palette slots for afterburner flame and HUD lights
            hw_palette.write(0x1D22 / 2 + i, FLAME_COLORS[phase][i]);
            hw_palette.write(0x1EA2 / 2 + i, FLAME_COLORS[(phase + 1) & 3][i]);
            hw_palette.write(0x1E42 / 2 + i, FLAME_COLORS[(phase + 2) & 3][i]);
        }
    }

    // 2. Palette fade effect ($014A86):
    if (fade_active)
    {
        if (fade_duration > 0)
        {
            fade_duration--;
        }
        else
        {
            fade_active = false;
        }
    }
}

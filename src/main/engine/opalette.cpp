#include "engine/opalette.hpp"

OPalette::OPalette() = default;
void OPalette::trigger_flash(uint8_t duration)
{
    fade_duration = duration;
    fade_active = true;
}
void OPalette::update(HWPalette& palette)
{
    ++frame_counter;
    // $013FF2 streams fourteen colors to player palette F2 from the ROM table
    // selected by the pointer list at $141F2. Keep airframe colors intact.
    const uint32_t source = 0x2bf5e + ((frame_counter / 4) & 7) * 32;
    if (program && source + 32 <= program_length)
        for (int i = 1; i < 15; ++i)
            palette.write(0xf20 + i, (program[source + i * 2] << 8) | program[source + i * 2 + 1]);
    const unsigned phase = (frame_counter / 4) & 3;
    palette.write(0xf21, 0x006f + phase * 0x10);
    palette.write(0xf22, 0x009f + phase * 0x10);
    palette.write(0xf24, 0x07cf + phase * 0x110);
    if (fade_active)
    {
        if (fade_duration > 0) --fade_duration;
        else fade_active = false;
    }
}

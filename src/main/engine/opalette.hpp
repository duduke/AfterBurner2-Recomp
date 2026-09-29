#pragma once
#include <cstdint>
#include "hwvideo/hwpalette.hpp"

// Replaces the two per-VBLANK palette animation subsystems:
// 1. $014A86: Palette fade/flash countdown effect
// 2. $013F92: Multi-rate palette-cycling animation player (exhaust flame & HUD cues)
class OPalette
{
public:
    OPalette();

    // Trigger a palette flash/fade down over `duration` frames
    void trigger_flash(uint8_t duration);

    // Call once per VBLANK frame to advance cycling and fade animations
    void update(HWPalette& hw_palette);

private:
    uint16_t frame_counter{0};
    uint8_t fade_duration{0};
    uint8_t fade_channels{0x07};
    bool fade_active{false};
};

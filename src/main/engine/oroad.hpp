#pragma once
#include <cstdint>
#include "hwvideo/hwroad.hpp"

// Replaces the Sub CPU road generation engine ($000432 - $000538).
// Computes horizon pitch, roll angle, and per-scanline curvature,
// writing 224 scanlines directly to HWRoad RAM and swapping the double-buffer.
class ORoad
{
public:
    ORoad();

    void set_stage(uint8_t stage);
    void set_horizon(int16_t horizon_y);
    void set_roll(int16_t roll_angle);
    void set_curve(int16_t curve_val);

    // Updates the 224 scanline raster tables and swaps the hardware double-buffer.
    void update(HWRoad& hw_road);

private:
    uint8_t current_stage{1};
    int16_t horizon{112}; // Midpoint of 224 scanlines
    int16_t roll{0};
    int16_t curve{0};
    uint8_t priority_control{0x04};
};

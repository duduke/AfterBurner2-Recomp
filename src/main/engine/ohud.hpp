#pragma once
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/hwpalette.hpp"
struct HudState
{
    uint32_t score{};
    uint16_t hits{}, missiles{50}, speed{2};
    bool completed{};
};
class OHud
{
public:
    void init(HWPalette& palette, HWTiles& tiles, const uint8_t* rom, uint32_t length);
    void update(HWTiles& tiles, const HudState& state);
private:
    static void number(HWTiles& tiles, int column, int row, unsigned value, int digits);
    static void put(HWTiles& tiles, int column, int row, uint16_t value);
};

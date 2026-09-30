#include "engine/ohud.hpp"
#include <algorithm>
void OHud::put(HWTiles& tiles, int column, int row, uint16_t value)
{
    tiles.write_text(row * 64 + column + 24, value);
}
void OHud::number(HWTiles& tiles, int column, int row, unsigned value, int digits)
{
    value = std::min(value, digits == 7 ? 9999999u : 999u);
    for (int i = digits - 1; i >= 0; --i)
    {
        const unsigned digit = value % 10; value /= 10;
        put(tiles, column + i, row, static_cast<uint16_t>(0x8800 | ((value == 0 && digit == 0 && i < digits - 1) ? 0x20 : digit + 0x30)));
    }
}
void OHud::init(HWPalette& palette, HWTiles& tiles, const uint8_t* rom, uint32_t length)
{
    // Text palette groups selected by the original HUD initialization.
    constexpr uint32_t sources[7] = {0x30b74,0x30b34,0x30b94,0x30ba4,0x30b14,0x30bc4,0x30b54};
    for (int group = 0; group < 7; ++group)
        if (rom && sources[group] + 16 <= length)
            for (int i = 0; i < 8; ++i)
                palette.write(0x1c00 + group * 8 + i,
                              (rom[sources[group] + i * 2] << 8) | rom[sources[group] + i * 2 + 1]);
    // Original speed bar uses the last text palette, changed by its sequencer.
    constexpr uint16_t speed_colors[8] = {0,0xfff,0x00f,0xf00,0xfff,0xaaa,0x7fff,0};
    for (int i = 0; i < 8; ++i) palette.write(0x1c38 + i, speed_colors[i]);
    for (int i = 0; i < 64 * 28; ++i) tiles.write_text(i, 0x20);
    for (int i = 0; i < 7; ++i)
    {
        put(tiles, 2 + i, 1, 0x8c81);
        put(tiles, 2 + i, 2, 0x8c82 + i);
        put(tiles, 2 + i, 3, i == 0 ? 0x8c91 : i == 6 ? 0x8c93 : 0x8c92);
    }
    for (int i = 0; i < 4; ++i) put(tiles, 20 + i, 2, 0x8cd1 + i);
    for (int i = 0; i < 15; ++i) put(tiles, 1 + i, 25, 0x8f07 + i);
    for (int i = 0; i < 4; ++i) put(tiles, 17 + i, 25, 0x8d20 + i);
    number(tiles, 23, 25, 1, 1);
    palette.write(0x1c40, 0x0fa0); // cyan speed segment
    palette.write(0x1c41, 0x00ff); // yellow speed segment
    palette.write(0x1c42, 0x006f); // orange boost segment
    // Fixed top-right radar bracket; animated target pip is a ROM sprite.
    put(tiles, 30, 3, 0x8e5b); put(tiles, 33, 3, 0x8e5c);
}
void OHud::update(HWTiles& tiles, const HudState& state)
{
    number(tiles, 9, 2, state.score, 7);
    number(tiles, 25, 2, state.hits, 3);
    for (int i = 0; i < 6; ++i)
    {
        const bool visible = state.missiles > i * 10;
        put(tiles, 25 + i, 25, visible ? 0x8bfd : 0x20);
        put(tiles, 25 + i, 26, visible ? 0x8bfe : 0x20);
    }
    for (int i = 0; i < 3; ++i)
        put(tiles, 2 + i, 26, i < state.speed ? 0x8bf8 : 0x20);
    if (state.completed)
    {
        const char* message = "STAGE 1 CLEAR";
        for (int i = 0; message[i]; ++i) put(tiles, 13 + i, 8, 0x8800 | message[i]);
        const char* restart = "PRESS ENTER";
        for (int i = 0; restart[i]; ++i) put(tiles, 14 + i, 10, 0x8800 | restart[i]);
    }
}

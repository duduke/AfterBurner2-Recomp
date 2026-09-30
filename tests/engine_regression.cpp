#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "engine/oengine.hpp"

void check(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

void sprite_scaling()
{
    // Eight valid rows followed by poison rows, one eight-pixel word per row.
    std::vector<uint8_t> rom(0x40000, 0xee);
    for (int y = 0; y < 8; ++y)
    {
        // Forward reads use the second-lowest nibble as the end-of-row marker.
        const uint32_t word = 0x111111f1;
        for (int b = 0; b < 4; ++b) rom[y * 4 + b] = (word >> (b * 8)) & 255;
    }
    HWSprites hw; hw.init(rom.data(), rom.size());
    for (const uint16_t zoom : {0x100, 0x200, 0x300, 0x400, 0x580})
    {
        OSprites sprites; OEntry obj; obj.x = 8; obj.y = 8;
        sprites.append_sprite(obj, 0, 0, 1, zoom, zoom, 8, 7, 2);
        sprites.flush_to_hardware(hw);
        std::vector<uint16_t> pixels(32 * 32, 0xffff);
        hw.render(pixels.data(), 32, 32);
        int rows = 0;
        for (int y = 0; y < 32; ++y)
            if (pixels[y * 32 + 8] != 0xffff) ++rows;
        check(rows == std::max(1, 8 * 512 / zoom), "Sprite must occupy its scaled destination height");
        for (const auto pixel : pixels)
            check(pixel == 0xffff || (pixel & 15) == 1, "Zoom must preserve all 11 bits and stay within source rows");
    }
}

void palette_colors()
{
    HWPalette p(8192); HWRoad r(0x1700, 0x1720, 0x1780, -166);
    std::vector<uint8_t> roadrom(0x10000); r.init(roadrom.data(), roadrom.size());
    HWSprites s; HWTiles t; OEngine e; e.init(p, r, s, t, nullptr, 0);
    for (int i = 0; i < 32; ++i)
    {
        const auto sky = p.pen(0x1780 + i);
        check(sky.b > sky.g && sky.g > sky.r, "Every sky color must remain blue");
    }
}

void release_stick()
{
    OLevelObjs objects; OSprites sprites; ORoad road;
    for (int i = 0; i < 40; ++i) {
        objects.handle_input(1, 1, false, false, 1);
        sprites.begin_frame(); objects.update(sprites, road);
    }
    for (int i = 0; i < 40; ++i) {
        objects.handle_input(0, 0, false, false, 1);
        sprites.begin_frame(); objects.update(sprites, road);
    }
    check(objects.get_roll() == 0 && objects.get_pitch() == 0,
          "Releasing the stick must restore neutral roll and pitch");
}

int main()
{
    sprite_scaling(); palette_colors(); release_stick();
    std::cout << "Engine regression checks passed\n";
}

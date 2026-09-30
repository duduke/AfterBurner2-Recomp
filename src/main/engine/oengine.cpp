#include "engine/oengine.hpp"
#include <algorithm>

namespace
{
    // X-Board: low four bits per channel in RGB nibbles, fifth bits at 12..14.
    constexpr uint16_t pack_rgb(int r, int g, int b)
    {
        return static_cast<uint16_t>((r >> 1) | ((g >> 1) << 4) | ((b >> 1) << 8) |
                                    ((r & 1) << 12) | ((g & 1) << 13) | ((b & 1) << 14));
    }
}

OEngine::OEngine()
{
}

void OEngine::init(HWPalette& palette, HWRoad& road, HWSprites& sprites, HWTiles& tiles,
                  const uint8_t* maincpu_rom, uint32_t maincpu_len,
                  const uint8_t* subcpu_rom, uint32_t subcpu_len)
{
    // 1. Load Master Palette table from ROM ($0002EDF4, 3712 words / 7424 bytes)
    // Matches arcade reset init at $013B1C: LEA ($00120000),A1; LEA ($0002EDF4),A0; MOVE.W #$E7,D0...
    if (maincpu_rom && maincpu_len >= 0x2EDF4 + 7424)
    {
        for (uint32_t i = 0; i < 3712; ++i)
        {
            uint16_t w = static_cast<uint16_t>(
                (maincpu_rom[0x2EDF4 + i * 2] << 8) | maincpu_rom[0x2EDF4 + i * 2 + 1]);
            palette.write(i, w);
        }
    }

    // Stage 1 sky and sea, observed in original gameplay RAM. Haze blends
    // toward white over the thirty lines immediately above the sea.
    for (int i = 0; i < 32; ++i)
    {
        palette.write(0x1700 + i, pack_rgb(0, 0, 28));
        palette.write(0x1720 + i, pack_rgb(0, 21, 31));
        palette.write(0x1780 + i, pack_rgb(i * 29 / 31, 21 + i * 10 / 31, 31));
    }
    if (maincpu_rom && maincpu_len >= 0x2ea34)
        for (int i = 0; i < 16; ++i)
            palette.write(0xf10 + i, (maincpu_rom[0x2ea14 + i * 2] << 8) | maincpu_rom[0x2ea15 + i * 2]);
    palette_engine.init(maincpu_rom, maincpu_len);
    palette_engine.update(palette);

    // 3. Clear all tile pages in Tile RAM to transparent (tile 0)
    for (uint32_t addr = 0; addr < 0x8000; ++addr)
        tiles.write_tile(addr, 0);

    // Clear Text RAM registers
    tiles.write_text(0x740, 0x0000); // FG page 0
    tiles.write_text(0x741, 0x0000); // BG page 0
    tiles.write_text(0x748, 0);      // FG y-scroll
    tiles.write_text(0x749, 0);      // BG y-scroll
    tiles.write_text(0x74C, 0);      // FG x-scroll
    tiles.write_text(0x74D, 0);      // BG x-scroll
    tiles.latch_frame_registers();

    // 4. Initialize game objects and road synthesizer
    level_objs.set_rom(subcpu_rom, subcpu_len);
    level_objs.init();
    hud_engine.init(palette, tiles, maincpu_rom, maincpu_len);
    hud_engine.update(tiles, level_objs.hud());
    frame_count = 0;
    road_engine.set_stage(1);
    road_engine.update(road);
    sprite_engine.begin_frame();
    sprite_engine.flush_to_hardware(sprites);
}

void OEngine::handle_input(int stick_x, int stick_y, bool fire_vulcan, bool fire_missile, int throttle)
{
    level_objs.handle_input(stick_x, stick_y, fire_vulcan, fire_missile, throttle);
}

void OEngine::tick(HWPalette& palette, HWRoad& road, HWSprites& sprites, HWTiles& tiles)
{
    frame_count++;

    // 1. Prepare sprite buffer for this frame
    sprite_engine.begin_frame();

    // 2. Run object dispatch & flight model (emits sprites, feeds road attitude)
    level_objs.update(sprite_engine, road_engine);

    // 3. Synthesize road scanlines & flip double-buffer
    road_engine.update(road);

    // 4. Depth-sort and blit sprites to hardware RAM, triggering scaler
    sprite_engine.flush_to_hardware(sprites);

    // 5. Update palette cycling (exhaust flames and lights)
    palette_engine.update(palette);

    hud_engine.update(tiles, level_objs.hud());
    tiles.latch_frame_registers();
}

#pragma once
#include <cstdint>
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/osprites.hpp"
#include "engine/oroad.hpp"
#include "engine/opalette.hpp"

// Master Engine Coordinator.
// Connects the high-level ported game logic (objects, player, road, sprites, palette)
// to the underlying X-Board hardware layer (hwvideo/*) each frame tick.
class OEngine
{
public:
    OEngine();

    void init(HWPalette& palette, HWRoad& road, HWSprites& sprites, HWTiles& tiles,
              const uint8_t* maincpu_rom, uint32_t maincpu_len,
              const uint8_t* subcpu_rom = nullptr, uint32_t subcpu_len = 0);

    // Platform input feed (keyboard / gamepad)
    void handle_input(int stick_x, int stick_y, bool fire_vulcan, bool fire_missile, int throttle);

    // Executes one frame tick (60Hz / VBLANK step)
    void tick(HWPalette& palette, HWRoad& road, HWSprites& sprites, HWTiles& tiles);

    [[nodiscard]] OLevelObjs& get_objects() { return level_objs; }
    [[nodiscard]] ORoad& get_road() { return road_engine; }

private:
    OLevelObjs level_objs;
    OSprites sprite_engine;
    ORoad road_engine;
    OPalette palette_engine;
    OHud hud_engine;
    uint32_t frame_count{0};
};

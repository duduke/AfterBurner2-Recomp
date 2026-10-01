#pragma once
#include <cstdint>
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "engine/olevelobjs.hpp"
#include "engine/osprites.hpp"
#include <memory>
#include "engine/mainbus.hpp"
#include "engine/osubroad.hpp"
#include "engine/oscenery.hpp"
#include "engine/ostagesetup.hpp"
#include "engine/omainpalette.hpp"
#include "engine/omainframe.hpp"
#include "engine/osubsprites.hpp"

// Master Engine Coordinator.
// Verified video/scheduling ports share MainBus. Native object producers and HUD remain native until milestone 3d.
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
    [[nodiscard]] MainBus& arcade_bus() { return *bus; }

private:
    OLevelObjs level_objs;
    OSprites sprite_engine;
    MainBus::Memory memory;
    std::unique_ptr<MainBus> bus;
    OSubRoad road_engine;
    OScenery scenery_engine;
    OStageSetup stage_setup;
    OMainPalette palette_engine;
    OMainFrame frame_engine;
    OMainSprites main_sprites;
    OSubSprites sub_sprites;
    int stick_x{},stick_y{},throttle{1};
    bool vulcan{},missile{};
    OHud hud_engine;
    uint32_t frame_count{0};
};

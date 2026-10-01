#include "engine/oengine.hpp"
#include <algorithm>

OEngine::OEngine() = default;

void OEngine::init(HWPalette& palette, HWRoad& road, HWSprites& sprites, HWTiles& tiles,
                  const uint8_t* maincpu_rom, uint32_t maincpu_len,
                  const uint8_t* subcpu_rom, uint32_t subcpu_len)
{
    memory = {};
    bus = std::make_unique<MainBus>(maincpu_rom,maincpu_len,subcpu_rom,subcpu_len,memory);
    bus->endpoints.video_write = [&palette,&tiles](uint32_t address,uint16_t value) {
        if (address>=0xc0000 && address<0xd0000) tiles.write_tile((address-0xc0000)/2,value);
        else if (address>=0xd0000 && address<0xd1000) tiles.write_text((address-0xd0000)/2,value);
        else if (address>=0x120000 && address<0x124000) palette.write((address-0x120000)/2,value);
    };
    bus->endpoints.road_swap = [this,&road] {
        // MainBus has just exchanged its CPU/latched halves. Copy the newly
        // latched words into the hardware's CPU half, then trigger its latch.
        for (uint32_t a=0;a<0x1000;a+=2)
            road.write16(a,(memory.road_buffer[a]<<8)|memory.road_buffer[a+1]);
        road.read_road_control();
    };
    bus->endpoints.road_control = [&road](uint8_t value) { road.write_road_control(value); };

    stage_setup.run(*bus,0x13972);
    stage_setup.run(*bus,0x18918);
    stage_setup.run(*bus,0xddfe);
    stage_setup.run(*bus,0x13cee);
    stage_setup.run(*bus,0x13b1c);
    stage_setup.run(*bus,0x183de);
    stage_setup.run(*bus,0x13d10);
    // Host starts the retained native Stage 1 experience at the ordinary
    // flight selector accepted by $00046A/$018444. Credit/stage dispatch
    // remains Phase 3e; native player angles are not arcade bus inputs (3d).
    bus->write16(0x29c09c,0x10);
    bus->write8(0x29c200,0x1b); // $00D5A4 gameplay flags
    bus->write16(0x29c0c2,0x1fe0); // $00B958 player altitude initialization
    bus->write16(0x29c094,0xfff8); // $00B950 player view initialization
    stage_setup.run(*bus,0xe062); // $00E062: normal-road transition
    bus->sub.write8(0xee001,bus->sub.read8(SubBus::A6+0x320));
    road_engine.initialize(bus->sub,0x5c4);
    road_engine.initialize(bus->sub,0x5ee);
    // Both halves need the initialized line selectors on the first frame.
    bus->sub.read8(0xee001);
    road_engine.initialize(bus->sub,0x5c4);
    road_engine.initialize(bus->sub,0x5ee);
    scenery_engine.stage_events(*bus);

    // Objects, their palettes, and HUD remain native until 3c/3d.
    if (maincpu_rom && maincpu_len>=0x2ea34)
        for (int i=0;i<16;++i) palette.write(0xf10+i,bus->read16(0x2ea14+i*2));
    palette_engine.init(maincpu_rom,maincpu_len);
    palette_engine.update(palette);
    level_objs.set_rom(subcpu_rom,subcpu_len);
    level_objs.init();
    hud_engine.init(palette,tiles,maincpu_rom,maincpu_len);
    hud_engine.update(tiles,level_objs.hud());
    frame_count=0;
    sprite_engine.begin_frame();
    sprite_engine.flush_to_hardware(sprites);
    tiles.latch_frame_registers();
}

void OEngine::handle_input(int stick_x,int stick_y,bool fire_vulcan,bool fire_missile,int throttle)
{
    level_objs.handle_input(stick_x,stick_y,fire_vulcan,fire_missile,throttle);
}

void OEngine::tick(HWPalette& palette,HWRoad&,HWSprites& sprites,HWTiles& tiles)
{
    ++frame_count;
    sprite_engine.begin_frame();
    level_objs.update(sprite_engine);
    // $000436-$00044A, $000464/$000466, then $00046A: generate next
    // frame after latching the preceding frame, exactly once per host frame.
    road_engine.service_requests(bus->sub);
    bus->sub.read8(0xee001);
    bus->sub.write8(0xee001,bus->sub.read8(SubBus::A6+0x320));
    road_engine.frame(bus->sub);
    scenery_engine.stage_events(*bus);
    sprite_engine.flush_to_hardware(sprites);
    palette_engine.update(palette);
    hud_engine.update(tiles,level_objs.hud());
    tiles.latch_frame_registers();
}

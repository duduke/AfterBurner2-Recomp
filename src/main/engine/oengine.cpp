#include "engine/oengine.hpp"
#include <algorithm>

OEngine::OEngine() = default;

void OEngine::init(HWPalette& palette, HWRoad& road, HWSprites& sprites, HWTiles& tiles,
                  const uint8_t* maincpu_rom, uint32_t maincpu_len,
                  const uint8_t* subcpu_rom, uint32_t subcpu_len)
{
    memory = {};
    bus = std::make_unique<MainBus>(maincpu_rom,maincpu_len,subcpu_rom,subcpu_len,memory);
    bus->endpoints.video_write = [&palette,&tiles,&sprites](uint32_t address,uint16_t value) {
        if (address>=0xc0000 && address<0xd0000) tiles.write_tile((address-0xc0000)/2,value);
        else if (address>=0xd0000 && address<0xd1000) tiles.write_text((address-0xd0000)/2,value);
        else if (address>=0x100000 && address<0x101000) sprites.write16(address-0x100000,value);
        else if (address>=0x120000 && address<0x124000) palette.write((address-0x120000)/2,value);
    };
    bus->endpoints.sprite_draw = [&sprites] { sprites.swap(); };
    bus->endpoints.road_swap = [this,&road] {
        // MainBus has just exchanged its CPU/latched halves. Copy the newly
        // latched words into the hardware's CPU half, then trigger its latch.
        for (uint32_t a=0;a<0x1000;a+=2)
            road.write16(a,(memory.road_buffer[a]<<8)|memory.road_buffer[a+1]);
        road.read_road_control();
    };
    bus->endpoints.road_control = [&road](uint8_t value) { road.write_road_control(value); };
    bus->io[1].input = [this](unsigned port) -> uint8_t {
        if (port == 0) {
            uint8_t b = 0xff;
            if (vulcan) b &= ~0x10;
            if (missile) b &= ~0x20;
            return b;
        }
        return 0xff;
    };
    bus->io[0].direction[2] = 0; // Port C output (ADC mux)
    bus->io[0].output = [this](unsigned port, uint8_t data, uint8_t) {
        if (port == 2) adc_mux = data;
    };
    bus->endpoints.adc_read = [this]() -> uint8_t {
        const unsigned channel = (bus->io[0].latch[2] >> 2) & 7;
        switch (channel) {
        case 0: return raw_axis_x;
        case 1: return raw_axis_y;
        case 2: return raw_throttle;
        default: return 0x80;
        }
    };
    bus->endpoints.adc_write = [](uint8_t) {};

    input_regs = {};
    input_regs.a[5] = 0xffffa040;
    input_regs.a[6] = 0x000e4000;
    if (maincpu_rom && maincpu_len) {
        OMainObjects::initialize(*bus, input_regs);
    }
    input_regs.a[5] = 0xffffa040;
    input_regs.a[6] = 0x000e4000;

    sound_commands.clear();
    bus->compare_timer.sound_command = [this](uint8_t cmd) {
        sound_commands.push_back(cmd);
    };
    // $00D16E: enable the arcade sound queue at $8902 and initialize ring pointers
    bus->write8(0xffff8902, 0xff);
    bus->write16(0xffff8900, 0);
    bus->write8(0xffff8906, 0);
    bus->write16(0xffff8908, 0);
    bus->write16(0xffff890a, 0);

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

    // Verified reset pointers/priority. Native Stage 1 selects interpolation
    // and the terminal airframe palette transition until 3e boots a credit.
    main_sprites.initialize(*bus);
    bus->write16(0x29c056,0x1000); // $0139CA (also in verified reset tail)
    bus->write16(0x2a3c00,1); // $01486E's caller re-arm value
    bus->write8(0x3f8b6f,2); // native host opts into $014734 interpolation
    bus->write16(0x3f8c2a,0x1e);
    palette_engine.run(*bus,0x13f92);
    bus->write16(0x3f8c2a,0);
    // Empty list terminators in both reset pointer halves; Sub pass publishes
    // the real normal/interpolated terminators before the first blit.
    sub_sprites.pass(bus->sub);
    sprite_engine.bind(*bus,subcpu_rom,subcpu_len);
    level_objs.set_rom(subcpu_rom,subcpu_len);
    level_objs.init();
    hud_engine.init(palette,tiles,maincpu_rom,maincpu_len);
    hud_engine.update(tiles,level_objs.hud());
    frame_count=0;
    sprite_engine.begin_frame();
    main_sprites.blit(*bus,false);
    bus->write8(0x110001,0xff);
    tiles.latch_frame_registers();
}

void OEngine::handle_input(int stick_x,int stick_y,bool fire_vulcan,bool fire_missile,int throttle)
{
    this->stick_x=stick_x;this->stick_y=stick_y;this->throttle=throttle;
    vulcan=fire_vulcan;missile=fire_missile;

    if (stick_x == 0) raw_axis_x = 0x80;
    else if (stick_x > 0) raw_axis_x = static_cast<uint8_t>(std::min(0x80 + stick_x * 0x60, 0xdb));
    else raw_axis_x = static_cast<uint8_t>(std::max(0x80 + stick_x * 0x60, 0x25));

    if (stick_y == 0) raw_axis_y = 0x80;
    else if (stick_y > 0) raw_axis_y = static_cast<uint8_t>(std::min(0x80 + stick_y * 0x40, 0xc0));
    else raw_axis_y = static_cast<uint8_t>(std::max(0x80 + stick_y * 0x40, 0x40));

    if (throttle >= 2) raw_throttle = 0xb0;
    else if (throttle == 0) raw_throttle = 0x40;
    else raw_throttle = 0x80;
}

void OEngine::tick(HWPalette&,HWRoad&,HWSprites&,HWTiles& tiles)
{
    ++frame_count;
    for (int i = 0; i < 6; ++i) main_compare_interrupt(*bus, input_regs);
    auto phase=frame_engine.interrupt(*bus,[&](uint32_t entry) {
        switch(entry) {
        case 0x18444: scenery_engine.stage_events(*bus);break;
        case 0xb7c2: main_sprites.blit(*bus,false);break;
        case 0xb7d2: main_sprites.blit(*bus,true);break;
        case 0x148be: frame_engine.timers(*bus);break;
        case 0x1495c: frame_engine.buttons(*bus);break;
        case 0x148b8: frame_engine.bookkeeping(*bus);break;
        case 0x147e8: frame_engine.service_edge(*bus);break;
        case 0x14990: case 0xe2da: break; // ROM RTS-only callees
        case 0x16c8a: OMainWeapons::reload(*bus, input_regs); break;
        case 0x17146: case 0xc6f8: break; // 3e attract/credit boundaries
        default: palette_engine.run(*bus,entry);break;
        }
    });
    if(phase.logic && !phase.service_redirect) {
        input_regs.a[5] = 0xffffa040;
        input_regs.a[6] = 0x000e4000;
        OMainInput::buttons(*bus, input_regs);
        OMainInput::update(*bus, input_regs);

        // $000556 waits for the negative logic-phase counter. $000640 then
        // consumes the previous published requests, after the ISR's mutex.
        road_engine.service_requests(bus->sub);
        sub_sprites.pass(bus->sub);
        // Native producer only: its constants stay in logic ticks, now 30 Hz.
        sprite_engine.begin_frame();
        level_objs.handle_input(stick_x,stick_y,vulcan,missile,throttle);
        level_objs.update(sprite_engine);
        main_sprites.publish(*bus);
        frame_engine.resume_logic(*bus);
        hud_engine.update(tiles,level_objs.hud());
    }
    if(!phase.logic) {
        // $000452's BGT releases when the following display phase makes
        // the counter zero. Latch the previous road, then generate the next
        // one. Captures confirm this completes once per TWO display frames.
        bus->sub.write8(SubBus::A6+0xd6,bus->sub.read8(SubBus::A6+0xd6)|2);
        bus->sub.read8(0xee001);
        bus->sub.write8(0xee001,bus->sub.read8(SubBus::A6+0x320));
        road_engine.frame(bus->sub);
    }
    tiles.latch_frame_registers();
}

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "engine/oengine.hpp"
#include "engine/omainhost.hpp"

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
    std::vector<uint8_t> subrom(0x10010);
    const uint16_t record[]={8,7,0,0,1,7};
    for(unsigned i=0;i<6;++i) {subrom[0x10000+i*2]=record[i]>>8;subrom[0x10001+i*2]=record[i];}
    for (const uint16_t zoom : {0x100, 0x200, 0x300})
    {
        MainBus::Memory memory;MainBus bus(nullptr,0,subrom.data(),subrom.size(),memory);
        OMainSprites main;main.initialize(bus);
        OSprites sprites;sprites.bind(bus,subrom.data(),subrom.size());
        OEntry obj; obj.x = 8; obj.y = 8;
        sprites.append_sprite(obj, 0, 0, 1, zoom, zoom, 8, 7, 2);
        main.publish(bus);
        bus.write32(0x2a3c08,bus.read32(0x2a3c04));bus.write32(0x2a3c10,bus.read32(0x2a3c0c));
        OSubSprites{}.pass(bus.sub);
        bus.write32(0x2a3c14,bus.read32(0x2a3c18));bus.write32(0x2a3c1c,bus.read32(0x2a3c20));
        bus.endpoints.video_write=[&](uint32_t a,uint16_t v){if(a>=0x100000&&a<0x101000) hw.write16(a-0x100000,v);};
        bus.endpoints.sprite_draw=[&]{hw.swap();};main.blit(bus,false);bus.write8(0x110001,0xff);
        std::vector<uint16_t> pixels(32 * 32, 0xffff);
        hw.render(pixels.data(), 32, 32);
        int rows = 0;
        for (int y = 0; y < 32; ++y)
            if (pixels[y * 32 + 8] != 0xffff) ++rows;
        check(rows == (7*(0xfe00/zoom)/0x7f)+1, "Sprite must occupy its scaled destination height");
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
        check(sky.b >= sky.g && sky.g >= sky.r, "Horizon haze must remain blue/white");
    }
}

void release_stick()
{
    OLevelObjs objects; OSprites sprites;
    for (int i = 0; i < 40; ++i) {
        objects.handle_input(1, 1, false, false, 1);
        sprites.begin_frame(); objects.update(sprites);
    }
    for (int i = 0; i < 40; ++i) {
        objects.handle_input(0, 0, false, false, 1);
        sprites.begin_frame(); objects.update(sprites);
    }
    check(objects.get_roll() == 0 && objects.get_pitch() == 0,
          "Releasing the stick must restore neutral roll and pitch");
}

void publication_wait()
{
    MainBus::Memory memory;MainBus bus(nullptr,0,nullptr,0,memory);
    OMainHost::Registers r;r.a[0]=0x12345678;
    bus.write32(0x2a3c0c,0x2a3cf8);bus.write16(0x2a3cf0,17);
    bus.write16(0x2a3c00,1);
    const auto before=memory.bytes;
    check(!OMainSprites::publish(bus,r) && r.a[0]==0x12345678 && memory.bytes==before,
          "Publication must yield without changing RAM or A0 while the counter is positive");
    unsigned boundaries=0;
    OMainHost::call(bus,r,0xb676,[&](uint32_t entry,OMainHost::Registers&) {
        check(entry==0xb676,"Publication must expose its named wait boundary");
        ++boundaries;bus.write16(0x2a3c00,0);
    });
    check(boundaries==1 && r.a[0]==0x2a3cf8 && bus.read16(0x2a3cf8)==17 &&
          bus.read16(0x29c058)==17 && bus.read16(0x2a3cf0)==0,
          "Publication must resume through the port with the live A0 and request count");
    bus.write16(0x2a3c00,0x8000);bus.write16(0x2a3cf0,192);
    check(OMainSprites::publish(bus,r) && bus.read16(0x29c058)==192,
          "The signed BGT wait must publish negative counters");
}

void host_input_adc()
{
    HWPalette p(8192); HWRoad r(0x1700, 0x1720, 0x1780, -166);
    HWSprites s; HWTiles t; OEngine e;
    e.init(p, r, s, t, nullptr, 0);

    // Initial neutral state: all buttons released (0xFF at 0x150001)
    check((e.arcade_bus().read8(0x150001) & 0x30) == 0x30, "Buttons at 0x150001 must be active-low 1 when released");

    // Press Vulcan and Missile:
    e.handle_input(0, 0, true, true, 1);
    check((e.arcade_bus().read8(0x150001) & 0x30) == 0, "Buttons at 0x150001 must be 0 when pressed");

    // Run 2 ticks so both display (60 Hz) and logic (30 Hz) phases execute:
    e.tick(p, r, s, t);
    e.tick(p, r, s, t);
    check((e.arcade_bus().read8(0x29c074) & 0x30) == 0x30, "Display-frame buttons at 0x29c074 must reflect active buttons");
    check((e.arcade_bus().read8(0x29c076) & 0x30) == 0x30, "Logic-frame buttons at 0x29c076 must reflect active buttons");

    // Release buttons:
    e.handle_input(0, 0, false, false, 1);
    check((e.arcade_bus().read8(0x150001) & 0x30) == 0x30, "Buttons at 0x150001 must return to released");
    e.tick(p, r, s, t);
    e.tick(p, r, s, t);
    check((e.arcade_bus().read8(0x29c076) & 0x30) == 0, "0x29c076 must clear released buttons");
    check((e.arcade_bus().read8(0x29c077) & 0x30) == 0x30, "0x29c077 must retain previous logic buttons");

    // ADC sampling:
    // With stick_x = 1, stick_y = -1, throttle = 2:
    e.handle_input(1, -1, false, false, 2);
    e.tick(p, r, s, t);
    check(e.arcade_bus().read8(0xffff8892) == 0xdb, "ADC channel 0 (Stick X) must be clamped to 0xDB");
    check(e.arcade_bus().read8(0xffff8893) == 0x40, "ADC channel 1 (Stick Y dive) must be 0x40");
    check(e.arcade_bus().read8(0xffff8894) == 0xb0, "ADC channel 2 (Throttle afterburner) must be 0xB0");
}

int main()
{
    sprite_scaling(); palette_colors(); release_stick(); publication_wait(); host_input_adc();
    std::cout << "Engine regression checks passed\n";
}

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
    HWPalette p(8192); HWRoad r(0x1700,0x1720,0x1780,-166);
    HWSprites sprites; HWTiles tiles; OEngine e;
    e.init(p,r,sprites,tiles,nullptr,0);
    auto& bus=e.arcade_bus();
    auto& board=e.arcade_io().board;
    bus.io[0].direction[2]=0; // Explicit fixture for reset's ADC output direction.
    e.handle_input(1,-1,true,true,2);
    check((bus.read8(0x150001)&0x30)==0,"Host weapon buttons must be active low");
    check(bus.read8(0x130001)==0,"ADC must retain reset result before conversion");
    bus.write8(0x140005,0); bus.write8(0x130001,0);
    board.advance(59199);
    check(bus.read8(0x130001)==0,"ADC read before deadline must retain previous result");
    bus.write8(0x130001,0); // Busy writes must not restart conversion.
    board.advance(59200);
    check(bus.read8(0x130001)==0xe0,"ADC must preserve physical X endpoint after completion");
    uint64_t ns=59200;
    auto sample=[&](unsigned channel) {
        bus.write8(0x140005,channel<<2);bus.write8(0x130001,0);
        ns+=59200;board.advance(ns);return bus.read8(0x130001);
    };
    check(sample(1)==0xc0,"Up must use the reversed physical Y endpoint");
    check(sample(2)==0xff,"Throttle must retain physical high endpoint");
    check(sample(3)==0x80 && sample(4)==0x80 && sample(5)==0x10,
          "Motor feedback and unconnected ADC channels must match board defaults");
    e.handle_input(-1,1,false,false,0);
    check(sample(0)==0x20 && sample(1)==0x40 && sample(2)==0,
          "Physical low endpoints must survive conversion without gameplay clamping");
    // Conversion samples the lane at completion, not at its start.
    bus.write8(0x140005,0);bus.write8(0x130001,0);
    e.handle_input(0,0,false,false,1);
    ns+=59200;board.advance(ns);
    check(bus.read8(0x130001)==0x80,"Input release during conversion must be sampled at completion");
    check((bus.read8(0x150001)&0x30)==0x30,"Released buttons must be active low ones");
    bus.write8(0xe8017,0x90);bus.write8(0xe8017,0x40);
    check(e.take_sound_commands()==std::vector<uint8_t>({0x90,0x40}),
          "Sound latch callback must preserve command order");
    check(e.take_sound_commands().empty(),"Draining commands must empty the callback buffer");

    // Native frame ticks provide no CPU boundaries: they must not manufacture
    // comparator service, input-handler execution or reload production.
    bus.write8(0xffff8902,0xff);bus.write8(0xffff8906,1);
    bus.write8(0xffff890c,0x9b);bus.write8(0xffff90ff,0xff);
    bus.write16(0xffff9100,5);bus.write16(0xffff8d66,10);
    const auto time=board.now();
    e.tick(p,r,sprites,tiles);e.tick(p,r,sprites,tiles);
    check(board.now()==time && bus.read16(0xffff8900)==0 &&
          bus.read8(0xffff8906)==1 && bus.read16(0xffff8d66)==10 &&
          bus.read8(0xffff9103)==0 && e.take_sound_commands().empty(),
          "Native ticks must not guess arcade input/IRQ/reload cadence");
    e.init(p,r,sprites,tiles,nullptr,0);
    check(e.arcade_io().board.now()==0 && e.arcade_bus().read8(0x130001)==0 &&
          (e.arcade_bus().read8(0x150001)&0x30)==0x30 && e.take_sound_commands().empty(),
          "Restart must reset physical endpoints and conversion state");
}

int main()
{
    sprite_scaling(); palette_colors(); release_stick(); publication_wait(); host_input_adc();
    std::cout << "Engine regression checks passed\n";
}

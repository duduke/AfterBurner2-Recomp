#include "engine/ostagesetup.hpp"
#include <stdexcept>

namespace {
uint32_t sw(uint16_t value) { return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value))); }
void copy(MainBus& bus, uint32_t source, uint32_t destination, unsigned longs)
{
    for (unsigned i=0; i<longs; ++i) { bus.write32(destination,bus.read32(source)); source+=4; destination+=4; }
}
}
void OStageSetup::run(MainBus& bus, uint32_t entry)
{
    switch (entry)
    {
    case 0xddfe: // $00DDFE-$00DE4A: attitude reset and eight-word history.
        bus.write16(0x29c096,0); bus.write16(0x29c098,0);
        bus.write8(0x29c201,0);
        for (uint32_t a : {0xceu,0xb0u,0xb2u,0xaeu,0xb4u,0xb6u,0x9eu}) bus.write16(0x29c000+a,0);
        for (uint32_t a=0x29c620;a<0x29c630;a+=4) bus.write32(a,0);
        break;
    case 0x13972: // Tail of reset flight setup, $013972-$0139D2 (AE6E is RTS).
    {
        bus.write32(0x29c0aa,0); bus.write32(0x29c006,0);
        bus.write8(0xffff9128,0); bus.write8(0xffff911e,0);
        const uint16_t index=(bus.read8(0xffff8070)&0xc0)>>3;
        bus.write32(0xffff89da,bus.read32(0x324f8+sw(index)));
        bus.write32(0xffff89de,bus.read32(0x324fc+sw(index)));
        bus.write16(0x29c0e6,0xffa8); bus.write16(0x29c0ea,0xffa8);
        bus.write16(0x29c0e8,0x3b); bus.write16(0x29c0ec,0x3b);
        bus.write16(0x29c0ee,4); bus.write16(0x29c056,0x1000);
        break;
    }
    case 0x18918: // $018918-$018942: ordered stage-selector table, from ROM.
    {
        bus.write16(0x29c060,0); bus.write16(0xffff8d30,0);
        const uint16_t count=bus.read16(0x32480);
        bus.write16(0x29c00e,count);
        uint16_t n=static_cast<uint16_t>(count-1);
        uint32_t source=0x32482,destination=0x29c010;
        do { bus.write8(destination++,bus.read8(source++)); } while (n--!=0);
        break;
    }
    case 0x13cee: // $013CEE-$013D0E: stage palette/control descriptor.
    {
        uint16_t index=bus.read16(0x29c060);
        index=static_cast<uint16_t>((index & 0xff00)|bus.read8(0x29c010+sw(index)));
        index=static_cast<uint16_t>(index*4);
        bus.write32(0xffff8a0c,bus.read32(0x3249a+sw(index)));
        break;
    }
    case 0x13d10: // $013D10-$013D9A: both ground palette modes.
    {
        const bool alternate=bus.read8(0xffff8c2e)&1;
        uint32_t source=bus.read32(0xffff8a0c);
        source=bus.read32(source+(alternate?4:0));
        if (!alternate)
        {
            const uint32_t sky=bus.read32(source),ground=bus.read32(source+4);
            for (unsigned i=0;i<8;++i) bus.write32(0x122e40+i*4,sky);
            for (unsigned i=0;i<4;++i) bus.write32(0x122e00+i*4,ground);
        }
        else
        {
            bus.write32(0x122f00,bus.read32(source));
            for (unsigned i=0;i<4;++i) bus.write32(0x122e10+i*4,bus.read32(source+8+i*4));
            const uint32_t sky=bus.read32(source+4);
            for (unsigned i=0;i<8;++i) bus.write32(0x122e60+i*4,sky);
        }
        break;
    }
    case 0x183de: // $0183DE-$01843E: reset scenery, tail-call road palette.
        bus.write16(0xffff9040,0x8100); bus.write8(0xffff9035,0xff);
        for (uint32_t a=0xc0000;a<0xc3000;a+=4) bus.write32(a,0);
        for (uint32_t a=0xd0f00;a<0xd1000;a+=4) bus.write32(a,0);
        for (uint32_t a=0xd0e80;a<0xd0ea0;a+=4) bus.write32(a,0);
        run(bus,0x13c3e);
        break;
    case 0x13c3e: // $013C3E-$013C6A: runtime expansion of road color table.
    {
        uint32_t destination=0x123880,source=0x30d82;
        for (unsigned i=0;i<120;++i)
        {
            const uint32_t colors=0x30dfa+(bus.read8(source++)<<4);
            copy(bus,colors,destination,4); destination+=16;
        }
        break;
    }
    case 0x13b1c: // $013B1C-$013B40: master palette, 232 x eight longs.
        copy(bus,0x2edf4,0x120000,232*8);
        break;
    case 0xe062: // $00E062-$00E072: leave special road.
        bus.write16(0x29c320,0x0402);
        bus.write8(0x29c200,bus.read8(0x29c200)|0x20);
        break;
    case 0xe074: // $00E074-$00E084: request special road.
        bus.write16(0x29c320,0x0480);
        bus.write8(0x29c200,bus.read8(0x29c200)&0xdf);
        break;
    default: throw std::invalid_argument("unported stage setup entry");
    }
}

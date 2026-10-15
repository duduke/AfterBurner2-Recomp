#pragma once
#include "engine/subbus.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <span>
#include <limits>
namespace Presentation {
// Detached $7B8..$9E2 projection, copied from the verified OSubSprites port.
// Only horizontal visibility bounds are wider. All math-chip writes target a
// private shared-RAM copy; descriptor X retains an unwrapped presentation anchor.
class WideSpriteProjection {
    static constexpr uint32_t A6=SubBus::A6,DIV=0xe4000,MUL=DIV-0x4000,
        MUL_RESULT=MUL+4,MUL_LOW=MUL+6,SIZE_TABLE=0x7800,SPRITE_PRIORITY=0x56,ZOOM_TABLE=0x9e4;
    static uint32_t sext(uint16_t w){return uint32_t(int32_t(int16_t(w)));}
    static int16_t s(uint16_t w){return int16_t(w);}
    static uint32_t swap(uint32_t w){return w<<16|w>>16;}
    static uint16_t ror16(uint16_t w,int n){return uint16_t(w>>n|w<<(16-n));}
    int margin{},anchor{};
    void transform(SubBus& bus,uint32_t a5,uint32_t& a4){

    uint32_t a0, a1, d0l, d1l;
    uint16_t d0, d1, d2, d4, d5, d6, d7, t;
    uint8_t flags;   // D0's low byte: descriptor byte 8 (direction/flip/pitch sign)

    a0 = bus.read32(a5 + 12);
    const uint16_t d3 = bus.read16(a5);
    d1 = static_cast<uint16_t>(d3 & 0x3ff);
    if (d1 == 0) goto L9C8;

    if (d1 < 0x40)                                           // BCS $000806
    {
        // Very small zoom: table gives the hardware zoom, a scale factor and
        // which (lower-detail) frame record to use instead.
        a1 = ZOOM_TABLE + sext(static_cast<uint16_t>(d1 << 3));
        d1 = bus.read16(a1);
        a1 += 2;
        bus.write16(MUL, bus.read16(a1));
        a1 += 2;
        a0 += sext(bus.read16(a1));
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        uint32_t w = bus.read32(MUL_RESULT);
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        uint32_t h = bus.read32(MUL_RESULT);
        w += w; w += w; w = swap(w);                         // $000826
        h += h; h += h; h = swap(h);
        d4 = static_cast<uint16_t>(w);
        d5 = static_cast<uint16_t>(h);
    }
    else
    {
        // $0007CE: scaled size = size * zoom / $7F; hardware zoom = $FE00 / zoom.
        bus.write16(MUL, d1);
        bus.write16(DIV + 4, 0x7f);
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        bus.write32(DIV + 16, bus.read32(MUL_RESULT));
        d4 = bus.read16(DIV + 8);
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        bus.write32(DIV + 16, bus.read32(MUL_RESULT));
        d5 = bus.read16(DIV + 8);
        bus.write32(DIV, 0xfe00);
        bus.write16(DIV + 20, d1);
        d1 = bus.read16(DIV + 8);
    }

    // $000832: record scaled size for the object, start the descriptor.
    a1 = A6 + SIZE_TABLE + sext(static_cast<uint16_t>(bus.read8(a5 + 12) * 4));
    bus.write16(a1, d4);
    bus.write16(a1 + 2, d5);
    bus.write16(a4 + 8, d1);
    d1 = static_cast<uint16_t>(d1 | bus.read16(A6 + SPRITE_PRIORITY));
    bus.write16(a4 + 6, d1);

    // $000852: top line (D7) and left edge (D6), with the hardware's
    // +$100 / +$160 origin offsets.
    d7 = bus.read16(a5 + 6);
    if (d3 & 0x1000) d7 = static_cast<uint16_t>(d7 + d5);
    d1 = d5;
    if (!(d3 & 0x4000)) d1 = static_cast<uint16_t>(d1 >> 1);
    d7 = static_cast<uint16_t>(d7 - d1 + 0x100);

    d6 = bus.read16(a5 + 4);
    d1 = d4;
    {
        const uint16_t x_flag = d1 & 1;                      // LSR.W #1 -> X
        d1 = static_cast<uint16_t>(d1 >> 1);
        d6 = static_cast<uint16_t>(d6 - d1 - x_flag);        // SUBX.W D1,D6
    }
    d6 = static_cast<uint16_t>(d6 + 0x160);
    d1 = static_cast<uint16_t>(d6 + d4);                     // right edge
    if (s(d1) < 0xc0-margin) goto L9DA;
    if (s(d6) >= 0x200+margin) goto L9DA;

    bus.write16(a4, bus.read16(a0));                         // $000890
    a0 += 2;
    d2 = bus.read16(a0);
    a0 += 2;
    d4 = bus.read16(a0);                                     // pitch from here on
    a0 += 2;

    if (s(d6) < 0)                                           // TST.W D6 / BPL $0008DE
    {
        // Clipped far left: advance the ROM offset past the hidden columns.
        t = static_cast<uint16_t>(0xc0 - margin - d6);
        t = static_cast<uint16_t>(t >> 3);
        bus.write16(MUL, bus.read16(a4 + 8));
        bus.write16(MUL + 2, t);
        d0l = swap(bus.read32(MUL_RESULT) << 7);
        if (d3 & 0x2000) d2 = static_cast<uint16_t>(d2 - d0l);
        else d2 = static_cast<uint16_t>(d2 + d0l);
        d0l = sext(static_cast<uint16_t>(d0l)) << 8;         // $0008C0
        d0l += d0l;
        bus.write32(DIV, d0l);
        bus.write32(DIV + 20, bus.read32(a4 + 8));
        d6 = static_cast<uint16_t>(d6 + (bus.read16(DIV + 8) << 3));
        flags = 0xe0;
        goto L8FE;
    }

    flags = 0xe0;                                            // $0008DE
    if (s(d6) >= 0xc0-margin) goto L8FE;
    if (s(d1) >= 0x200+margin) goto L8FE;
    flags &= static_cast<uint8_t>(~0x20);   // partly off the left edge: draw right-to-left from the right edge
    t = d1; d1 = d6; d6 = t;                                 // EXG D1,D6
    d6 = static_cast<uint16_t>(d6 - 1);
    if (d3 & 0x2000) goto L90C;
    goto L904;

L8FE:
    if (!(d3 & 0x2000)) goto L90C;
L904:
    flags &= static_cast<uint8_t>(~0x40);
    d2 = static_cast<uint16_t>(d2 + d4 - 1);
L90C:
    anchor=int16_t(d6)-190;
    d6 &= 0x1ff;

    // $000910: vertical extent and clipping.
    d1 = d7;
    if (d3 & 0x1000) d7 = static_cast<uint16_t>(d7 - d5);
    else d1 = static_cast<uint16_t>(d1 + d5);
    if (s(d1) < 0x100) goto L9DA;
    if (s(d7) >= 0x1e0) goto L9DA;
    if (s(d7) >= 0x100) goto L984;

    // Clipped at the top: draw bottom-to-top from the bottom line.
    flags &= static_cast<uint8_t>(~0x80);
    t = d1; d1 = d7; d7 = t;                                 // EXG D1,D7
    d7 = static_cast<uint16_t>(d7 - 1);
    if (s(d7) < 0x200) goto L97A;

    // Also off the bottom: skip the hidden rows in the ROM offset.
    d1 = static_cast<uint16_t>(d7 - 0x1e0);                  // $000942
    bus.write16(MUL, d1);
    bus.write16(MUL + 2, bus.read16(a4 + 8));
    d1l = swap(bus.read32(MUL_RESULT) << 7);
    bus.write16(MUL, static_cast<uint16_t>(d1l));
    bus.write16(MUL + 2, d4);
    d2 = static_cast<uint16_t>(d2 - bus.read16(MUL_LOW));
    d1l = sext(static_cast<uint16_t>(d1l)) << 9;             // EXT.L / MOVEQ #9,D5 / ASL.L D5,D1
    d5 = 9;
    bus.write32(DIV, d1l);
    bus.write16(DIV + 20, bus.read16(a4 + 8));
    d7 = static_cast<uint16_t>(d7 - bus.read16(DIV + 8));

L97A:
    d5 = static_cast<uint16_t>((d5 & 0xff00) | (d7 & 0xff));  // MOVE.B D7,D5
    if (d3 & 0x1000) goto L99E;
    goto L996;

L984:
    if (s(d5) > 0xff) d5 = static_cast<uint16_t>(0x1e0 - d7);
    if (!(d3 & 0x1000)) goto L99E;
L996:
    d4 = static_cast<uint16_t>(-d4);
    d2 = static_cast<uint16_t>(d2 + bus.read16(a0));
    flags |= 0x10;   // pitch sign (descriptor word 4 bit 12)
L99E:
    d7 &= 0x1ff;
    d4 = ror16(static_cast<uint16_t>(d4 & 0x7f), 7);
    d6 = static_cast<uint16_t>(d6 | d4);
    bus.write16(a4, static_cast<uint16_t>(bus.read16(a4) | d7));
    bus.write16(a4 + 2, d2);
    bus.write16(a4 + 4, d6);
    bus.write8(a4 + 8, static_cast<uint8_t>(bus.read8(a4 + 8) | flags));
    bus.write16(a4 + 10, static_cast<uint16_t>(d5 & 0xff));
    bus.write8(a4 + 13, bus.read8(a5 + 3));
    a4 += 16;
    return;

L9C8:
    d0 = static_cast<uint16_t>(bus.read8(a5 + 12) * 4);
    bus.write32(A6 + SIZE_TABLE + sext(d0), 0);
L9DA:
    bus.write8(a4, 0x40);
    a4 += 16;

    }
public:
    struct Result {std::array<uint8_t,16> descriptor{};int x=INT32_MIN;unsigned width{};};
    Result project(std::span<const uint8_t> request,std::span<const uint8_t> shared,
                   std::span<const uint8_t> rom,unsigned extra=39){
        if(request.size()!=16||shared.size()!=0x8000||extra>160)throw std::invalid_argument("Invalid wide projection inputs");
        std::array<uint8_t,0x8000> copy;std::copy(shared.begin(),shared.end(),copy.begin());
        std::copy(request.begin(),request.end(),copy.begin()+0x400);
        std::fill(copy.begin()+0x800,copy.begin()+0x810,0);
        SubBus bus(rom.data(),rom.size(),copy.data());uint32_t output=A6+0x800;
        margin=int(extra);anchor=INT32_MIN;transform(bus,A6+0x400,output);
        Result out;std::copy_n(copy.begin()+0x800,16,out.descriptor.begin());out.x=anchor;
        out.width=bus.read16(A6+SIZE_TABLE+unsigned(request[12])*4);return out;
    }
    static Result halfway(Result out,std::span<const uint8_t> request){
        auto word=[&](unsigned i){return uint16_t(request[i]<<8|request[i+1]);};
        auto field=[&](unsigned i,int delta){auto value=uint16_t(out.descriptor[i]<<8|out.descriptor[i+1]);
            value=uint16_t((value&0xfe00)|((value-delta)&511));out.descriptor[i]=value>>8;out.descriptor[i+1]=value;};
        if(!(request[0]&8))field(0,int16_t(uint16_t(word(6)-word(10)))>>1);
        if(!(request[0]&4)){const int delta=int16_t(uint16_t(word(4)-word(8)))>>1;field(4,delta);if(out.x!=INT32_MIN)out.x-=delta;}
        return out;
    }
};
}

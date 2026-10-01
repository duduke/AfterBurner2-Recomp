#include "engine/omaininput.hpp"

namespace {
void w(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
void byte(uint32_t& r, uint8_t v) { r=(r&0xffffff00u)|v; }
int16_t s(uint32_t v) { return static_cast<int16_t>(v); }
}

void OMainInput::buttons(MainBus& b, OMainObjects::Registers& r) {
    // $014976-$01498E: retain the previous 30 Hz byte, invert active-low
    // board inputs, and preserve the upper 24 bits of the caller's D0.
    b.write8(0x29c077,b.read8(0x29c076));
    byte(r.d[0],b.read8(0x150001)); byte(r.d[0],~r.d[0]);
    b.write8(0x29c076,r.d[0]);
}

void OMainInput::update(MainBus& b, OMainObjects::Registers& r) {
    auto& d0=r.d[0]; auto& d1=r.d[1];
    const auto a5=r.a[5], a6=r.a[6];
    // $00B0AC-$00B0DA: bank gates and scripted bank override.
    w(d0,0);
    if((b.read8(0xffff91c4)&1) || b.read16(0x29c09a) || (b.read8(0xffff90fe)&1))
        goto bank_store;
    if(b.read8(0xffff8d36)&1) { w(d0,b.read16(0xffff8d38)); goto bank_store; }
    // $00B0DE-$00B11A: raw axis clamp and movement deadband accumulator.
    byte(d0,b.read8(0xffff8892));
    if(s(d0)>0xdb) w(d0,0xdb); else if(s(d0)<0x25) w(d0,0x25);
    w(d1,d0); w(d0,d0-b.read16(a5+22)); b.write16(a5+22,d1);
    b.write16(a5+24,b.read16(a5+24)+d0); w(d0,b.read16(a5+24));
    if(s(d0)<0) w(d0,-d0);
    if(s(d0)<=2) goto pitch;
    // $00B11E-$00B160: retain chip transactions, including divide trigger.
    b.write16(a5+24,0); w(d0,b.read16(a5+22)); w(d0,d0-0x80);
    b.write16(a6-0x4000,d0); b.write16(a6-0x3ffe,0x100);
    b.write32(a6,b.read32(a6-0x3ffc)); b.write16(a6+20,0xb6);
    w(d0,b.read16(a6+8));
    if(s(d0)>0x7f) w(d0,0x7f); else if(s(d0)<=-0x7f) w(d0,0xff81);
bank_store:
    b.write16(0x29c0ce,d0);
pitch:
    // $00B166-$00B1AE: one-shot pitch resync and cut-scene input override.
    w(d0,0);
    {
        uint8_t flags=b.read8(0xffff90fe); b.write8(0xffff90fe,flags&~4);
        if(flags&4) {
            byte(d0,b.read8(0xffff8893)); w(d1,d0);
            w(d1,d1-b.read16(0x29c0d0)); b.write16(a5+28,d1);
        } else if(b.read8(0xffff91c4)&1) {
            byte(d0,b.read8(0xffff91c5));
            flags=b.read8(0xffff91c4); b.write8(0xffff91c4,flags&~2);
            if(flags&2) {
                w(d1,d0); w(d1,d1-b.read16(0x29c0d0)); b.write16(a5+28,d1);
            }
        } else byte(d0,b.read8(0xffff8893));
    }
    // $00B1B2-$00B1EA: vertical clamp and independent movement accumulator.
    if(s(d0)>0xc0) w(d0,0xc0); else if(s(d0)<0x40) w(d0,0x40);
    w(d1,d0); w(d0,d0-b.read16(a5+26)); b.write16(a5+26,d1);
    b.write16(a5+28,b.read16(a5+28)+d0); w(d0,b.read16(a5+28));
    if(s(d0)<0) w(d0,-d0);
    if(s(d0)<=2) goto throttle;
    // $00B1EE-$00B22C: multiplier/divider and signed saturation.
    b.write16(a5+28,0); w(d0,b.read16(a5+26)); w(d0,d0-0x80);
    b.write16(a6-0x4000,d0); b.write16(a6-0x3ffe,0x100);
    b.write32(a6,b.read32(a6-0x3ffc)); b.write16(a6+20,0x80);
    w(d0,b.read16(a6+8));
    if(s(d0)>0x7f) w(d0,0x7f); else if(s(d0)<=-0x7f) w(d0,0xff81);
    // $00B230-$00B25C: collision/sequence gates, forced pitch, gameplay mode.
    if(b.read16(0x29c09a) || (b.read8(0xffff90fe)&1)) goto throttle;
    w(d1,b.read16(0x29c0cc)); if(uint16_t(d1)) w(d0,d1);
    if(b.read16(0x29c09c)==0x10) b.write16(0x29c0d0,d0);
throttle:
    // $00B262-$00B2B0: DIP disables analogue throttle. Upper D0 is cleared.
    d0=0x80;
    if(!(b.read8(0xffff8070)&4)) {
        byte(d0,b.read8(0xffff8894));
        if(s(d0)>0xb0) w(d0,0xb0); else if(s(d0)<0x40) w(d0,0x40);
        w(d0,d0-0x40);
        b.write16(a6-0x4000,d0); b.write16(a6-0x3ffe,0x100);
        b.write32(a6,b.read32(a6-0x3ffc)); b.write16(a6+20,0x71);
        w(d0,b.read16(a6+8));
    }
    b.write16(0x29c0d2,d0);
    // $00B2B6-$00B2FC: runtime debug switch and sub-ROM attract stream.
    if(b.read16(0xb0aa)) {
        r.a[4]=0xc3000;
        if(b.read16(0x29c09c)!=1) goto debug;
        if(!b.read16(r.a[4])) goto attract_end;
    } else {
        r.a[4]=0x230000; w(d0,b.read16(0xffff8820)); w(d0,d0&3);
        w(d0,d0+d0); w(d0,d0+d0);
        r.a[4]=b.read32(r.a[4]+s(d0));
        if(b.read16(0x29c09c)!=1) goto debug;
    }
    // $00B300-$00B352: stream frame limits; four bytes per 30 Hz update.
    if(s(b.read16(0xffffe000))>0x2ee) {
        if(s(b.read16(0xffffe000))>0x348) goto attract_end;
        b.write16(0xffffe000,b.read16(0xffffe000)+1); goto debug;
    }
    w(d0,b.read16(0xffffe000)); b.write16(0xffffe000,b.read16(0xffffe000)+1);
    r.a[1]=r.a[4]+0x2000; w(d0,d0+d0); w(d0,d0+d0); r.a[1]+=s(d0);
    byte(d1,b.read8(r.a[1]++)); w(d1,static_cast<int8_t>(d1)); b.write16(0x29c0ce,d1);
    byte(d1,b.read8(r.a[1]++)); w(d1,static_cast<int8_t>(d1)); b.write16(0x29c0d0,d1);
    w(d1,0); byte(d1,b.read8(r.a[1]++)); b.write16(0x29c0d2,d1);
    b.write8(0x29c076,b.read8(r.a[1]++)); goto debug;
attract_end:
    // $00B356-$00B35C
    b.write16(0x29c09c,b.read16(0x29c09c)+1); return;
debug:
    // $00B35E-$00B384: disabled in the production ROM, retained verbatim.
    if(!b.read16(0xb0aa) || b.read16(0x29c09c)!=0x10 || s(b.read16(r.a[4]))>0x5fa) return;
    if(s(b.read16(r.a[4]))<=0) {
        // $00B388-$00B3D6: scalar state snapshot. These are addresses, not ROM tables.
        d0=b.read32(0x29c006); b.write32(r.a[4]+2,d0);
        for(auto pair : {std::pair<uint32_t,unsigned>{0x29c060,14}, {0xffff8d30,16},
            {0xffff8d66,22}, {0xffff8802,6}, {0x29c0c0,18}, {0x29c0c2,20},
            {0xffff8d6e,24}, {0x29c0aa,26}}) {
            w(d0,b.read16(pair.first)); b.write16(r.a[4]+pair.second,d0);
        }
        // $00B3DA-$00B454: byte/word copies, including the repeated +54 write.
        for(auto pair : {std::pair<uint32_t,unsigned>{0x29c202,32}, {0xffff90fe,33},
            {0x29c201,34}, {0x29c0a1,35}, {0xffff8b6f,36}, {0x29c200,37},
            {0xffff8d7e,38}, {0xffff8c2e,39}, {0xffff8d6a,40}})
            b.write8(r.a[4]+pair.second,b.read8(pair.first));
        for(auto pair : {std::pair<uint32_t,unsigned>{0x29c07c,42}, {0x29c0ee,44},
            {0x29c0e6,46}, {0x29c0e8,48}, {0x29c0cc,50}, {0x29c320,52}})
            b.write16(r.a[4]+pair.second,b.read16(pair.first));
        b.write8(r.a[4]+54,b.read8(0xffff8d36)); b.write16(r.a[4]+56,b.read16(0xffff8d38));
        b.write8(r.a[4]+54,b.read8(0xffff8d3a));
        // $00B45A-$00B46E: DBF 7, four longs per iteration.
        r.a[1]=r.a[4]+0x170; r.a[0]=0xffff8d80; w(d0,7);
        do {
            for(unsigned i=0;i<4;++i) { auto v=b.read32(r.a[0]); r.a[0]+=4; b.write32(r.a[1],v); r.a[1]+=4; }
            w(d0,d0-1);
        } while(uint16_t(d0)!=0xffff);
        // $00B472-$00B480: DBF $40 copies 65 words, not 64.
        r.a[0]=0xffff8950; r.a[1]=r.a[4]+0x90; w(d0,0x40);
        do {
            auto v=b.read16(r.a[0]); r.a[0]+=2; b.write16(r.a[1],v); r.a[1]+=2; w(d0,d0-1);
        } while(uint16_t(d0)!=0xffff);
        // $00B484-$00B49A: ROM-selected object records.
        w(d0,b.read16(0x1c58e)); w(d0,d0-1); w(d0,d0<<4);
        r.a[0]=b.read32(0x1c588); r.a[1]=r.a[4]+0x200;
        do {
            auto v=b.read32(r.a[0]); r.a[0]+=4; b.write32(r.a[1],v); r.a[1]+=4; w(d0,d0-1);
        } while(uint16_t(d0)!=0xffff);
        // $00B49E-$00B4B4: seven palette longs.
        r.a[0]=0x121d02; r.a[1]=r.a[4]+0x132;
        for(unsigned i=0;i<7;++i) { auto v=b.read32(r.a[0]); r.a[0]+=4; b.write32(r.a[1],v); r.a[1]+=4; }
    }
    // $00B4B6-$00B4E0: debug recording stream.
    w(d0,b.read16(r.a[4])); b.write16(r.a[4],b.read16(r.a[4])+1);
    r.a[1]=r.a[4]+0x2000; w(d0,d0+d0); w(d0,d0+d0); r.a[1]+=s(d0);
    for(auto a : {0x29c0cfu,0x29c0d1u,0x29c0d3u,0x29c076u}) b.write8(r.a[1]++,b.read8(a));
}

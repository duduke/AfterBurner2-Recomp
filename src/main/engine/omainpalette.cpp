#include "engine/omainpalette.hpp"
#include <stdexcept>
namespace {
void copy(MainBus& b,uint32_t src,uint32_t dst,unsigned longs) {
    for(unsigned i=0;i<longs;++i) b.write32(dst+4*i,b.read32(src+4*i));
}
uint8_t clear(MainBus& b,uint32_t a,uint8_t mask) {
    auto old=b.read8(a); b.write8(a,old & ~mask); return old & mask;
}
}
void OMainPalette::fade(MainBus& b) {
    // $014A86-$014AD8: edge-triggered capture of the auxiliary palettes.
    uint16_t bank;
    if(clear(b,0xcdd07,1)) {
        b.write8(0xcdd05,(b.read8(0xcdd04)&0x1f)^0x1f);
        b.write8(0x29c54c,0); b.write8(0xcdd08,0);
        b.write16(0xcdc00,b.read16(0x122f00));
        copy(b,0x122e00,0xcdc02,16);
        b.write32(0xcdc42,b.read32(0x122e40));
        b.write32(0xcdc46,b.read32(0x122e60)); bank=0;
    } else {
        bank=b.read16(0xcdd02);
        if(!bank) goto apply;
    }
    // $014AE4-$014B28: four 64-palette capture banks, seven longs per palette.
    bank &=3;
    b.write16(0xcdd02,(bank+1)&3);
    for(unsigned i=0;i<64;++i) copy(b,0x120002+bank*0x800+i*32,0xcc000+bank*0x700+i*28,7);
    return;
apply:
    // $014B2A-$014BB2: upload one bank, then advance the fade bank.
    if(!b.read8(0xcdd05)) return;
    bank=b.read16(0xcdd00);
    uint32_t src=0xcc000+bank*0x380;
    for(unsigned i=0;i<32;++i) copy(b,src+i*28,0x120002+int16_t(uint16_t(bank<<10))+i*32,7);
    src+=32*28;
    bank=(bank+1)&7; b.write16(0xcdd00,bank);
    if(!bank) {
        b.write16(0x122f00,b.read16(src)); src+=2;
        copy(b,0xcd960,0x122f02,7); copy(b,src,0x122e00,16); src+=64;
        b.write32(0x122e40,b.read32(src)); src+=4;
        b.write32(0x122e60,b.read32(src));
    }
    // $014BB2-$014C24: decrement each enabled five-bit DAC channel.
    src=0xcc000+bank*0x380;
    auto mask=b.read8(0xcdd06);
    for(unsigned i=0;i<(bank==7?35u:32u)*14;++i) {
        auto color=b.read16(src); uint16_t result=0;
        const uint16_t masks[]={0x100f,0x20f0,0x4f00};
        const uint16_t bits[]={0x1000,0x2000,0x4000};
        const uint16_t steps[]={1,0x10,0x100};
        for(unsigned c=0;c<3;++c) if(!(mask&(1<<c))) {
            uint16_t v=color&masks[c];
            if(v) { bool high=v&bits[c]; v^=bits[c]; if(!high) v-=steps[c]; result|=v; }
        }
        b.write16(src,result); src+=2;
    }
    if(!b.read16(0xcdd00)) b.write8(0xcdd05,b.read8(0xcdd05)-1);
}
void OMainPalette::blend(MainBus& b) {
    // $0142DC-$0143A6: build original colors and signed channel coefficients.
    auto flags=b.read8(0xcdd08);
    if(flags&1) {
        b.write16(0xcdd08,0x240);
        b.write8(0x29c54c,b.read8(0x29c54c)|2);
        uint32_t dst=0xcc000, coeff=0xcc024;
        auto prepare=[&](uint32_t src,uint32_t target,unsigned n) {
            for(unsigned i=0;i<n;++i) {
                uint16_t a=b.read16(src),z=b.read16(target); src+=2;target+=2;
                b.write16(dst,a); dst+=2;
                for(unsigned c=0;c<3;++c) {
                    auto x=uint16_t((((a>>(c*4))&15)*2+((a>>(12+c))&1))&31);
                    auto y=uint16_t((((z>>(c*4))&15)*2+((z>>(12+c))&1))&31);
                    b.write16(coeff,uint16_t((y-x)*4)); coeff+=2;
                    b.write16(coeff,uint16_t(x<<8)); coeff+=2;
                }
            }
        };
        prepare(0xcc000,0xcc014,3);
        uint32_t src=b.read32(0x140da+int16_t(b.read16(0x3f8d30)))+2;
        prepare(src,src+32,14); return;
    }
    // $0143A8-$014422: accumulate coefficients and reassemble DAC words.
    if(!(flags&2)) return;
    uint32_t coeff=0xcc024,dst=0xcc000;
    for(unsigned i=0;i<17;++i) {
        uint16_t color=0;
        for(unsigned c=0;c<3;++c) {
            uint16_t delta=b.read16(coeff); coeff+=2;
            uint16_t value=uint16_t(delta+b.read16(coeff)); b.write16(coeff,value); coeff+=2;
            // Red/green read the HIGH byte at -2(A0), blue shifts the full word.
            if(c<2) {
                uint8_t channel=b.read8(coeff-2);
                if(channel&1) color|=uint16_t(1<<(12+c));
                if(!c) color|=(channel>>1)&15;
                else color|=uint8_t(channel<<3)&0xf0;
            } else {
                if(value&0x100) color|=0x4000;
                color|=(value>>1)&0xf00;
            }
        }
        b.write16(dst,color);dst+=2;
    }
    b.write8(0xcdd09,b.read8(0xcdd09)-1);
    if(!b.read8(0xcdd09)) clear(b,0xcdd08,2);
}
void OMainPalette::animate(MainBus& b) {
    // $013F92-$014060: ROM pointer-selected airframe transition palettes.
    if(b.read8(0xcdd05)) return;
    if(b.read16(0x3f8c2a)) {
        blend(b);
        const uint32_t tables[]={0x1413a,0x14196,0x141f2};
        const uint32_t destinations[]={0x121d22,0x121ea2,0x121e42};
        for(unsigned i=0;i<3;++i) {
            int16_t offset=uint16_t((int16_t(b.read16(0x3f8c2a))>>2)*32);
            int16_t index=b.read16(0x3f8d30);
            copy(b,b.read32(tables[i]+index)+offset+2,destinations[i]+(i==2?0:offset),7);
        }
        if(b.read16(0x3f8c2a)==0x1e)
            copy(b,0x2ea16+int16_t(uint16_t(b.read16(0x3f8d30)<<3)),0x121e22,7);
    }
    // $014062-$0140A8: throttle/phase-selected three flame colors.
    int16_t throttle=b.read16(0x29c0d2); if(throttle<0) throttle=0;
    uint16_t phase=b.read16(0x3f8b5a)&7; if(phase>3) phase^=7;
    int16_t offset=uint16_t(((throttle>>4)+phase)*6);
    uint32_t src=b.read32(0x1424e + int16_t(b.read16(0x3f8d30)))+offset;
    b.write32(0x121e42,b.read32(src)); b.write16(0x121e48,b.read16(src+4));
    // $0140AA-$0140D8: four-phase weapon palettes.
    offset=(b.read16(0x3f8b5a)&3)<<5;
    copy(b,0x2ecf6+offset,0x121fa2,2); copy(b,0x2ed76+offset,0x121fc2,2);
}
void OMainPalette::upload(MainBus& b) {
    // $014424-$01449C: publish the transition's palette colors.
    uint32_t src=0xcc000; uint16_t c=b.read16(src);src+=2;
    uint32_t dup=(uint32_t(c)<<16)|c; b.write16(0x122f00,c);
    if(!(b.read8(0x3f8c2e)&1)) {
        for(unsigned i=0;i<8;++i) b.write32(0x122e40+i*4,dup);
        c=b.read16(src);src+=2;dup=(uint32_t(c)<<16)|c;
        for(unsigned i=0;i<4;++i) b.write32(0x122e00+i*4,dup);
        src+=2;copy(b,src,0x121d02,7);
    } else { b.write32(0x122e60,b.read32(src));copy(b,src+4,0x122f02,7); }
}
void OMainPalette::run(MainBus& b,uint32_t entry) {
    switch(entry) {
    case 0x14a86: fade(b);break;
    case 0x13f92: animate(b);break;
    case 0x142dc: blend(b);break;
    case 0x14424: upload(b);break;
    // $013E5C-$013EAA: quarter-rate blend sequence; remaining compare is inert.
    case 0x13e5c: {
        if(!(b.read8(0x29c54c)&2)) return;
        upload(b);
        uint8_t next=b.read8(0x3f8c2c)+1;b.write8(0x3f8c2c,next);
        if((next^uint8_t(next-1))&4) {
            uint16_t t=b.read16(0x3f8c2a)+1;b.write16(0x3f8c2a,t);
            if(int16_t(t)>=0x20) {clear(b,0x29c54c,2);clear(b,0x3f8c2e,1);b.write16(0x3f8c2a,0);}
        }
        break;
    }
    // $013BB6/$013BCC: requested road palette banks.
    case 0x13bb6: copy(b,b.read32(0x3f9142),0x123800,32);break;
    case 0x13bcc: copy(b,0x30cf4+(b.read8(0x3f9115)<<4),0x123800+((b.read8(0x3f9114)&7)<<4),4);break;
    // $013BFA-$013C3C: repeated stage road color.
    case 0x13bfa: {
        uint32_t src=0x30d54+int16_t(uint16_t(b.read16(0x29c060)*2));
        b.write16(0x12387c,b.read16(src));
        for(unsigned i=0;i<64;++i) b.write16(0x123dc0+i*2,b.read16(src));
        break;
    }
    // $01329E-$01331E: ordinary/alternate cycle, diverted into fade RAM.
    case 0x1329e: {
        uint16_t selector=b.read16(0x29c09c)-0x11;if(!selector) return;
        uint16_t phase=b.read16(0x3f9146);
        uint32_t src=selector<6?0x35492+((phase<<1)&0x1e0):0x35192+((phase<<3)&0x3e0);
        copy(b,src,b.read8(0xcdd05)?0xcdab0:0x121e82,7);break;
    }
    default: throw std::invalid_argument("unported palette entry");
    }
}

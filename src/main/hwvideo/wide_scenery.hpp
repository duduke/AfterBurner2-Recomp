#pragma once
#include "engine/omainobjects.hpp"
namespace Presentation::SceneryProjection {
// Detached scenery lifetime uses the verified $8AF2..$8C14 port. The only
// extended behavior is its horizontal cull margin; native objects stay unchanged.

inline void w(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
inline void byte(uint32_t& r, uint8_t v) { r=(r&0xffffff00u)|v; }
inline int16_t s(uint32_t v) { return static_cast<int16_t>(v); }
inline void asr(uint32_t& r, unsigned n) { w(r,n>=16 ? (s(r)<0 ? 0xffff : 0) : s(r)>>n); }
inline void asrl(uint32_t& r, unsigned n) { r=static_cast<int32_t>(r)>>n; }
// $008B3E-$008B60 / $009220-$009242 / $00924E-$009270.
inline void sway(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5], m=r.a[6]; auto& d=r.d;
    w(d[6],d[7]); asr(d[6],4); r.a[0]=0x33ed8;
    byte(d[6],b.read8(r.a[0]+s(d[6]))); w(d[6],static_cast<int8_t>(d[6]));
    b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c0c6));
    d[6]=b.read32(m-0x3ffc); asrl(d[6],6); b.write16(p+42,b.read16(p+42)+d[6]);
}
// $008B64-$008B90 / $009274-$0092A0.
inline void horizontal(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5], m=r.a[6]; auto& d=r.d;
    b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]+=d[4]; asrl(d[0],7);
    b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[7]); d[0]=b.read32(m-0x3ffc);
    asrl(d[0],8); asrl(d[0],2); w(d[0],d[0]+b.read16(0x29c08e)); b.write16(p+4,d[0]);
}
// $008BBC-$008BEC / $0092BA-$0092EA.
inline void vertical(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5], m=r.a[6]; auto& d=r.d;
    b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]-=d[5]; asrl(d[0],7);
    b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[7]); d[0]=b.read32(m-0x3ffc);
    asrl(d[0],8); asrl(d[0],2); w(d[0],d[0]+0x70); w(d[0],d[0]-b.read16(0x29c08c)); b.write16(p+6,d[0]);
}

inline bool update(MainBus& b,OMainObjects::Registers& r,unsigned margin){
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    // $008AF2-$008B20: move depth and compute scale, delete past origin.
    b.write32(p+8,b.read32(p+4)); w(d[1],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<0) { return false; }
    w(d[1],d[1]+0x20); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]);
    // $008B22-$008B3C: packed object scale, runtime world offset pair.
    w(d[1],d[0]); asr(d[0],3); b.write8(p+2,d[0]); byte(d[0],b.read8(p+21)); asr(d[1],d[0]&63);
    b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[1]);
    r.a[0]=b.read32(p+54); d[4]=b.read32(r.a[0]); r.a[0]+=4; d[5]=b.read32(r.a[0]); sway(b,r); horizontal(b,r);
    // $008B94-$008BB8: clipping reads the sub CPU's scaled-width table.
    r.a[1]=0x2a3800; w(d[1],0); byte(d[1],b.read8(p+20)); w(d[1],d[1]+d[1]); w(d[1],d[1]+d[1]);
    w(d[1],b.read16(r.a[1]+s(d[1]))); asr(d[1],1); w(d[1],d[1]+0xa0+margin);
    if(s(d[0])>s(d[1])) { return false; }
    w(d[1],-d[1]); if(s(d[0])<=s(d[1])) { return false; }
    vertical(b,r);
    // $008BF0-$008C14: animation offset and flip flags come from shared RAM.
    r.a[0]=0x29c0a2; w(d[0],0); byte(d[0],b.read8(p+59)); w(d[1],b.read16(r.a[0]+s(d[0])));
    r.a[1]=b.read32(p+60); b.write32(p+12,b.read32(r.a[1]+s(d[1])));
    b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); return true;

}
inline bool finish(MainBus& b,OMainObjects::Registers& r){
    auto& d=r.d;const auto p=r.a[5];
    vertical(b,r);
    // $008BF0-$008C14: animation offset and flip flags come from shared RAM.
    r.a[0]=0x29c0a2; w(d[0],0); byte(d[0],b.read8(p+59)); w(d[1],b.read16(r.a[0]+s(d[0])));
    r.a[1]=b.read32(p+60); b.write32(p+12,b.read32(r.a[1]+s(d[1])));
    b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); return true;

}
}

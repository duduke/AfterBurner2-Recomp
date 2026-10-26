#pragma once
#include "hwvideo/wide_explosion.hpp"

namespace Presentation::HitEffectProjection {
using namespace SceneryProjection;
using State=ExplosionProjection::State;
// Detached $8E18/$8ECE updates. Only the horizontal retirement margin changes.
inline State finish(MainBus& b,OMainObjects::Registers& r){
    auto& d=r.d;const auto p=r.a[5];
    // $8FAE..$8FF6: native vertical projection and hidden/retired boundaries.
    vertical(b,r);w(d[1],d[7]);asr(d[1],3);w(d[1],-d[1]);
    if(s(d[0])<=s(d[1]))return State::hidden;
    w(d[1],-d[1]);w(d[1],d[1]+0xe0);
    if(s(d[0])>s(d[1]))return State::retired;
    // $8FFA..$902E: advance on even ages until the byte frame count matches.
    // Palette comes from the base frame, before the shared direction offset.
    byte(d[0],b.read8(p+46));
    if(uint8_t(d[0])!=b.read8(p+47)){
        w(d[0],b.read16(p+38));w(d[0],d[0]&1);
        if(!uint16_t(d[0]))b.write8(p+47,b.read8(p+47)+1);
    }
    byte(d[0],b.read8(p+47));w(d[0],static_cast<int8_t>(d[0]));w(d[0],d[0]<<4);r.a[0]=b.read32(p+60);
    b.write8(p+3,b.read8(r.a[0]+s(d[0])+12));w(d[0],d[0]+b.read16(0x29c0a4));
    b.write32(p+12,b.read32(r.a[0]+s(d[0])));
    // $9034..$9044: shared flips; caller publishes only the detached request.
    b.write8(p,b.read8(p)&0xcf);byte(d[0],b.read8(0x29c0a0));b.write8(p,b.read8(p)|d[0]);
    return State::visible;
}
inline State update(MainBus& b,OMainObjects::Registers& r,unsigned margin,bool ground){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    // $8E18..$8E54 / $8ECE..$8F0A: previous position, pause, age/depth/scale.
    b.write32(p+8,b.read32(p+4));if(b.read8(0xffff91d0)&1)return State::visible;
    b.write16(p+38,b.read16(p+38)+1);w(d[1],b.read16(0x29c0aa));b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[1],b.read16(p+36));asr(d[1],5);if(s(d[1])<0)return State::retired;
    w(d[1],d[1]+0x20);b.write32(m,0x7fe0);b.write16(m+20,d[1]);w(d[0],b.read16(m+8));w(d[7],d[0]);
    // $8E56..$8E82 / $8F0C..$8F38: signed age cap, word product shift.
    asr(d[0],2);w(d[1],d[0]);asr(d[0],1);b.write8(p+2,d[0]);
    w(d[2],b.read16(p+38));if(s(d[2])>4)w(d[2],4);
    b.write16(m-0x4000,d[2]);b.write16(m-0x3ffe,d[1]);d[1]=b.read32(m-0x3ffc);asr(d[1],2);
    b.write16(p,b.read16(p)&0xfc00);b.write16(p,b.read16(p)|d[1]);
    // $8E84..$8ECA / $8F3A..$8F64: ground drift bypasses sway; air never does.
    r.a[0]=b.read32(p+54);d[4]=b.read32(r.a[0]);r.a[0]+=4;d[5]=b.read32(r.a[0]);
    if(ground&&b.read16(0x29c33c)){w(d[0],b.read16(0x29c33e));b.write16(p+42,b.read16(p+42)+d[0]);}
    else sway(b,r);
    // $8F68..$8FAA: native horizontal projection with the presentation margin.
    horizontal(b,r);w(d[1],d[7]);asr(d[1],4);w(d[1],d[1]+0xa0+margin);
    if(s(d[0])>s(d[1]))return State::retired;
    w(d[1],-d[1]);if(s(d[0])<=s(d[1]))return State::retired;
    return finish(b,r);
}
}

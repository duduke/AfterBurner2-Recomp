#pragma once
#include "hwvideo/wide_scenery.hpp"

namespace Presentation::ExplosionProjection {
using namespace SceneryProjection;
// $91C0..$9364, detached world-explosion presentation. Only horizontal
// retirement is extended; hidden vertical frames remain alive but unpublished.
enum class State { retired, hidden, visible };
inline State finish(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5];
    // $92BA..$9302: native vertical projection, hidden top and retired bottom.
    vertical(b,r); w(d[1],d[7]); asr(d[1],3); w(d[1],-d[1]);
    if(s(d[0])<=s(d[1]))return State::hidden;
    w(d[1],-d[1]); w(d[1],d[1]+0xe0);
    if(s(d[0])>s(d[1]))return State::retired;
    // $9306..$934E: signed timer and live ROM animation payload.
    const auto old=b.read16(p+48); b.write16(p+48,old-1);
    if(s(old)<1){
        byte(d[1],b.read8(p+46));
        if(static_cast<int8_t>(d[1])>static_cast<int8_t>(b.read8(p+47))){
            b.write8(p+47,b.read8(p+47)+1); w(d[0],0); byte(d[0],b.read8(p+47)); w(d[0],d[0]<<3);
            r.a[0]=b.read32(p+60); b.write32(p+12,b.read32(r.a[0]+s(d[0])));
            b.write8(p+3,b.read8(r.a[0]+s(d[0])+4)); w(d[1],0); byte(d[1],b.read8(r.a[0]+s(d[0])+6)); b.write16(p+48,d[1]);
            w(d[1],0); byte(d[1],b.read8(r.a[0]+s(d[0])+7)); b.write16(p+50,d[1]); b.write8(p+21,b.read8(r.a[0]+s(d[0])+5));
        }
    }
    // $9354..$9364: shared flips; the caller owns detached submission.
    b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]);
    return State::visible;
}
inline State update(MainBus& b, OMainObjects::Registers& r, unsigned margin){
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    // $91C0..$91FE: previous position, pause, age/depth and native scale.
    b.write32(p+8,b.read32(p+4)); if(b.read8(0xffff91d0)&1)return State::visible;
    b.write16(p+38,b.read16(p+38)+1); w(d[1],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<0)return State::retired;
    w(d[1],d[1]+0x20); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]);
    // $9200..$921E: unsigned multiply and packed word scale.
    asr(d[0],2); w(d[2],d[0]); asr(d[0],1); b.write8(p+2,d[0]);
    d[2]=uint32_t(uint16_t(d[2]))*b.read16(p+50); asrl(d[2],8); byte(d[1],b.read8(p+21)); w(d[1],d[1]&3);
    w(d[2],d[2]<<(d[1]&63)); b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[2]);
    // $9220..$92A0: two sway operations around the shared camera-pair read.
    sway(b,r); r.a[0]=b.read32(p+54); d[4]=b.read32(r.a[0]); r.a[0]+=4; d[5]=b.read32(r.a[0]); sway(b,r); horizontal(b,r);
    // $92A4..$92B6: widen only the horizontal retirement threshold.
    w(d[1],d[7]); asr(d[1],4); w(d[1],d[1]+0xa0+margin);
    if(s(d[0])>s(d[1]))return State::retired;
    w(d[1],-d[1]); if(s(d[0])<=s(d[1]))return State::retired;
    return finish(b,r);
}
}

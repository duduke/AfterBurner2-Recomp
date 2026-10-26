#pragma once
#include "hwvideo/wide_explosion.hpp"
#include "engine/omainweapons.hpp"

namespace Presentation::TrailEffectProjection {
using namespace SceneryProjection;
using State=ExplosionProjection::State;
enum Family : unsigned { player=1,enemy=2,damage=3 };

// $AB6E..$ABB8: player trail forward projection and perspective divisor.
inline void player_x(MainBus& b,OMainObjects::Registers& r){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    b.write16(m-0x4000,b.read16(0x29c092));b.write16(m-0x3ffe,b.read16(p+42));d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44));w(d[6],d[6]-b.read16(0x29c080));b.write16(m-0x4000,d[6]);b.write16(m-0x3ffe,b.read16(0x29c090));d[6]=b.read32(m-0x3ffc);
    d[0]+=d[6];asrl(d[0],7);d[0]<<=5;w(d[2],d[7]);w(d[2],d[2]+0x20);
    b.write16(m+4,d[2]);b.write32(m+16,d[0]);w(d[0],b.read16(m+8));w(d[0],d[0]+b.read16(0x29c08a));b.write16(p+4,d[0]);
}
inline State player_finish(MainBus& b,OMainObjects::Registers& r){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    // $ABD6..$AC1A: Y reuses D2's divisor. Reinstall it in the private chip
    // when resuming from a captured native cull; no host math chip is touched.
    b.write16(m+4,d[2]);
    b.write16(m-0x4000,b.read16(0x29c090));b.write16(m-0x3ffe,b.read16(p+42));d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44));w(d[6],d[6]-b.read16(0x29c080));b.write16(m-0x4000,d[6]);b.write16(m-0x3ffe,b.read16(0x29c092));d[6]=b.read32(m-0x3ffc);
    d[0]-=d[6];asrl(d[0],7);d[0]<<=5;b.write32(m+16,d[0]);w(d[0],b.read16(m+8));w(d[0],d[0]+0x70);w(d[0],d[0]-b.read16(0x29c088));b.write16(p+6,d[0]);
    // $AC1E..$AC46: top hides, bottom retires, palette follows the ramp age.
    w(d[1],0);byte(d[1],b.read8(p+1));asr(d[1],1);w(d[1],-d[1]);if(s(d[0])<=s(d[1]))return State::hidden;
    w(d[1],-d[1]);w(d[1],d[1]+0xe0);if(s(d[0])>s(d[1]))return State::retired;
    w(d[0],b.read16(p+50));w(d[0],d[0]+0xe8);b.write8(p+3,d[0]);return State::visible;
}
inline State player_update(MainBus& b,OMainObjects::Registers& r,unsigned margin){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    // $AAEE..$AB1E: this controller has no native draw-only/pause-bit gate.
    b.write32(p+8,b.read32(p+4));w(d[1],b.read16(0x29c0ac));b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[7],b.read16(p+36));asr(d[7],5);w(d[1],d[7]);if(s(d[1])<0)return State::retired;
    w(d[1],d[1]+0x20);b.write16(m+4,d[1]);b.write32(m+16,0x1fe0);w(d[0],b.read16(m+8));b.write8(p+2,d[0]);
    // $AB22..$AB44: first-eight-update width ramp.
    if(s(b.read16(p+50))<=7){
        b.write16(p+50,b.read16(p+50)+1);w(d[1],b.read16(p+50));w(d[1],d[1]+8);
        b.write16(m-0x4000,d[1]);b.write16(m-0x3ffe,d[0]);d[0]=b.read32(m-0x3ffc);asr(d[0],4);
    }
    b.write8(p+1,d[0]);
    // $AB48..$AB6A: native curvature drift.
    w(d[6],d[7]);asr(d[6],2);r.a[0]=0x33e98;byte(d[6],b.read8(r.a[0]+s(d[6])));w(d[6],static_cast<int8_t>(d[6]));
    b.write16(m-0x4000,d[6]);b.write16(m-0x3ffe,b.read16(0x29c0c4));d[6]=b.read32(m-0x3ffc);asrl(d[6],6);b.write16(p+42,b.read16(p+42)+d[6]);
    player_x(b,r);
    // $ABBC..$ABD2: only horizontal retirement gains a margin.
    w(d[1],0);byte(d[1],b.read8(p+1));asr(d[1],2);w(d[1],d[1]+0xa0+margin);
    if(s(d[0])>s(d[1]))return State::retired;
    w(d[1],-d[1]);if(s(d[0])<=s(d[1]))return State::retired;
    return player_finish(b,r);
}

inline State enemy_finish(MainBus& b,OMainObjects::Registers& r){
    // $14FAC..$14FC2: palette only. Native ring unlink/free is never replayed.
    auto& d=r.d;const auto p=r.a[5];w(d[0],b.read16(p+44));if(s(d[0])>15)d[0]=15;
    w(d[0],uint16_t(d[0])>>1);byte(d[0],d[0]+0xf5);b.write8(p+3,d[0]);return State::visible;
}
inline State enemy_update(MainBus& b,OMainObjects::Registers& r,unsigned margin){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    // $14EE2..$14F26: enable/draw-only, 36-tick age and unsigned depth limits.
    b.write32(p+8,b.read32(p+4));if(!(b.read8(0x29c200)&2))return State::retired;
    if(b.read8(0xffff91d0)&2)return State::visible;
    b.write16(p+44,b.read16(p+44)+1);if(s(b.read16(p+44))>=0x24)return State::retired;
    w(d[1],b.read16(0x29c0aa));w(d[1],d[1]+b.read16(p+46));b.write16(p+36,b.read16(p+36)-d[1]);
    if(b.read16(p+36)>=0x80e0)return State::retired;
    w(d[1],b.read16(p+36));w(d[1],uint16_t(d[1])>>5);if(uint16_t(d[1])>=0x3ff)return State::retired;
    // $14F2A..$14F88: native growth/taper and the verified pure projection tails.
    w(d[1],d[1]+0x20);b.write32(m,0x7fe0);b.write16(m+20,d[1]);w(d[0],b.read16(m+8));w(d[7],d[0]);asr(d[0],2);
    w(d[1],d[0]);w(d[1],d[1]-4);if(s(d[1])<=0)d[1]=1;b.write8(p+2,d[1]);w(d[1],b.read16(p+44));
    bool grow=s(d[1])<13;
    if(s(d[1])>=30){w(d[1],d[1]-0x24);w(d[1],-d[1]);w(d[1],uint16_t(d[1])>>1);grow=true;}
    if(grow){w(d[1],d[1]+4);b.write16(m-0x4000,d[1]);b.write16(m-0x3ffe,d[0]);d[0]=b.read32(m-0x3ffc);asrl(d[0],4);}
    asr(d[0],1);w(d[1],d[0]);asr(d[1],2);w(d[0],d[0]+d[1]);b.write8(p+1,d[0]);
    OMainWeapons::enemy(b,r,0x15842);OMainWeapons::enemy(b,r,0x15200);
    // $14F92..$14FA8: symmetric signed >= boundary (not the player's > / <=).
    w(d[0],b.read16(p+4));if(s(d[0])<0)w(d[0],-d[0]);w(d[1],0);byte(d[1],b.read8(p+1));asr(d[1],1);w(d[1],d[1]+0xb4+margin);
    if(s(d[0])>=s(d[1]))return State::retired;
    return enemy_finish(b,r);
}

// $FF08..$FF52: damage-emission projection before its horizontal cull.
inline void damage_x(MainBus& b,OMainObjects::Registers& r){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    b.write16(m-0x4000,b.read16(0x29c092));b.write16(m-0x3ffe,b.read16(p+42));d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44));w(d[6],d[6]-b.read16(0x29c080));b.write16(m-0x4000,d[6]);b.write16(m-0x3ffe,b.read16(0x29c090));
    d[6]=b.read32(m-0x3ffc);d[0]+=d[6];asrl(d[0],7);d[0]<<=5;
    w(d[2],d[7]);w(d[2],d[2]+0x20);b.write32(m,d[0]);b.write16(m+20,d[2]);w(d[0],b.read16(m+8));
    w(d[0],d[0]+b.read16(0x29c08a));b.write16(p+4,d[0]);
}
inline void damage_y(MainBus& b,OMainObjects::Registers& r){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    // $FF70..$FFBC: Y and perspective. D2 belongs to the current X projection.
    b.write16(m-0x4000,b.read16(0x29c090));b.write16(m-0x3ffe,b.read16(p+42));d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44));w(d[6],d[6]-b.read16(0x29c080));b.write16(m-0x4000,d[6]);b.write16(m-0x3ffe,b.read16(0x29c092));
    d[6]=b.read32(m-0x3ffc);d[0]-=d[6];asrl(d[0],7);d[0]<<=5;
    b.write32(m,d[0]);b.write16(m+20,d[2]);w(d[0],b.read16(m+8));w(d[0],d[0]+0x70);w(d[0],d[0]-b.read16(0x29c088));b.write16(p+6,d[0]);
}
inline State damage_finish(MainBus& b,OMainObjects::Registers& r,bool animation){
    auto& d=r.d;const auto p=r.a[5];
    damage_y(b,r);
    // $FD76..$FDA4 / $FEA0..$FF06: native hidden/bottom limits and animation.
    if(!animation)w(d[1],0);
    w(d[1],b.read16(p));w(d[1],d[1]&0x3ff);asr(d[1],1);w(d[1],-d[1]);if(s(d[0])<=s(d[1]))return State::hidden;
    w(d[1],-d[1]);w(d[1],d[1]+0xe0);if(s(d[0])>s(d[1]))return State::retired;
    if(animation){
        // $FEBC..$FEEE: SUBQ/BGT preserves signed overflow.
        const auto old=b.read16(p+32),value=uint16_t(old-1);b.write16(p+32,value);
        if(!(value&&((value&0x8000)!=0)==(old==0x8000))){
            r.a[0]=b.read32(p+26);b.write16(p+30,b.read16(p+30)+1);w(d[0],b.read16(p+30));w(d[0],d[0]<<3);
            const auto a=r.a[0]+s(d[0]);b.write32(p+12,b.read32(a));b.write16(p+32,b.read16(a+6));
            byte(d[1],b.read8(a+5));w(d[1],d[1]&0xff);b.write16(p+34,d[1]);b.write8(p+3,b.read8(a+4));
        }
        w(d[0],b.read16(p));w(d[0],d[0]&0x3ff);d[0]=uint16_t(d[0])*b.read16(p+34);asrl(d[0],7);
        b.write16(p,b.read16(p)&0xfc00);b.write16(p,b.read16(p)|uint16_t(d[0]));
    }else{w(d[0],b.read16(p+50));w(d[0],d[0]+0xe8);b.write8(p+3,d[0]);}
    return State::visible;
}
inline State damage_update(MainBus& b,OMainObjects::Registers& r,unsigned margin,uint32_t entry=0){
    auto& d=r.d;const auto p=r.a[5],m=r.a[6];
    // $FCB4/$FDFA: native disable/draw-only gates and visual animation transition.
    b.write32(p+8,b.read32(p+4));if(!(b.read8(0x29c200)&4))return State::retired;
    if(b.read8(0xffff91d0)&4)return State::visible;
    bool animation=(entry?entry:b.read32(p+16))==0xfdfa;
    if(!animation&&s(b.read16(0xffff9124))>=5){
        // $FDA6..$FDFC: seed ROM animation. Native alternating-sound counter
        // and sound enqueue are deliberately not replayed by a visual copy.
        b.write16(p+30,0);r.a[0]=0x2bc34;const auto a=r.a[0];
        b.write32(p+26,a);b.write32(p+12,b.read32(a));w(d[0],b.read16(a+6));w(d[0],d[0]+1);b.write16(p+32,d[0]);
        byte(d[0],b.read8(a+5));w(d[0],d[0]&0xff);b.write16(p+34,d[0]);b.write8(p+3,b.read8(a+4));
        b.write16(p+50,0);b.write32(p+16,0xfdfa);animation=true;b.write32(p+8,b.read32(p+4));
    }
    // $FCDA..$FD74 / $FE16..$FE9E: depth, first-eight-tick ramp and lateral sway.
    w(d[1],b.read16(0x29c0aa));if(!animation)asr(d[1],b.read16(0xffff9124)==4?1:3);
    b.write16(p+36,b.read16(p+36)-d[1]);w(d[7],b.read16(p+36));asr(d[7],5);w(d[1],d[7]);if(s(d[1])<0)return State::retired;
    w(d[1],d[1]+0x20);b.write16(m+20,d[1]);b.write32(m+16,0x1fe0);w(d[0],b.read16(m+8));
    w(d[1],d[0]);w(d[1],d[1]+0x10);if(s(d[1])>255)w(d[1],255);b.write8(p+2,d[1]);
    if(s(b.read16(p+50))<=7){b.write16(p+50,b.read16(p+50)+1);w(d[1],b.read16(p+50));w(d[1],d[1]+8);d[0]=uint16_t(d[0])*uint16_t(d[1]);asr(d[0],4);}
    asr(d[0],1);b.write16(p,b.read16(p)&0xfc00);b.write16(p,b.read16(p)|uint16_t(d[0]));
    w(d[6],d[7]);asr(d[6],2);r.a[0]=0x33e98;byte(d[6],b.read8(r.a[0]+s(d[6])));w(d[6],static_cast<int8_t>(d[6]));
    b.write16(m-0x4000,d[6]);b.write16(m-0x3ffe,b.read16(0x29c0c6));d[6]=b.read32(m-0x3ffc);asrl(d[6],4);b.write16(p+42,b.read16(p+42)+d[6]);
    damage_x(b,r);
    // $FF56..$FF6C: retire the copy, never call native $102F2/free its pool.
    w(d[1],b.read16(p));w(d[1],d[1]&0x3ff);asr(d[1],2);w(d[1],d[1]+0xa0+margin);
    if(s(d[0])>s(d[1]))return State::retired;
    w(d[1],-d[1]);if(s(d[0])<=s(d[1]))return State::retired;
    return damage_finish(b,r,animation);
}
inline State update(MainBus& b,OMainObjects::Registers& r,unsigned margin,unsigned family,uint32_t entry=0){
    if(family==player)return player_update(b,r,margin);
    if(family==enemy)return enemy_update(b,r,margin);
    return damage_update(b,r,margin,entry);
}
inline State finish(MainBus& b,OMainObjects::Registers& r,unsigned family){
    if(family==player)return player_finish(b,r);
    if(family==enemy)return enemy_finish(b,r);
    return damage_finish(b,r,b.read32(r.a[5]+16)==0xfdfa);
}
}

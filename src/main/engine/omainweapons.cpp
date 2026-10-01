#include "engine/omainweapons.hpp"
#include "engine/omainsound.hpp"
#include "engine/omainplayer.hpp"
#include <stdexcept>
#include <functional>

namespace {
void w(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
void byte(uint32_t& r, uint8_t v) { r=(r&0xffffff00u)|v; }
int16_t s(uint32_t v) { return static_cast<int16_t>(v); }
}

void OMainWeapons::decimal(OMainObjects::Registers& r) {
    // $00D062-$00D08C / $00D090-$00D09E: four DIVU steps, nibble fold.
    r.d[3]=0; r.d[0]&=0xffff;
    for(auto divisor : {10000u,1000u,100u,10u}) {
        w(r.d[1],divisor);
        const uint32_t dividend=r.d[0];
        r.d[0]=((dividend%divisor)<<16)|(dividend/divisor);
        byte(r.d[3],r.d[3]|r.d[0]); r.d[3]<<=4;
        r.d[0]&=0xffff0000; r.d[0]=(r.d[0]<<16)|(r.d[0]>>16);
    }
    byte(r.d[3],r.d[3]|r.d[0]); r.d[0]=r.d[3];
}

void OMainWeapons::missile_hud(MainBus& b, OMainObjects::Registers& r) {
    auto& d0=r.d[0]; auto& d1=r.d[1]; auto& d2=r.d[2];
    // $00CFC4-$00CFEA: only gameplay selectors; clamp displayed ammunition.
    if(s(b.read16(0x29c09c))<15 || s(b.read16(0x29c09c))>18) return;
    w(d0,b.read16(0xffff8d66)); if(s(d0)>s(b.read16(0x3218a))) w(d0,b.read16(0x3218a));
    // $00CFF0-$00D008: clear fourteen pairs of text tiles.
    r.a[0]=0xd0d62; r.a[1]=r.a[0]; w(d1,0x20); d2=13;
    do {
        b.write16(r.a[1]-128,d1); b.write16(r.a[1],d1); r.a[1]+=2; w(d2,d2-1);
    } while(uint16_t(d2)!=0xffff);
    if(s(d0)<=0) return;
    // $00D00E-$00D026: count tens from packed decimal, plus hundreds.
    decimal(r); w(d1,d0); w(d1,d1&0xf0); w(d1,(uint16_t(d1)>>4)|(d1<<12));
    if(s(d0)>=0x100) w(d1,d1+10); w(d1,d1-1);
    // $00D028-$00D036: each full ten renders one two-tile missile.
    if(s(d1)>=0) {
        do {
            b.write16(r.a[0]-128,0x8bfd); b.write16(r.a[0],0x8bfe); r.a[0]+=2; w(d1,d1-1);
        } while(uint16_t(d1)!=0xffff);
    }
    // $00D03A-$00D05C: partial pair tiles and trailing half.
    w(d0,d0&15); if(!uint16_t(d0)) return;
    w(d0,d0-2);
    if(s(d0)>=0) {
        w(d1,d0); w(d0,s(d0)>>1);
        do { b.write16(r.a[0],0x8bfc); r.a[0]+=2; w(d0,d0-1); } while(uint16_t(d0)!=0xffff);
        if(!(d1&1)) return;
    }
    b.write16(r.a[0],0x8bfb); r.a[0]+=2;
}

void OMainWeapons::reload(MainBus& b, OMainObjects::Registers& r) {
    auto& d0=r.d[0]; auto& d1=r.d[1];
    // $016C8A-$016CD2 / $016D22-$016D54: two original refill requests.
    if(b.read8(0xffff9102)&2) {
        w(d1,b.read16(0x3218c)); w(d0,b.read16(0xffff8d66)); w(d0,d0+b.read16(0x3218c));
        if(s(d0)>s(d1)) w(d0,d1); w(d0,d0-b.read16(0xffff8d66)); b.write16(0xffff9100,d0);
        b.write8(0xffff9102,0); b.write8(0xffff90ff,0xff); b.write8(0xffff9103,0);
    } else if(b.read8(0xffff9102)&1) {
        w(d1,b.read16(0x3218a)); w(d0,b.read16(0xffff8d66)); w(d0,d0+b.read16(0x3218c));
        w(d0,d0+b.read16(0xffff9106)); b.write16(0xffff9106,0);
        if(s(d0)>s(d1)) w(d0,d1); w(d0,d0-b.read16(0xffff8d66)); b.write16(0xffff9100,d0);
        b.write8(0xffff9102,0); b.write8(0xffff90ff,0xff); b.write8(0xffff9103,0xe0);
    }
    // $016CD8-$016CE8: signed byte delay; $E0 waits through zero.
    if(!b.read8(0xffff90ff)) return;
    byte(d0,b.read8(0xffff9103));
    if(static_cast<int8_t>(d0)<1) { b.write8(0xffff9103,b.read8(0xffff9103)+1); return; }
    // $016CEC-$016D1C: add one missile, original sound/HUD call order.
    w(d0,b.read16(0xffff9100));
    if(s(d0)>0) {
        b.write16(0xffff9100,b.read16(0xffff9100)-1); b.write16(0xffff8d66,b.read16(0xffff8d66)+1);
        w(d0,0x9b); OMainSound::enqueue(b,r,true); missile_hud(b,r); b.write8(0xffff9103,0);
    } else {
        b.write8(0xffff90ff,0); b.write8(0xffff9103,b.read8(0xffff9103)+1);
    }
}

void OMainWeapons::pool(MainBus& b, OMainObjects::Registers& r, bool vulcan, bool clear) {
    // $009F08/$009F44: optional removal, then ROM-selected free-slot list.
    const uint32_t table=vulcan ? 0x1c5d8 : 0x1c5d0;
    if(clear) {
        r.a[0]=b.read32(table); w(r.d[1],b.read16(table+6)); w(r.d[1],r.d[1]-1);
        do {
            b.write8(r.a[0],b.read8(r.a[0])&~0x80); r.a[0]+=64; w(r.d[1],r.d[1]-1);
        } while(uint16_t(r.d[1])!=0xffff);
    }
    // $009F22-$009F42 / $009F5E-$009F7A: first byte is count, then slots.
    if(!vulcan) b.write16(0xffff8d4a,0);
    r.a[0]=vulcan ? 0xffff8ee0 : 0xffff8e7f;
    w(r.d[0],b.read16(table+4)); w(r.d[1],b.read16(table+6));
    b.write8(r.a[0]++,r.d[1]); w(r.d[1],r.d[1]-1);
    do {
        b.write8(r.a[0]++,r.d[0]); w(r.d[0],r.d[0]+1); w(r.d[1],r.d[1]-1);
    } while(uint16_t(r.d[1])!=0xffff);
}

void OMainWeapons::initialize(MainBus& b, OMainObjects::Registers& r) {
    // $009D2C-$009D4E: initialize both pools and the weapon indicator.
    pool(b,r,false,false); pool(b,r,true,false);
    auto p=r.a[5]; b.write32(p+12,0x10a00); b.write8(p+1,0x7f); b.write8(p+2,0xff);
    b.write8(p+3,0x1d); b.write32(p+16,0x9d56); update(b,r);
}

void OMainWeapons::update(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5]; auto& d0=r.d[0]; auto& d1=r.d[1];
    // $009D56-$009D80: mode comparisons do not suppress the routine.
    if(b.read16(0x29c09c)!=0x10) (void)b.read16(0x29c09c);
    if(b.read16(0x29c09a)) { b.write32(p+16,0xfb08); return; }
    missile_fire(b,r); vulcan_fire(b,r);
    // $009D8C-$009DA6: previous position, indicator parent and active gate.
    b.write32(p+8,b.read32(p+4)); if(!(b.read8(0x29c200)&0x10)) return;
    r.a[3]=p+64; if(!(b.read8(r.a[3])&0x80)) return;
    // $009DAA-$009DCE: indicator position follows player through WORD math.
    w(d0,b.read16(r.a[3]+4)); w(d1,d0); w(d0,d0+d1); w(d0,d0+d1);
    w(d0,s(d0)>>5); w(d0,d0+0x64); b.write16(p+4,d0);
    w(d0,b.read16(r.a[3]+6)); w(d1,d0); w(d0,d0<<3); w(d0,d0-d1);
    w(d0,s(d0)>>6); w(d0,d0+0x18); b.write16(p+6,d0);
    // $009DD2-$009DEA: flashing palette and serialized sprite request.
    w(d0,0x1d); if(b.read8(0xffff8b5b)&4) w(d0,d0+1); b.write8(p+3,d0);
    OMainPlayer::append(b,r);
}

void OMainWeapons::missile_fire(MainBus& b, OMainObjects::Registers& r) {
    // $009DF8-$009E4E: edge-triggered missile, six gates and empty feedback.
    if(!(b.read8(0x29c200)&8)) return;
    if(b.read8(0xffff91d0)&8) { OMainPlayer::append(b,r); return; }
    if((b.read8(0x29c202)&1) || (b.read8(0xffff90fe)&1) || (b.read8(0x29c077)&0x20)
        || !(b.read8(0x29c076)&0x20)) return;
    b.write8(0xffff8d69,0x10); if(s(b.read16(0xffff8d66))>0) missile_allocate(b,r);
}

void OMainWeapons::vulcan_fire(MainBus& b, OMainObjects::Registers& r) {
    // $009E50-$009E80: held trigger, cadence from caller's $8B5B bit 0.
    if(!(b.read8(0x29c200)&8) || (b.read8(0xffff91d0)&8) || !(b.read8(0x29c076)&0x10)
        || !(b.read8(0xffff8b5b)&1)) return;
    vulcan_allocate(b,r);
}

void OMainWeapons::missile_allocate(MainBus& b, OMainObjects::Registers& r) {
    // $009E82-$009EA6: keep reserve slots for eight trails per active missile.
    w(r.d[1],b.read16(0xffff8d4a)); w(r.d[1],r.d[1]<<3); w(r.d[1],r.d[1]+0x18);
    r.a[1]=0xffff8e7f; w(r.d[0],0); byte(r.d[0],b.read8(r.a[1]));
    if(s(r.d[0])<=s(r.d[1])) return;
    r.a[0]=0xffffa000; byte(r.d[0],b.read8(r.a[1]+s(r.d[0])));
    b.write8(r.a[1],b.read8(r.a[1])-1); w(r.d[0],r.d[0]<<6); r.a[0]+=s(r.d[0]);
    // $009EA8-$009ED2: alternate launch channel, defer actual projectile init.
    b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write16(r.a[5]+62,b.read16(r.a[5]+62)+4);
    w(r.d[0],b.read16(r.a[5]+62)); w(r.d[0],r.d[0]&0x3c); b.write16(r.a[0]+48,r.d[0]);
    b.write32(r.a[0]+16,0x9f7c); b.write16(0x29c0b8,b.read16(0x29c0b8)+1);
    b.write16(0xffff8d4a,b.read16(0xffff8d4a)+1); b.write16(0xffff8d66,b.read16(0xffff8d66)-1);
    missile_hud(b,r);
}

void OMainWeapons::vulcan_allocate(MainBus& b, OMainObjects::Registers& r) {
    // $009ED8-$009F02: pop free slot, activate, set init handler, queue sound.
    r.a[1]=0xffff8ee0; w(r.d[0],0); byte(r.d[0],b.read8(r.a[1])); if(!uint16_t(r.d[0])) return;
    r.a[0]=0xffffa000; byte(r.d[0],b.read8(r.a[1]+s(r.d[0])));
    b.write8(r.a[1],b.read8(r.a[1])-1); w(r.d[0],r.d[0]<<6); r.a[0]+=s(r.d[0]);
    b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write32(r.a[0]+16,0xa030);
    w(r.d[0],0x90); OMainSound::enqueue(b,r,true);
}

namespace {
void asr(uint32_t& r,unsigned n) { w(r,s(r)>>n); }
void asrl(uint32_t& r,unsigned n) { r=static_cast<int32_t>(r)>>n; }
// $00A0CC-$00A12C / $00A178-$00A1D8: roll two logical coordinates.
void rotate(MainBus& b,OMainObjects::Registers& r,unsigned x,unsigned y) {
    auto& d=r.d; const auto m=r.a[6],p=r.a[5];
    w(d[0],b.read16(0x29c09e)); w(d[0],d[0]&0xff); r.a[0]=0x33e98;
    byte(d[1],b.read8(r.a[0]+s(d[0]))); byte(d[2],b.read8(r.a[0]+s(d[0])-64));
    w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2])); w(d[3],d[1]); w(d[4],d[2]);
    b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,b.read16(p+x)); d[2]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+y)); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[6]); d[1]=b.read32(m-0x3ffc);
    d[1]+=d[2]; asrl(d[1],7);
    b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,b.read16(p+x)); d[3]=b.read32(m-0x3ffc);
    b.write16(m-0x4000,d[4]); b.write16(m-0x3ffe,d[6]); d[4]=b.read32(m-0x3ffc);
    d[3]-=d[4]; asrl(d[3],7); w(d[3],d[3]+b.read16(0x29c080));
}
}

void OMainWeapons::missile_initialize(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5];
    // $009F7C-$009FAC: claim the first nonzero target from the target ring.
    b.write8(0xffff89ea,1); r.a[0]=0xffff8e30; w(d[7],b.read16(0x1c59e)); asr(d[7],1); w(d[7],d[7]-1);
    do {
        auto target=b.read32(r.a[0]); r.a[0]+=4;
        if(target) {
            r.a[0]-=4; r.a[4]=b.read32(r.a[0]); b.write32(r.a[0],0);
            b.write8(r.a[4]+22,b.read8(r.a[4]+22)|1); b.write32(p+56,r.a[4]); goto found;
        }
        w(d[7],d[7]-1);
    } while(uint16_t(d[7])!=0xffff);
    // $009F9A CLR.L writes its low word before its high word on the 68000.
    b.write16(p+58,0); b.write16(p+56,0);
found:
    // $009FB0-$009FFC: source side alternates on the actual launch counter.
    b.write8(p,b.read8(p)&~0x10); b.write16(p+62,0); b.write16(p+54,0); b.write16(p+40,0);
    d[0]=0x20; if(b.read8(0x29c0b9)&1) w(d[0],-d[0]); w(d[0],d[0]+b.read16(0xffff8d14)); b.write16(p+42,d[0]);
    b.write16(p+44,b.read16(0xffff8d16)); b.write16(p+44,b.read16(p+44)+0x40);
    w(d[0],b.read16(0xffff8d18)); w(d[0],d[0]+0x20); b.write16(p+36,d[0]);
    b.write16(p+4,b.read16(0xffff8d1a)); w(d[0],b.read16(0xffff8d1c)); b.write16(p+6,d[0]); b.write8(p+3,7);
    // $00A002-$00A02E: speed, animation timer and launch-channel sound table.
    b.write16(p+46,b.read16(0x29c0aa)); b.write16(p+46,b.read16(p+46)+0x40); b.write16(p+52,0);
    w(d[0],b.read16(p+48)); asr(d[0],2); r.a[0]=0xa716; byte(d[0],b.read8(r.a[0]+s(d[0])));
    OMainSound::enqueue(b,r,true); b.write32(p+16,0xa234);
}

void OMainWeapons::vulcan_initialize(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    // $00A030-$00A062: initial position relative to the live player.
    b.write16(p+40,0); b.write32(p+12,0x15950); b.write8(p+3,0x2f); b.write16(p+4,b.read16(0xffff8d1a));
    w(d[0],b.read16(0xffff8d1c)); w(d[1],d[0]); w(d[1],d[1]-0x68); asr(d[1],2); w(d[0],d[0]-d[1]); b.write16(p+6,d[0]);
    w(d[0],b.read16(0xffff8d18)); w(d[0],d[0]+0x20); b.write16(p+36,d[0]);
    // $00A066-$00A08C: depth divider and packed scale.
    w(d[1],b.read16(p+36)); asr(d[1],5); w(d[1],d[1]+0x20); b.write16(m+4,d[1]); b.write32(m+16,0x7fe0);
    w(d[0],b.read16(m+8)); w(d[7],d[0]); asr(d[0],1); b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[0]);
    asr(d[0],1); b.write8(p+2,d[0]);
    // $00A090-$00A0C6: invert perspective into logical X/Y.
    d[1]=10; w(d[0],b.read16(p+4)); w(d[0],d[0]-b.read16(0x29c08e)); d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63);
    b.write16(m+4,d[7]); b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+42,d[0]);
    w(d[0],b.read16(p+6)); w(d[0],d[0]+b.read16(0x29c08c)); w(d[0],d[0]-0x70); d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63);
    b.write32(m+16,d[0]); b.write16(p+44,b.read16(m+8)); rotate(b,r,42,44); b.write16(p+42,d[1]); b.write16(p+44,d[3]);
    // $00A13A-$00A172: invert shadow perspective with fixed divisor $4E.
    w(d[7],0x4e); d[1]=10; w(d[0],b.read16(0xffff8d46)); w(d[0],d[0]-b.read16(0x29c08e));
    d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63); b.write16(m+4,d[7]); b.write32(m+16,d[0]); b.write16(p+48,b.read16(m+8));
    w(d[0],b.read16(0xffff8d48)); w(d[0],d[0]+b.read16(0x29c08c)); w(d[0],d[0]-0x70);
    d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63); b.write32(m+16,d[0]); b.write16(p+50,b.read16(m+8)); rotate(b,r,48,50);
    // $00A1DE-$00A216: aim towards shadow, retaining WORD differences.
    w(d[1],d[1]-b.read16(p+42)); w(d[3],d[3]-b.read16(p+44)); d[1]=static_cast<int32_t>(s(d[1])); d[3]=static_cast<int32_t>(s(d[3])); d[1]<<=4; d[3]<<=4;
    w(d[0],0x3000); w(d[0],d[0]-b.read16(p+36)); d[0]=static_cast<int32_t>(s(d[0])); b.write32(m,d[0]); b.write16(m+20,0x200);
    b.write16(m+4,b.read16(m+8)); b.write32(m+16,d[1]); b.write16(p+48,b.read16(m+8)); b.write32(m+16,d[3]); b.write16(p+50,b.read16(m+8));
    // $00A21C-$00A232: speed and live handler.
    w(d[0],b.read16(0x29c0aa)); w(d[0],d[0]+0x200); b.write16(p+46,d[0]); b.write32(p+16,0xac4c);
}

void OMainWeapons::remove(MainBus& b, OMainObjects::Registers& r, bool missile, bool hit, bool decrement) {
    const auto p=r.a[5];
    // $00AE1C-$00AE2E: hit counters and target's pending-damage flag.
    if(hit) {
        b.write16(0x29c0ba,b.read16(0x29c0ba)+1); b.write16(0x29c0bc,b.read16(0x29c0bc)+1);
        b.write8(r.a[4]+22,b.read8(r.a[4]+22)|2);
    }
    if(missile && decrement) b.write16(0xffff8d4a,b.read16(0xffff8d4a)-1);
    // $00AE32-$00AE4E / $00AE50-$00AE6C: delete and return original slot.
    b.write16(p+36,0); b.write8(p+1,0); b.write8(p,b.read8(p)&~0x80);
    r.a[0]=missile ? 0xffff8e7f : 0xffff8ee0; b.write8(r.a[0],b.read8(r.a[0])+1);
    w(r.d[0],0); byte(r.d[0],b.read8(r.a[0])); b.write8(r.a[0]+s(r.d[0]),b.read8(p+20));
}

void OMainWeapons::vulcan_update(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    // $00AC4C-$00AC84: previous position, pause and depth deletion bounds.
    b.write32(p+8,b.read32(p+4)); if(!(b.read8(0x29c200)&0x10)) return;
    if(b.read8(0xffff91d0)&0x10) { OMainPlayer::append(b,r); return; }
    w(d[0],b.read16(p+46)); w(d[0],d[0]-b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)+d[0]);
    w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<=0 || s(d[1])>=0x180) { remove(b,r,false); return; }
    // $00AC88-$00ACD0: perspective and scale relative to the player's depth.
    w(d[1],d[1]+0x20); b.write16(m+4,d[1]); b.write32(m+16,0x7fe0); w(d[0],b.read16(m+8)); w(d[7],d[0]);
    asr(d[0],2); b.write8(p+2,d[0]); w(d[0],d[7]); w(d[1],0x3000); w(d[2],d[1]); w(d[1],d[1]-b.read16(0xffff8d18)); w(d[2],d[2]-b.read16(p+36));
    b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,d[0]); b.write32(m,b.read32(m-0x3ffc)); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); asr(d[0],1);
    b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[0]);
    // $00ACD2-$00ACF6: move logical X/Y; crossing the ground creates a splash.
    w(d[0],b.read16(p+48)); asr(d[0],4); b.write16(p+42,b.read16(p+42)+d[0]);
    w(d[0],b.read16(p+50)); asr(d[0],4); b.write16(p+44,b.read16(p+44)+d[0]);
    if(s(b.read16(p+36))<=0x3000 && s(b.read16(p+44))>=0x168) {
        // $00AD9C-$00ADE6: free world slot, environment-specific splash handler.
        if(!(b.read8(0x29c0a1)&1)) {
            r.a[1]=0xffff8d80; w(d[0],0); byte(d[0],b.read8(r.a[1]));
            if(uint16_t(d[0])) {
                r.a[0]=0xffffa000; byte(d[0],b.read8(r.a[1]+s(d[0]))); b.write8(r.a[1],b.read8(r.a[1])-1);
                w(d[0],d[0]<<6); r.a[0]+=s(d[0]); b.write8(r.a[0],b.read8(r.a[0])|0x80);
                b.write32(r.a[0]+16,(b.read8(0x29c202)&1) ? 0x8d44 : 0x8d58);
                // $00ADE6-$00AE0E: position/depth, rotating noise source and sound.
                b.write16(r.a[0]+42,b.read16(p+42)); b.write16(r.a[0]+44,0x168); b.write16(r.a[0]+36,b.read16(p+36)); b.write16(r.a[0]+40,0);
                w(d[0],b.read16(0xffff8d32)); w(d[0],(uint16_t(d[0])>>1)|(d[0]<<15)); b.write16(0xffff8d32,d[0]); w(d[0],d[0]&3);
                byte(d[0],b.read8(0xae18+s(d[0]))); OMainSound::enqueue(b,r,true);
            }
        }
        remove(b,r,false); return;
    }
    // $00ACFA-$00AD16: signed-byte roll table for forward projection.
    w(d[0],0); byte(d[0],b.read8(0x29c09f)); r.a[0]=0x33e98;
    byte(d[1],b.read8(r.a[0]+s(d[0]))); byte(d[2],b.read8(r.a[0]+s(d[0])-64));
    w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2])); w(d[3],d[1]); w(d[4],d[2]);
    // $00AD18-$00AD56: rotated X, followed by depth scale and road origin.
    b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,b.read16(p+42)); d[2]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080)); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[6]); d[1]=b.read32(m-0x3ffc);
    d[1]+=d[2]; asrl(d[1],7); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[7]); d[1]=b.read32(m-0x3ffc); asrl(d[1],8); asrl(d[1],2);
    w(d[1],d[1]+b.read16(0x29c08e)); b.write16(p+4,d[1]);
    // $00AD5A-$00AD92: rotated Y, same fixed-point depth scale.
    b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,b.read16(p+42)); d[3]=b.read32(m-0x3ffc);
    b.write16(m-0x4000,d[4]); b.write16(m-0x3ffe,d[6]); d[4]=b.read32(m-0x3ffc); d[3]-=d[4]; asrl(d[3],7);
    b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,d[7]); d[3]=b.read32(m-0x3ffc); asrl(d[3],8); asrl(d[3],2);
    w(d[3],d[3]+0x70); w(d[3],d[3]-b.read16(0x29c08c)); b.write16(p+6,d[3]); OMainPlayer::append(b,r);
}

namespace {
// $00A2CA-$00A360 / $00A43E-$00A4DE / $00A5F6-$00A696:
// forward rotation and perspective using D7 = depth >> 5.
void missile_project(MainBus& b,OMainObjects::Registers& r,bool launch) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    if(launch) { w(d[1],b.read16(0x29c090)); w(d[2],b.read16(0x29c092)); }
    else {
        w(d[0],0); byte(d[0],b.read8(0x29c09f)); r.a[0]=0x33e98;
        byte(d[1],b.read8(r.a[0]+s(d[0]))); byte(d[2],b.read8(r.a[0]+s(d[0])-64));
        w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2]));
    }
    w(d[3],d[1]); w(d[4],d[2]);
    b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,b.read16(p+42)); d[2]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080)); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[6]); d[1]=b.read32(m-0x3ffc);
    d[1]+=d[2]; asrl(d[1],7); d[1]<<=5; w(d[5],d[7]); w(d[5],d[5]+0x20);
    b.write16(m+4,d[5]); b.write32(m+16,d[1]); w(d[1],b.read16(m+8)); w(d[1],d[1]+b.read16(0x29c08a)); b.write16(p+4,d[1]);
    b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,b.read16(p+42)); d[3]=b.read32(m-0x3ffc);
    // Both ROM paths reload logical Y after the X projection.
    w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080));
    b.write16(m-0x4000,d[4]); b.write16(m-0x3ffe,d[6]); d[4]=b.read32(m-0x3ffc); d[3]-=d[4]; asrl(d[3],7); d[3]<<=5;
    if(launch) b.write16(m+4,d[5]);
    b.write32(m+16,d[3]); w(d[3],b.read16(m+8)); w(d[3],d[3]+0x70); w(d[3],d[3]-b.read16(0x29c088)); b.write16(p+6,d[3]);
}
// Select translated C++ labels using the ROM jump table's BRA displacements.
// No opcodes are interpreted or executed at runtime.
uint32_t branch(MainBus& b,uint32_t table,int16_t index) {
    uint32_t at=table+index; return at+2+static_cast<int16_t>(b.read16(at+2));
}
// $00A728-$00A7B4 / $00A882-$00A90C: the six reachable guided profiles.
void guided(MainBus& b,OMainObjects::Registers& r,uint32_t entry) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    const bool circular=entry==0xa728 || entry==0xa738 || entry==0xa748 || entry==0xa758;
    if(circular) {
        switch(entry) {
            case 0xa728: r.a[0]=0x34018; r.a[1]=0x34098; break;
            case 0xa738: r.a[0]=0x34098; r.a[1]=0x34098; break;
            case 0xa748: r.a[0]=0x33fd8; r.a[1]=0x34058; break;
            case 0xa758: r.a[0]=0x34058; r.a[1]=0x34058; break;
        }
        b.write16(p+62,b.read16(p+62)+4); w(d[0],b.read16(p+62)); w(d[0],d[0]&0xff);
        byte(d[1],b.read8(r.a[0]+s(d[0]))); w(d[1],static_cast<int8_t>(d[1])); b.write16(p+42,b.read16(p+42)+d[1]);
        byte(d[1],b.read8(r.a[1]+s(d[0])-64)); w(d[1],static_cast<int8_t>(d[1])); b.write16(p+44,b.read16(p+44)+d[1]);
    } else {
        if(entry!=0xa882 && entry!=0xa88c) throw std::runtime_error("unported guided profile");
        r.a[0]=entry==0xa882 ? 0x34118 : 0x34098;
        b.write16(p+62,b.read16(p+62)+4);
        if(s(b.read16(p+62))<=0x7f) {
            w(d[0],b.read16(p+62)); byte(d[1],b.read8(r.a[0]+s(d[0]))); w(d[1],static_cast<int8_t>(d[1])); b.write16(p+42,b.read16(p+42)+d[1]);
        }
        if(s(d[6])<=15) {
            w(d[4],d[6]); w(d[4],d[4]<<2); r.a[0]=0x34098; byte(d[4],b.read8(r.a[0]+s(d[4])+127)); w(d[4],static_cast<int8_t>(d[4]));
            w(d[4],d[4]+d[4]); w(d[4],d[4]+0x80);
            w(d[0],b.read16(r.a[4]+44)); w(d[0],d[0]-b.read16(p+44)); d[0]=static_cast<int32_t>(s(d[0]));
            b.write16(m+4,d[6]); b.write32(m+16,d[0]); b.write16(m-0x4000,b.read16(m+8)); b.write16(m-0x3ffe,d[4]); d[0]=b.read32(m-0x3ffc); asrl(d[0],7); b.write16(p+44,b.read16(p+44)+d[0]);
        }
    }
    w(d[0],b.read16(r.a[4]+42)); w(d[0],d[0]-b.read16(p+42)); d[0]=static_cast<int32_t>(s(d[0])); b.write16(m+4,d[6]); b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+42,b.read16(p+42)+d[0]);
    if(circular) {
        w(d[0],b.read16(r.a[4]+44)); w(d[0],d[0]-b.read16(p+44)); d[0]=static_cast<int32_t>(s(d[0])); b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+44,b.read16(p+44)+d[0]);
    }
}
// $00A94E-$00A9F0: untracked profiles. Resolve the ROM branch table first.
void unguided(MainBus& b,OMainObjects::Registers& r,uint32_t entry) {
    auto& d=r.d; const auto p=r.a[5];
    switch(entry) {
        case 0xa94e: r.a[0]=0x33f28; d[2]=0; break;
        case 0xa95a: r.a[0]=0x33f28; d[2]=1; break;
        case 0xa966: r.a[0]=0x33f28; d[2]=2; break;
        case 0xa972: r.a[0]=0x33f28; d[2]=3; break;
        case 0xa97e: r.a[0]=0x33f28; d[2]=4; break;
        case 0xa98a: r.a[0]=0x33ea8; d[2]=0; break;
        case 0xa996: r.a[0]=0x33ea8; d[2]=1; break;
        case 0xa9a2: r.a[0]=0x33ea8; d[2]=2; break;
        case 0xa9ae: r.a[0]=0x33ea8; d[2]=3; break;
        case 0xa9ba: r.a[0]=0x33ea8; d[2]=4; break;
        default: throw std::runtime_error("unported unguided profile");
    }
    b.write16(p+62,b.read16(p+62)+4); if(s(b.read16(p+62))>0x7f) return;
    w(d[0],b.read16(p+62)); byte(d[1],b.read8(r.a[0]+s(d[0]))); w(d[1],static_cast<int8_t>(d[1])); b.write16(p+42,b.read16(p+42)+d[1]);
    r.a[0]=0x34098; byte(d[1],b.read8(r.a[0]+s(d[0]))); w(d[1],static_cast<int8_t>(d[1])); asr(d[1],d[2]&63); b.write16(p+44,b.read16(p+44)-d[1]);
}
}

void OMainWeapons::missile_update(MainBus& b,OMainObjects::Registers& r,uint32_t entry) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    if(entry==0xa234) {
        // $00A234-$00A24E: launch lasts seven updates, then enters tracking.
        b.write16(p+62,b.read16(p+62)+1);
        if(s(b.read16(p+62))>=8) { b.write32(p+16,0xa382); b.write16(p+62,0); entry=0xa382; }
        else {
            // $00A252-$00A2C6: accelerate, perspective/packed scale and Y arc.
            b.write32(p+8,b.read32(p+4)); if(!(b.read8(0x29c200)&0x10)) return;
            if(b.read8(0xffff91d0)&0x10) { OMainPlayer::append(b,r); return; }
            b.write16(p+46,b.read16(p+46)+0x20); w(d[0],b.read16(p+46)); w(d[0],d[0]-b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)+d[0]);
            w(d[7],b.read16(p+36)); asr(d[7],5); w(d[1],d[7]); w(d[1],d[1]+0x20); b.write16(m+4,d[1]); b.write32(m+16,0x3fe0); w(d[0],b.read16(m+8));
            b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[0]); asr(d[0],1); b.write8(p+2,d[0]);
            r.a[0]=0x33e98; w(d[0],b.read16(p+62)); w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]);
            byte(d[0],b.read8(r.a[0]+s(d[0])-64)); w(d[0],static_cast<int8_t>(d[0])); w(d[0],d[0]+0x80); asr(d[0],2); b.write16(p+44,b.read16(p+44)+d[0]);
            missile_project(b,r,true); w(d[0],b.read16(p+62)); w(d[0],d[0]<<3); r.a[0]=0x28c24;
            b.write32(p+12,b.read32(r.a[0]+s(d[0]))); b.write8(p+3,b.read8(r.a[0]+s(d[0])+4)); OMainPlayer::append(b,r); return;
        }
    }
    // $00A382-$00A39A / $00A53A-$00A552: shared pause/draw gates.
    b.write32(p+8,b.read32(p+4)); if(!(b.read8(0x29c200)&0x10)) return;
    if(b.read8(0xffff91d0)&0x10) { OMainPlayer::append(b,r); return; }
    if(entry==0xa382) {
        // $00A39E-$00A3BC: missing/released target switches to free flight.
        r.a[4]=b.read32(p+56);
        if(!(b.read8(r.a[4])&0x80) || !(b.read8(r.a[4]+22)&1)) { b.write32(p+16,0xa53a); missile_update(b,r,0xa53a); return; }
    }
    // $00A3C0-$00A3DA / $00A556-$00A570: accelerate by $30 through $800.
    if(s(b.read16(p+46))<=0x800) b.write16(p+46,b.read16(p+46)+0x30);
    w(d[0],b.read16(p+46)); w(d[0],d[0]-b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)+d[0]);
    if(entry==0xa382) {
        // $00A3DE-$00A434: detect passing target, perspective and guided step.
        d[6]=0; w(d[6],b.read16(r.a[4]+36)); const int32_t target_distance=int32_t(s(d[6]))-s(b.read16(p+36)); w(d[6],target_distance); if(target_distance<=0) { remove(b,r,true,true); return; }
        w(d[7],b.read16(p+36)); asr(d[7],5); w(d[1],d[7]); w(d[1],d[1]+0x20); b.write16(m+4,d[1]); b.write32(m+16,0x1fe0); w(d[0],b.read16(m+8));
        b.write8(p+1,d[0]); b.write8(p+2,d[0]); w(d[3],b.read16(p+46)); w(d[3],d[3]-b.read16(0x29c0aa));
        if(s(d[3])>0) {
            b.write16(m+4,d[3]); b.write32(m+16,d[6]); w(d[6],b.read16(m+8)); w(d[0],b.read16(p+48)); r.a[0]=0xa6d6; guided(b,r,branch(b,r.a[0],s(d[0])));
        }
        w(d[7],b.read16(p+36)); asr(d[7],5); missile_project(b,r,false);
        // $00A4E2-$00A500: flip from target direction and road roll.
        b.write8(p,b.read8(p)&~0x20); w(d[0],b.read16(0x29c09e)); w(d[0],d[0]+b.read16(r.a[4]+40)); w(d[0],d[0]&0xff);
        if(!(d[0]&0x80)) b.write8(p,b.read8(p)|0x20);
    } else {
        // $00A574-$00A5AE: free-flight bounds and profile selection.
        w(d[7],b.read16(p+36)); asr(d[7],5); w(d[1],d[7]);
        if(s(d[1])<=0 || s(d[1])>=0x1ff) { remove(b,r,true); return; }
        w(d[1],d[1]+0x20); b.write16(m+4,d[1]); b.write32(m+16,0x1fe0); w(d[0],b.read16(m+8)); b.write8(p+1,d[0]); b.write8(p+2,d[0]);
        w(d[0],b.read16(p+48)); r.a[0]=0xa90e; unguided(b,r,branch(b,r.a[0],s(d[0])));
        // $00A5B2-$00A5F2: road-curve drift and late outward drift.
        r.a[0]=0x33e98; w(d[1],d[7]); asr(d[1],4); byte(d[1],b.read8(r.a[0]+s(d[1]))); w(d[1],static_cast<int8_t>(d[1]));
        b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,b.read16(0x29c0c4)); d[1]=b.read32(m-0x3ffc); asrl(d[1],6); b.write16(p+42,b.read16(p+42)+d[1]);
        w(d[1],d[7]); w(d[1],d[1]-0x100);
        if(s(d[1])>0) { asr(d[1],2); byte(d[1],b.read8(r.a[0]+s(d[1]))); if(s(b.read16(p+42))<0) w(d[1],-d[1]); b.write16(p+42,b.read16(p+42)+d[1]); }
        missile_project(b,r,false);
    }
    // $00A500-$00A534 / $00A69A-$00A6D0: emit missile, then distance-driven trail.
    b.write32(p+12,0x1df00); OMainPlayer::append(b,r); d[0]=0; w(d[0],b.read16(0x29c0ac)); w(d[0],d[0]+b.read16(p+46));
    if(entry==0xa53a) w(d[0],d[0]+d[0]);
    w(d[0],d[0]+b.read16(p+54)); b.write32(m,d[0]); b.write16(m+20,0x600); b.write16(p+54,b.read16(m+10));
    if(b.read16(m+8)) trail_allocate(b,r);
}

void OMainWeapons::trail_allocate(MainBus& b,OMainObjects::Registers& r) {
    // $00A9F2-$00AA2C: shared missile pool, logical position and ROM frame.
    r.a[1]=0xffff8e7f; w(r.d[0],0); byte(r.d[0],b.read8(r.a[1])); if(!uint16_t(r.d[0])) return;
    r.a[0]=0xffffa000; byte(r.d[0],b.read8(r.a[1]+s(r.d[0]))); b.write8(r.a[1],b.read8(r.a[1])-1);
    w(r.d[0],r.d[0]<<6); r.a[0]+=s(r.d[0]); b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write32(r.a[0]+16,0xaa32);
    b.write32(r.a[0]+42,b.read32(r.a[5]+42)); b.write16(r.a[0]+36,b.read16(r.a[5]+36)); b.write32(r.a[0]+12,b.read32(0x28c64)); b.write16(r.a[5]+52,0);
}

namespace {
// $00AA4C-$00AA9A / $00AB6E-$00ABB8: trail forward rotation/perspective X.
void trail_x(MainBus& b,OMainObjects::Registers& r,bool initial) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42)); d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080)); b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c090)); d[6]=b.read32(m-0x3ffc);
    d[0]+=d[6]; asrl(d[0],7); d[0]<<=5; w(d[2],initial ? b.read16(p+36) : d[7]); if(initial) asr(d[2],5); w(d[2],d[2]+0x20);
    b.write16(m+4,d[2]); b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); w(d[0],d[0]+b.read16(0x29c08a)); b.write16(p+4,d[0]);
}
// $00AA9E-$00AAE2 / $00ABD6-$00AC1A: trail Y reuses perspective divisor.
void trail_y(MainBus& b,OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42)); d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080)); b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c092)); d[6]=b.read32(m-0x3ffc);
    d[0]-=d[6]; asrl(d[0],7); d[0]<<=5; b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); w(d[0],d[0]+0x70); w(d[0],d[0]-b.read16(0x29c088)); b.write16(p+6,d[0]);
}
}

void OMainWeapons::trail(MainBus& b,OMainObjects::Registers& r,bool initial) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    if(initial) {
        // $00AA32-$00AAEA: setup projects once before the normal update.
        b.write16(p+40,0); b.write8(p+3,0xe9); b.write32(p+16,0xaaee); b.write8(p,b.read8(p)&~4); b.write8(p,b.read8(p)&~8);
        trail_x(b,r,true); trail_y(b,r); b.write16(p+50,0); b.write16(p+52,0);
    }
    // $00AAEE-$00AB1E: previous position and depth/scale. Trail removal enters
    // $AE32, below the active-missile counter decrement at $AE2E.
    b.write32(p+8,b.read32(p+4)); w(d[1],b.read16(0x29c0ac)); b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[7],b.read16(p+36)); asr(d[7],5); w(d[1],d[7]);
    if(s(d[1])<0) { remove(b,r,true,false,false); return; }
    w(d[1],d[1]+0x20); b.write16(m+4,d[1]); b.write32(m+16,0x1fe0); w(d[0],b.read16(m+8)); b.write8(p+2,d[0]);
    // $00AB22-$00AB44: growing trail width for its first eight updates.
    if(s(b.read16(p+50))<=7) {
        b.write16(p+50,b.read16(p+50)+1); w(d[1],b.read16(p+50)); w(d[1],d[1]+8);
        b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[0]); d[0]=b.read32(m-0x3ffc); asr(d[0],4);
    }
    b.write8(p+1,d[0]);
    // $00AB48-$00AB6A: curve drift on the logical position.
    w(d[6],d[7]); asr(d[6],2); r.a[0]=0x33e98; byte(d[6],b.read8(r.a[0]+s(d[6]))); w(d[6],static_cast<int8_t>(d[6]));
    b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c0c4)); d[6]=b.read32(m-0x3ffc); asrl(d[6],6); b.write16(p+42,b.read16(p+42)+d[6]);
    trail_x(b,r,false);
    // $00ABBC-$00ABD2: scaled horizontal clipping returns the slot.
    w(d[1],0); byte(d[1],b.read8(p+1)); asr(d[1],2); w(d[1],d[1]+0xa0);
    if(s(d[0])>s(d[1])) { remove(b,r,true,false,false); return; }
    w(d[1],-d[1]); if(s(d[0])<=s(d[1])) { remove(b,r,true,false,false); return; }
    trail_y(b,r);
    // $00AC1E-$00AC46: top clipping hides, bottom clipping removes.
    w(d[1],0); byte(d[1],b.read8(p+1)); asr(d[1],1); w(d[1],-d[1]); if(s(d[0])<=s(d[1])) return;
    w(d[1],-d[1]); w(d[1],d[1]+0xe0); if(s(d[0])>s(d[1])) { remove(b,r,true,false,false); return; }
    w(d[0],b.read16(p+50)); w(d[0],d[0]+0xe8); b.write8(p+3,d[0]); OMainPlayer::append(b,r);
}

void OMainWeapons::enemy(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    auto& d=r.d; auto& a=r.a; const auto p=a[5],m=a[6];
    auto ext=[&](unsigned n) { d[n]=static_cast<int32_t>(s(d[n])); };
    auto swap=[&](unsigned n) { d[n]=(d[n]<<16)|(d[n]>>16); };
    std::function<void(uint32_t)> call=[&](uint32_t e) { enemy(b,r,e); };
    // $014FF2/$014FF4: return an active record to the projectile pool.
    if(entry==0x14ff2 || entry==0x14ff4) {
        if(entry==0x14ff2) a[2]=p;
        if(static_cast<int8_t>(b.read8(a[2]))>=0) return;
        b.write16(a[2],0); a[0]=0xffff8e7f; d[0]=0; byte(d[0],b.read8(a[0]));
        b.write8(a[0],b.read8(a[0])+1); b.write8(a[0]+s(d[0])+1,b.read8(a[2]+20)); return;
    }
    // $01528A/$0152A8: clear ownership and remove the paired shadow.
    if(entry==0x1528a || entry==0x152a8) {
        bool owner=true;
        if(entry==0x1528a && (b.read8(p+45)&8)) {
            d[0]=0; byte(d[0],b.read8(p+39)); a[0]=0xffff9162; a[0]+=s(d[0]);
            byte(d[0],b.read8(p+48)); b.write8(a[0],b.read8(a[0])&~(1u<<(d[0]&7)));
            owner=!b.read8(a[0]);
        }
        if(owner) { byte(d[0],b.read8(p+38)); b.write8(0xffff9160,b.read8(0xffff9160)&~(1u<<(d[0]&7))); }
        call(0x14ff2); a[2]=static_cast<int32_t>(s(b.read16(p+62))); call(0x14ff4); return;
    }
    // $014DC4-$014DEE: player-relative target; decoy side offset.
    if(entry==0x14dc4) {
        w(d[2],b.read16(0xffff8d14)); w(d[0],b.read16(0xffff8d16));
        if(b.read8(p+45)&0x40) { w(d[1],0x60); if(s(b.read16(p+24))<0) w(d[1],-d[1]); w(d[2],d[2]+d[1]); w(d[0],d[0]-0x28); }
        b.write16(p+48,d[2]); b.write16(p+50,d[0]); return;
    }
    // $01578C/$0157BA: perspective and packed scale.
    if(entry==0x1578c || entry==0x157ba) {
        w(d[1],b.read16(p+36)); w(d[1],uint16_t(d[1])>>5); w(d[1],d[1]+0x20); d[0]=0x7fe0;
        b.write32(m,d[0]); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]);
        w(d[0],uint16_t(d[0])>>(entry==0x1578c ? 1 : 2)); b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[0]);
        w(d[0],uint16_t(d[0])>>1); b.write8(p+2,d[0]); return;
    }
    // $0157E8/$0157F2-$015818: 16.16 coordinates with word velocities.
    if(entry==0x157e8 || entry==0x157f2) {
        if(entry==0x157f2) { w(d[0],b.read16(0x29c0aa)); w(d[0],d[0]+b.read16(p+46)); }
        else w(d[0],b.read16(p+46));
        b.write16(p+36,b.read16(p+36)-d[0]); w(d[0],b.read16(p+28)); ext(0); d[0]<<=8;
        // ADD.L to memory is a read/modify/write: low word is written first.
        const uint32_t x=b.read32(p+24)+d[0]; b.write16(p+26,x); b.write16(p+24,x>>16);
        w(d[1],b.read16(p+34)); ext(1); d[1]<<=8;
        const uint32_t y=b.read32(p+30)+d[1]; b.write16(p+32,y); b.write16(p+30,y>>16); return;
    }
    // $01581A-$01583C: unsigned clipping, including wrap around.
    if(entry==0x1581a) {
        w(d[0],b.read16(p+4)); w(d[0],d[0]+0xaa);
        if(uint16_t(d[0])<0x154) { w(d[0],b.read16(p+6)); w(d[0],d[0]+10); if(uint16_t(d[0])<0xf4) { d[0]=0; return; } }
        d[0]=1; return;
    }
    // $01583E/$015842-$01586A: road-roll-dependent sway.
    if(entry==0x1583e || entry==0x15842) {
        d[1]=entry==0x1583e ? 7 : 6; w(d[0],d[7]); asr(d[0],4); a[0]=0x33e98;
        byte(d[0],b.read8(a[0]+s(d[0]))); w(d[0],static_cast<int8_t>(d[0]));
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,b.read16(0x29c0c4)); d[0]=b.read32(m-0x3ffc);
        asrl(d[0],d[1]&63); b.write16(p+24,b.read16(p+24)+d[0]); return;
    }
    // $015200-$015288: chip projection rotated around road horizon.
    if(entry==0x15200) {
        w(d[4],b.read16(0x29c08a)); w(d[5],b.read16(0x29c088)); d[0]=0; byte(d[0],b.read8(0x29c09f)); a[0]=0x33e98;
        byte(d[1],b.read8(a[0]+s(d[0]))); byte(d[2],b.read8(a[0]+s(d[0])-64));
        w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2]));
        b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,b.read16(p+24)); d[0]=b.read32(m-0x3ffc);
        b.write16(m-0x4000,d[2]); d[3]=b.read32(m-0x3ffc); w(d[2],b.read16(p+30)); w(d[2],d[2]-b.read16(0x29c080));
        b.write16(m-0x3ffe,d[2]); d[2]=b.read32(m-0x3ffc); b.write16(m-0x4000,d[1]); d[3]+=b.read32(m-0x3ffc); asrl(d[3],7);
        b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,d[7]); d[3]=b.read32(m-0x3ffc); d[3]<<=6; swap(3);
        w(d[3],d[3]+d[4]); b.write16(p+4,d[3]); d[0]-=d[2]; asrl(d[0],7);
        b.write16(m-0x4000,d[0]); d[2]=b.read32(m-0x3ffc); d[2]<<=6; swap(2);
        w(d[2],d[2]+0x70); w(d[2],d[2]-d[5]); b.write16(p+6,d[2]); return;
    }
    // $01518A-$0151B6: initial projection and previous screen position.
    if(entry==0x1518a) {
        b.write16(p,0x8000); b.write16(p+22,0); w(d[7],b.read16(p+36)); w(d[7],uint16_t(d[7])>>5); w(d[7],d[7]+0x20);
        b.write32(m,0x7fe0); b.write16(m+20,d[7]); w(d[7],b.read16(m+8)); call(0x15200); b.write32(p+8,b.read32(p+4)); return;
    }
    // $0151B8-$0151FE: altitude and roll select runtime frame tables.
    if(entry==0x151b8) {
        w(d[1],0); w(d[2],b.read16(p+30)); a[4]=0x152d0;
        while(true) { auto t=b.read16(a[4]); a[4]+=2; if(s(d[2])>=s(t)) break; w(d[1],d[1]+2); }
        a[0]=0x152d8; a[0]+=s(b.read16(a[0]+s(d[1]))); d[0]=0; byte(d[0],b.read8(0x29c09f)); w(d[1],d[0]);
        byte(d[2],b.read8(p)); byte(d[2],d[2]&0xcf); w(d[1],d[1]&0xc0); byte(d[1],(uint8_t(d[1])<<2)|(uint8_t(d[1])>>6));
        a[1]=0x152bc; byte(d[2],d[2]|b.read8(a[1]+s(d[1]))); b.write8(p,d[2]); w(d[0],d[0]&0x78); asr(d[0],1);
        b.write32(p+12,b.read32(a[0]+s(d[0]))); return;
    }
    // $0150C0-$015188: sweep across the player's depth and scaled bounds.
    if(entry==0x150c0) {
        w(d[6],0); if((b.read8(p+45)&0x40) || b.read16(0x29c09a) || (b.read8(0xffff90fe)&1)) return;
        a[0]=b.read32(0x1c5c0); w(d[0],b.read16(p+36)); if(s(d[0])>s(b.read16(0xffff8d18))) return;
        w(d[2],b.read16(0xffff8d18)); w(d[1],d[0]); w(d[1],d[1]+b.read16(0x29c0aa)); w(d[1],d[1]+b.read16(p+46)); if(s(d[1])<s(d[2])) return;
        w(d[2],d[2]-d[0]); b.write16(m-0x4000,d[2]);
        w(d[0],b.read16(p+4)); w(d[2],d[0]); w(d[0],d[0]-b.read16(p+8)); w(d[0],d[0]<<8); b.write16(m-0x3ffe,d[0]);
        b.write32(m,b.read32(m-0x3ffc)); w(d[0],b.read16(0x29c0aa)); w(d[0],d[0]+b.read16(p+46)); b.write16(m+20,d[0]);
        byte(d[0],b.read8(m+8)); w(d[0],static_cast<int8_t>(d[0])); w(d[2],d[2]-d[0]);
        if(s(d[2])<s(b.read16(0xffff8d20)) || s(d[2])>s(b.read16(0xffff8d22))) return;
        w(d[0],b.read16(p+6)); w(d[2],d[0]); w(d[0],d[0]-b.read16(p+10)); w(d[0],d[0]<<8); b.write16(m-0x3ffe,d[0]);
        b.write32(m+16,b.read32(m-0x3ffc)); byte(d[0],b.read8(m+8)); w(d[0],static_cast<int8_t>(d[0])); w(d[2],d[2]-d[0]);
        if(s(d[2])<s(b.read16(0xffff8d24)) || s(d[2])>s(b.read16(0xffff8d26))) return;
        d[6]=1; b.write32(0xffff9136,b.read32(p+4)); b.write8(0xffff8d60,b.read8(0xffff8d60)|d[6]); b.write8(0xffff9134,b.read8(0xffff9134)|d[6]); return;
    }
    // $015750-$015788: return-address discard is an explicit C++ early return.
    auto gate=[&]() {
        b.write32(p+8,b.read32(p+4));
        if(!(b.read8(0x29c200)&2)) { call((b.read8(p+45)&8) ? 0x1528a : 0x152a8); return false; }
        if(b.read8(0xffff91d0)&2) { OMainPlayer::append(b,r); return false; }
        const auto age=b.read8(p+44); b.write8(p+44,age+1); if(age==255) b.write8(p+44,b.read8(p+44)|0x80); return true;
    };
    if(entry==0x15750) { gate(); return; }
    // $01500C-$0150BE: paired missile shadow follows the original parent.
    if(entry==0x1500c) {
        call(0x1518a); b.write8(p+3,0x2c); b.write32(p+12,0x19040); b.write32(p+16,0x15026); entry=0x15026;
    }
    if(entry==0x15026) {
        a[2]=static_cast<int32_t>(s(b.read16(p+62)));
        if(static_cast<int8_t>(b.read8(a[2]))>=0) { call(0x14ff2); return; }
        if(b.read8(a[2]+44)<12) return;
        b.write32(p+8,b.read32(p+4));
        if(!(b.read8(0xffff91d0)&2)) {
            w(d[0],b.read16(a[2]+24)); w(d[1],d[0]); asr(d[0],6); w(d[1],d[1]+d[0]); b.write16(p+24,d[1]);
            w(d[0],b.read16(a[2]+30)); w(d[1],d[0]); asr(d[0],6); w(d[1],d[1]+d[0]); b.write16(p+30,d[1]);
            w(d[1],b.read16(a[2]+36)); w(d[1],d[1]+0x100); w(d[1],uint16_t(d[1])>>5); w(d[1],d[1]+0x20);
            b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]); asr(d[0],2);
            w(d[1],d[0]); w(d[1],d[1]-1); b.write8(p+2,d[1]); byte(d[1],b.read8(a[2]+44));
            if(uint8_t(d[1])<0x20) asr(d[0],2);
            else { b.write32(p+12,0x18ff0); asr(d[0],3); w(d[2],d[0]); asr(d[2],1); w(d[0],d[0]+d[2]); }
            w(d[1],d[1]&3); w(d[0],d[0]+d[1]); b.write8(p+1,d[0]); call(0x15200);
        }
        OMainPlayer::append(b,r); return;
    }
    // $014EC8-$014FF2: enemy trail animation, unlinking its ring on removal.
    if(entry==0x14ec8) {
        call(0x1518a); b.write16(p+44,0); b.write32(p+12,0x164b0); b.write32(p+16,0x14ee2); return;
    }
    if(entry==0x14ee2) {
        b.write32(p+8,b.read32(p+4)); if(!(b.read8(0x29c200)&2)) { call(0x14ff2); return; }
        bool remove=false;
        if(!(b.read8(0xffff91d0)&2)) {
            b.write16(p+44,b.read16(p+44)+1);
            if(s(b.read16(p+44))>=0x24) remove=true;
            else {
                w(d[1],b.read16(0x29c0aa)); w(d[1],d[1]+b.read16(p+46)); b.write16(p+36,b.read16(p+36)-d[1]);
                if(b.read16(p+36)>=0x80e0) remove=true;
                else {
                    w(d[1],b.read16(p+36)); w(d[1],uint16_t(d[1])>>5);
                    if(uint16_t(d[1])>=0x3ff) remove=true;
                    else {
                        w(d[1],d[1]+0x20); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]); asr(d[0],2);
                        w(d[1],d[0]); w(d[1],d[1]-4); if(s(d[1])<=0) d[1]=1; b.write8(p+2,d[1]); w(d[1],b.read16(p+44));
                        bool grow=s(d[1])<13;
                        if(s(d[1])>=30) { w(d[1],d[1]-0x24); w(d[1],-d[1]); w(d[1],uint16_t(d[1])>>1); grow=true; }
                        if(grow) { w(d[1],d[1]+4); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[0]); d[0]=b.read32(m-0x3ffc); asrl(d[0],4); }
                        asr(d[0],1); w(d[1],d[0]); asr(d[1],2); w(d[0],d[0]+d[1]); b.write8(p+1,d[0]); call(0x15842); call(0x15200);
                        w(d[0],b.read16(p+4)); if(s(d[0])<0) w(d[0],-d[0]); w(d[1],0); byte(d[1],b.read8(p+1)); asr(d[1],1); w(d[1],d[1]+0xb4);
                        if(s(d[0])>=s(d[1])) remove=true;
                        else { w(d[0],b.read16(p+44)); if(s(d[0])>15) d[0]=15; w(d[0],uint16_t(d[0])>>1); byte(d[0],d[0]+0xf5); b.write8(p+3,d[0]); }
                    }
                }
            }
        }
        if(!remove) { OMainPlayer::append(b,r); return; }
        // $014FC8-$014FEE: remove oldest ring element unless already replaced.
        d[2]=0; byte(d[2],b.read8(p+39)); a[0]=0xffff9162; a[0]+=s(d[2]); byte(d[2],b.read8(p+38));
        b.write8(a[0],b.read8(a[0])-1); ++a[0]; byte(d[0],b.read8(a[0])); byte(d[0],d[0]-1); w(d[0],d[0]&7); b.write8(a[0],d[0]);
        byte(d[1],b.read8(a[0]+s(d[0])+1)); if(uint8_t(d[1])!=b.read8(p+20)) b.write8(a[0]+s(d[2])+1,d[1]); call(0x14ff2); return;
    }
    // $014E14/$014E1E/$014DF0-$014EC6: recycle the oldest trail then allocate.
    if(entry==0x14df0 || entry==0x14e14 || entry==0x14e1e) {
        w(d[1],entry==0x14e14 ? 0x1000 : 0x320);
        w(d[0],b.read16(p+(entry==0x14e14 ? 34 : 46))); if(entry!=0x14e14) w(d[0],d[0]+b.read16(0x29c0aa));
        b.write16(p+52,b.read16(p+52)+d[0]); if(s(d[1])>s(b.read16(p+52))) return;
        b.write16(p+52,b.read16(p+52)-d[1]); d[3]=entry==0x14df0 ? 3 : 8; d[2]=0; byte(d[2],b.read8(p+39)); a[1]=0xffff9162; a[1]+=s(d[2]);
        if(static_cast<int8_t>(d[3])<=static_cast<int8_t>(b.read8(a[1]))) {
            d[0]=0; byte(d[0],b.read8(a[1]+1)); byte(d[0],b.read8(a[1]+s(d[0])+2)); a[2]=0xffffa000; w(d[0],d[0]<<6); a[2]+=s(d[0]);
            call(0x14ff4); b.write8(a[1],b.read8(a[1])-1);
        }
        a[0]=0xffff8e7f; d[0]=0; byte(d[0],b.read8(a[0])); if(!uint16_t(d[0])) return;
        byte(d[0],b.read8(a[0]+s(d[0]))); b.write8(a[0],b.read8(a[0])-1); a[0]=0xffffa000; byte(d[1],d[0]); w(d[0],d[0]<<6); a[0]+=s(d[0]);
        b.write8(a[1],b.read8(a[1])+1); ++a[1]; d[0]=0; byte(d[0],b.read8(a[1])); b.write8(a[1]+s(d[0])+1,d[1]); b.write8(a[1],b.read8(a[1])+1);
        if(static_cast<int8_t>(d[3])<=static_cast<int8_t>(b.read8(a[1]))) b.write8(a[1],0);
        b.write16(a[0],0x8000); b.write32(a[0]+16,0x14ec8); w(d[0],b.read16(p+36)); w(d[0],d[0]+0x100); b.write16(a[0]+36,d[0]);
        b.write16(a[0]+24,b.read16(p+24)); b.write16(a[0]+30,b.read16(p+30)); w(d[0],b.read16(p+46)); w(d[0],-d[0]); asr(d[0],2); b.write16(a[0]+46,d[0]); return;
    }
    // $0153E0-$0154B6: initialize a paired missile, then enter its update.
    if(entry==0x153e0) {
        b.write16(p,0x8000); b.write16(p+22,0); b.write8(p+3,0xb2); a[2]=static_cast<int32_t>(s(b.read16(p+62)));
        b.write16(a[2],0x8000); b.write32(a[2]+16,0x1500c); d[0]=b.read32(p+4); b.write32(p+8,d[0]); b.write32(a[2]+8,d[0]);
        b.write16(p+26,0); b.write16(p+32,0); w(d[0],b.read16(p+36)); w(d[0],d[0]-0x1fe0);
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,0x70); b.write32(m,b.read32(m-0x3ffc)); b.write16(m+20,0x3000);
        w(d[0],b.read16(m+8)); w(d[0],d[0]-b.read16(p+30)); w(d[1],d[0]); w(d[0],d[0]<<4); b.write16(p+34,d[0]);
        w(d[1],-d[1]); asr(d[1],1); b.write16(p+40,d[1]); b.write16(p+52,0x3000); b.write32(p+16,0x154b8); b.write16(p+60,0);
        if(!(b.read8(p+45)&0x40)) { entry=0x154d8; }
        else {
            call(0x14dc4); d[1]=0; w(d[1],b.read16(p+36)); w(d[1],d[1]-b.read16(0xffff8d18)); b.write32(m,d[1]);
            w(d[0],b.read16(0x29c0aa)); w(d[0],d[0]+b.read16(p+46)); b.write16(m+20,d[0]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1);
            b.write16(m+4,d[1]); b.write16(m-0x4000,d[1]); OMainObjects::random(b,r); w(d[0],d[0]&0xfff); w(d[0],d[0]+0x3800);
            w(d[1],b.read16(p+24)); w(d[1],d[1]-b.read16(p+48)); if(s(d[1])<0) w(d[0],-d[0]); ext(0); b.write32(m+16,d[0]); b.write16(p+60,b.read16(m+8)); entry=0x15506;
        }
    }
    // $0154B8-$015550: approach, target update, then climb.
    if(entry==0x154b8) {
        w(d[0],b.read16(p+36)); if(uint16_t(d[0])<0x2aa0) entry=0x15572;
        else { if(s(d[0])<=s(b.read16(0xffff89d6))) { b.write32(p+16,0x1552c); b.write8(p+45,b.read8(p+45)|0x20); } entry=0x154d8; }
    }
    if(entry==0x154d8) {
        call(0x14dc4); d[1]=0; w(d[1],b.read16(p+36)); w(d[1],d[1]-b.read16(0xffff8d18)); b.write32(m,d[1]);
        w(d[0],b.read16(0x29c0aa)); w(d[0],d[0]+b.read16(p+46)); b.write16(m+20,d[0]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1);
        b.write16(m+4,d[1]); b.write16(m-0x4000,d[1]); entry=0x15506;
    }
    if(entry==0x15506) {
        b.write16(m-0x3ffe,b.read16(0x29c0c4)); w(d[0],b.read16(m-0x3ffa)); w(d[0],-d[0]); asr(d[0],4);
        w(d[0],d[0]+b.read16(p+48)); w(d[0],d[0]-b.read16(p+24)); ext(0); d[0]<<=8; b.write32(m+16,d[0]); b.write16(p+28,b.read16(m+8)); entry=0x1552c;
    }
    if(entry==0x1552c) {
        if(b.read8(p+44)==0x20 || b.read16(p+36)<0x2aa0) entry=0x15572;
        else {
            if(!gate()) return; call(0x157f2); w(d[0],b.read16(p+40)); b.write16(p+34,b.read16(p+34)+d[0]);
            call(0x1578c); call(0x15842); call(0x15200); call(0x1581a);
            if(d[0]) { call(0x152a8); return; } call(0x151b8); call(0x14e14); OMainPlayer::append(b,r); return;
        }
    }
    // $015572-$015632: curve down, accounting for the roll during interception.
    if(entry==0x15572) { b.write16(p+52,0x12c); b.write32(p+16,0x15582); entry=0x155aa; }
    if(entry==0x15582) {
        w(d[0],b.read16(p+36)); if(uint16_t(d[0])<0x1ff8) entry=0x15678;
        else {
            b.write8(p+45,b.read8(p+45)&~0x10);
            if(s(d[0])<=s(b.read16(0xffff89d6))) { b.write32(p+16,0x15638); entry=0x15638; }
            else { call(0x14dc4); entry=0x155aa; }
        }
    }
    if(entry==0x155aa) {
        d[1]=0; w(d[1],b.read16(p+36)); w(d[1],d[1]-b.read16(0xffff8d18)); b.write32(m,d[1]); w(d[2],b.read16(0x29c0aa)); w(d[2],d[2]+b.read16(p+46));
        b.write16(m+20,d[2]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1); b.write16(m+4,d[1]); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,b.read16(0x29c0c4));
        w(d[0],b.read16(m-0x3ffa)); w(d[0],-d[0]); b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,0x3000); w(d[0],b.read16(m-0x3ffc));
        w(d[0],d[0]+b.read16(p+48)); w(d[0],d[0]-b.read16(p+24)); ext(0); d[0]<<=8; b.write32(m+16,d[0]); b.write16(p+28,b.read16(m+8));
        d[1]=0; w(d[1],b.read16(p+36)); w(d[1],d[1]-0x1fe0); b.write32(m,d[1]); b.write16(m+20,d[2]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1); b.write16(m+4,d[1]);
        w(d[0],0xbe); w(d[0],d[0]-b.read16(p+30)); ext(0); d[0]<<=8; b.write32(m+16,d[0]); b.write16(p+34,b.read16(m+8)); entry=0x15638;
    }
    if(entry==0x15638) {
        if(!gate()) return; call(0x157f2); w(d[0],b.read16(p+60)); b.write16(p+28,b.read16(p+28)+d[0]);
        if(b.read16(p+36)<0x1ff8) entry=0x15678;
        else {
            call(0x1578c); call(0x15200); call(0x150c0); if(uint8_t(d[6])) { call(0x152a8); return; }
            call(0x1581a); if(d[0]) { call(0x152a8); return; } call(0x151b8); call(0x14e1e); OMainPlayer::append(b,r); return;
        }
    }
    // $015678-$0156EC: accelerate Y beyond the player's depth; retain overflow.
    if(entry==0x15678) {
        d[1]=0; w(d[1],b.read16(p+36)); w(d[1],d[1]-b.read16(0xffff8d18)); b.write32(m,d[1]);
        w(d[0],b.read16(0x29c0aa)); w(d[0],d[0]+b.read16(p+46)); b.write16(m+20,d[0]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1); b.write16(m+4,d[1]);
        w(d[0],b.read16(p+50)); w(d[0],d[0]-b.read16(p+30)); ext(0); d[0]<<=8; b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); ext(0); d[0]+=d[0];
        b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+40,d[0]); b.write16(p+34,d[0]); b.write32(p+16,0x156ce); entry=0x156ce;
    }
    if(entry==0x156ce) {
        if(!gate()) return; call(0x157f2); w(d[0],b.read16(p+40)); const auto old=b.read16(p+34); const int32_t sum=int32_t(s(old))+s(d[0]);
        b.write16(p+34,old+d[0]); if(sum<-32768 || sum>32767) b.write16(p+34,b.read16(p+34)-d[0]);
        w(d[0],b.read16(p+60)); b.write16(p+28,b.read16(p+28)+d[0]); w(d[0],b.read16(p+36)); if(uint16_t(d[0])>=0x80e0) { call(0x152a8); return; }
        // $0156F8-$01571C: one-shot passing sound, original command side.
        w(d[0],d[0]-0x1fe0);
        if(uint16_t(d[0])<b.read16(0xffff8d18)) {
            const auto flags=b.read8(p+45); b.write8(p+45,flags|0x80);
            if(!(flags&0x80)) { w(d[0],0x6e); if(s(b.read16(p+4))>=0) w(d[0],d[0]+1); OMainSound::enqueue(b,r,true); }
        }
        call(0x1578c); if(!(b.read8(p+45)&0x40)) call(0x1583e); call(0x15200); call(0x150c0);
        if(uint8_t(d[6])) { call(0x152a8); return; } call(0x1581a); if(d[0]) { call(0x152a8); return; }
        call(0x151b8); call(0x14e1e); OMainPlayer::append(b,r); return;
    }
    // $01586C-$015926: tracking missile and paired shadow initializer.
    if(entry==0x1586c) {
        b.write16(p,0x8000); b.write8(p+3,0xb2); a[2]=static_cast<int32_t>(s(b.read16(p+62)));
        b.write16(a[2],0x8000); b.write32(a[2]+16,0x1500c); d[0]=b.read32(p+4); b.write32(p+8,d[0]); b.write32(a[2]+8,d[0]);
        b.write16(p+26,0); b.write16(p+32,0); b.write32(p+16,0x158a8); b.write16(p+52,300); entry=0x158a8;
    }
    if(entry==0x158a8) {
        w(d[0],b.read16(p+36)); if(s(d[0])<=s(b.read16(0xffff89d6))) b.write32(p+16,0x1592a);
        call(0x14dc4); w(d[0],b.read16(p+36)); w(d[0],d[0]-b.read16(0xffff8d18)); ext(0); b.write32(m,d[0]);
        w(d[1],b.read16(0x29c0aa)); w(d[1],d[1]+b.read16(p+46)); b.write16(m+20,d[1]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1);
        b.write16(m+4,d[1]); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,b.read16(0x29c0c4)); w(d[0],b.read16(m-0x3ffa)); asr(d[0],4);
        b.write16(p+48,b.read16(p+48)-d[0]);
        for(auto offsets : {std::pair{48,24},std::pair{50,30}}) {
            w(d[0],b.read16(p+offsets.first)); w(d[0],d[0]-b.read16(p+offsets.second)); ext(0); d[0]<<=8;
            b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+offsets.second+4,d[0]);
        }
        entry=0x1592a;
    }
    if(entry==0x1592a) {
        // $01592A-$015962: collision/removal uses the shared missile callees.
        if(!gate()) return; call(0x157f2); if(b.read16(p+36)>=0x80e0) { call(0x152a8); return; }
        call(0x1578c); call(0x15842); call(0x15200); call(0x150c0); if(uint8_t(d[6])) { call(0x152a8); return; }
        call(0x1581a); if(d[0]) { call(0x152a8); return; } call(0x151b8); call(0x14e1e); OMainPlayer::append(b,r); return;
    }
    // $015968-$015A68: runtime launch-direction pair and word accelerations.
    if(entry==0x15968) {
        w(d[0],b.read16(p+52)); w(d[0],d[0]-4); w(d[0],d[0]+d[0]); a[0]=0x160a8+s(d[0]);
        b.write16(p,0x8000); b.write16(p+22,0); b.write8(p+3,0xb2); a[2]=static_cast<int32_t>(s(b.read16(p+62)));
        b.write16(a[2],0x8000); b.write32(a[2]+16,0x1500c); d[0]=b.read32(p+4); b.write32(p+8,d[0]); b.write32(a[2]+8,d[0]);
        call(0x14dc4); b.write16(p+26,0); b.write16(p+32,0); d[2]=0; w(d[2],b.read16(p+36)); w(d[2],d[2]-b.read16(0xffff8d18)); asr(d[2],2);
        b.write32(m,d[2]); w(d[1],b.read16(0x29c0aa)); w(d[1],d[1]+b.read16(p+46)); b.write16(m+20,d[1]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1);
        b.write16(m+4,d[1]); b.write16(p+58,d[1]); b.write8(p+58,d[1]); b.write16(m-0x4000,d[2]);
        w(d[0],b.read16(a[0])); a[0]+=2; asr(d[0],2); b.write16(m-0x3ffe,d[0]);
        d[0]=b.read32(m-0x3ffc); d[0]+=d[0]; swap(0); ext(0); d[0]+=d[0]; d[0]<<=8;
        b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+28,d[0]); w(d[0],-d[0]); ext(0);
        b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+60,d[0]);
        b.write16(m-0x3ffe,b.read16(a[0])); a[0]+=2; d[0]=b.read32(m-0x3ffc); d[0]+=d[0]; swap(0); ext(0); d[0]+=d[0]; d[0]<<=8;
        b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+34,d[0]); w(d[0],-d[0]); ext(0);
        b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+40,d[0]);
        w(d[0],b.read16(p+48)); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,b.read16(0x29c0c4)); w(d[1],b.read16(m-0x3ffa)); asr(d[1],4);
        w(d[0],d[0]-d[1]); w(d[2],d[0]); w(d[2],d[2]-d[1]); b.write16(p+48,d[2]); b.write32(p+16,0x15a6a); entry=0x15ab6;
    }
    if(entry==0x15a6a) {
        // $015A6A-$015AB4: unsigned depth gate; D3 remains zero-extended.
        d[3]=0; w(d[3],b.read16(p+36));
        if(uint16_t(d[3])<b.read16(0xffff89d6)) b.write32(p+16,0x15ab6);
        else {
            call(0x14dc4); w(d[3],d[3]-b.read16(0xffff8d18)); b.write32(m,d[3]);
            w(d[1],b.read16(0x29c0aa)); w(d[1],d[1]+b.read16(p+46)); b.write16(m+20,d[1]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1);
            b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,b.read16(0x29c0c4)); w(d[1],b.read16(m-0x3ffa)); asr(d[1],4); b.write16(p+48,b.read16(p+48)-d[1]);
        }
        entry=0x15ab6;
    }
    if(entry==0x15ab6) {
        // $015AB6-$015B08: SUBQ.B/BGT uses overflow; one-shot retarget.
        auto old=b.read8(p+58),value=uint8_t(old-1); b.write8(p+58,value);
        const bool positive=value && ((value&0x80)!=0)==(old==0x80);
        if(positive) entry=0x15b0a;
        else {
            auto flags=b.read8(p+45); b.write8(p+45,flags|4);
            if(flags&4) entry=0x15b6e;
            else {
                w(d[0],b.read16(p+48)); w(d[0],d[0]-b.read16(p+24)); ext(0); d[0]<<=3; b.write32(m,d[0]);
                d[1]=0; byte(d[1],b.read8(p+59)); b.write8(p+58,d[1]); b.write16(m+20,d[1]); b.write16(p+28,b.read16(m+8));
                w(d[0],b.read16(p+50)); w(d[0],d[0]-b.read16(p+30)); ext(0); d[0]<<=3; b.write32(m,d[0]); b.write16(p+34,b.read16(m+8));
                b.write16(p+60,0); b.write16(p+40,0); entry=0x15b0a;
            }
        }
    }
    if(entry==0x15b6e) {
        // $015B6E-$015BFC: double divide then acceleration-chip products.
        d[1]=0; w(d[1],b.read16(p+36)); w(d[1],d[1]-b.read16(0xffff8d18)); b.write32(m,d[1]);
        w(d[1],b.read16(0x29c0aa)); w(d[1],d[1]+b.read16(p+46)); b.write16(m+20,d[1]); w(d[1],b.read16(m+8)); w(d[1],d[1]+1);
        b.write16(m+4,d[1]); asr(d[1],1); b.write16(m-0x3ffe,d[1]);
        for(auto offsets : {std::pair{50,30},std::pair{48,24}}) {
            w(d[0],b.read16(p+offsets.first)); w(d[0],d[0]-b.read16(p+offsets.second)); ext(0); d[0]<<=8; b.write32(m+16,d[0]);
            w(d[0],b.read16(m+8)); ext(0); b.write32(m+16,d[0]);
            if(offsets.second==30) {
                w(d[1],b.read16(m+8)); b.write16(p+40,d[1]); b.write16(m-0x4000,d[1]); w(d[1],b.read16(m-0x3ffa)); b.write16(p+34,d[1]);
            } else {
                w(d[0],b.read16(m+8)); b.write16(p+60,d[0]); b.write16(m-0x4000,d[0]); w(d[0],b.read16(m-0x3ffa)); b.write16(p+28,d[0]);
            }
        }
        b.write32(p+16,0x15bfe); entry=0x15bfe;
    }
    if(entry==0x15b0a || entry==0x15bfe) {
        // $015B0A-$015B68 / $015BFE-$015C60: retain overflowing velocities.
        if(!gate()) return; call(0x157f2);
        for(auto offsets : {std::pair{40,34},std::pair{60,28}}) {
            w(d[0],b.read16(p+offsets.first)); auto old=b.read16(p+offsets.second);
            int32_t sum=int32_t(s(old))+s(d[0]); b.write16(p+offsets.second,old+d[0]);
            if(sum<-32768 || sum>32767) b.write16(p+offsets.second,b.read16(p+offsets.second)-d[0]);
        }
        w(d[0],b.read16(p+36)); const auto remove=entry==0x15bfe ? 0x1528a : 0x152a8;
        if(uint16_t(d[0])>=0x80e0) { call(remove); return; }
        call(0x1578c); if(!(b.read8(p+45)&0x40)) call(0x1583e); call(0x15200); call(0x150c0);
        if(uint8_t(d[6])) { call(remove); return; } call(0x1581a); if(d[0]) { call(remove); return; }
        call(0x151b8); call(0x14e1e); OMainPlayer::append(b,r); return;
    }
    // $015C62-$015D9C: debris burst; all velocity tables stay in ROM.
    if(entry==0x15c62 || entry==0x15c7a) {
        a[1]=static_cast<int32_t>(s(b.read16(p+62))); b.write8(p+52,entry==0x15c62 ? 0 : 16);
        b.write16(p+46,entry==0x15c62 ? -160 : 40); b.write16(a[1]+46,entry==0x15c62 ? -140 : 60); entry=0x15d36;
    }
    if(entry==0x15c94 || entry==0x15c9a || entry==0x15ca2 || entry==0x15caa) {
        b.write8(p+52,entry==0x15c94 ? 0 : entry==0x15c9a ? 1 : entry==0x15ca2 ? 16 : 17); entry=0x15cb0;
    }
    if(entry==0x15cb0) {
        a[0]=0xffff8e7f; d[0]=0; byte(d[0],b.read8(a[0]));
        if(uint8_t(d[0])) {
            d[3]=5; const bool borrow=uint16_t(d[0])<6; w(d[0],d[0]-6);
            if(borrow) { w(d[3],d[3]+d[0]); if(s(d[3])<0) { call(0x15d26); return; } d[0]=0; }
            b.write8(a[0]++,d[0]); a[0]+=s(d[0]); a[2]=0x161ac;
            do {
                d[0]=0; byte(d[0],b.read8(a[0]++)); w(d[0],d[0]<<6); a[1]=0xffffa000+s(d[0]); b.write16(a[1],0x8000); b.write32(a[1]+16,0x15d9e);
                b.write16(a[1]+24,b.read16(p+24)); b.write16(a[1]+30,b.read16(p+30)); b.write16(a[1]+44,0); b.write8(a[1]+52,b.read8(p+52));
                b.write16(a[1]+36,b.read16(p+36)); b.write16(a[1]+4,0xfde0); OMainObjects::random(b,r); asr(d[0],4); b.write16(a[1]+28,d[0]);
                b.write16(a[1]+34,0); b.write16(a[1]+46,b.read16(a[2])); a[2]+=2; b.write16(a[1]+40,0x180); w(d[3],d[3]-1);
            } while(uint16_t(d[3])!=0xffff);
        }
        entry=0x15d26;
    }
    if(entry==0x15d26) {
        a[1]=static_cast<int32_t>(s(b.read16(p+62))); b.write16(p+46,-160); b.write16(a[1]+46,-140); entry=0x15d36;
    }
    if(entry==0x15d36) {
        b.write32(p+16,0x15d9e); byte(d[0],b.read8(p+38)); b.write8(0xffff9160,b.read8(0xffff9160)&~(1u<<(d[0]&7)));
        b.write16(a[1],0x8000); b.write32(a[1]+16,0x15d9e); b.write16(a[1]+24,b.read16(p+24)); b.write16(a[1]+30,b.read16(p+30));
        b.write16(a[1]+44,0); b.write8(a[1]+52,b.read8(p+52)); b.write16(a[1]+36,b.read16(p+36)); b.write16(p+4,0xfde0); b.write16(a[1]+4,0xfde0);
        OMainObjects::random(b,r); asr(d[0],4); b.write16(p+28,d[0]); swap(0); asr(d[0],4); b.write16(a[1]+28,d[0]);
        b.write16(p+34,0); b.write16(p+40,0x180); b.write16(a[1]+40,0x180); entry=0x15d9e;
    }
    if(entry==0x15d9e) {
        // $015D9E-$015E54: falling debris, then ground-impact transition.
        b.write32(p+8,b.read32(p+4)); if(!(b.read8(0x29c200)&2)) { call(0x14ff2); return; }
        if(!(b.read8(0xffff91d0)&2)) {
            w(d[0],b.read16(p+36)); w(d[1],0x140);
            if(uint16_t(d[0])>=0x1ff8) { b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[0]); d[1]=b.read32(m-0x3ffc); d[1]<<=3; swap(1); }
            if(s(d[1])<s(b.read16(p+30))) {
                // $015E56 discards its return address: skip the falling caller.
                b.write32(p+16,0x15e6c); b.write16(p+46,0); b.write16(p+44,0); call(0x15f74); call(0x15e6c); return;
            }
            call(0x157f2); w(d[0],b.read16(p+40)); w(d[0],d[0]+b.read16(p+34)); if(uint16_t(d[0])>=0x1e00) w(d[0],0x1e00); b.write16(p+34,d[0]);
            w(d[0],b.read16(p+36)); if(uint16_t(d[0])>=0x80e0) { call(0x14ff2); return; }
            call(0x1578c); call(0x1583e); call(0x15200); call(0x1581a); if(d[0]) { call(0x14ff2); return; }
        }
        // $015DF6-$015E28: falling debris palette/frame and road flips.
        byte(d[0],b.read8(p+52)); byte(d[1],d[0]); w(d[0],d[0]&16); a[0]=0x16188+s(d[0]); w(d[1],d[1]&1);
        b.write8(p+3,b.read8(a[0]+s(d[1]))); w(d[0],b.read16(0x29c0a4)); b.write32(p+12,b.read32(a[0]+s(d[0])+4));
        b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); OMainPlayer::append(b,r); return;
    }
    if(entry==0x15e6c) {
        // $015E6C-$015F46: ground debris depth, world projection and animation.
        b.write32(p+8,b.read32(p+4)); if(!(b.read8(0x29c200)&2)) { call(0x14ff2); return; }
        if(!(b.read8(0xffff91d0)&2)) {
            w(d[0],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[0]); w(d[0],b.read16(p+36));
            if(uint16_t(d[0])>=0x80e0) { call(0x14ff2); return; }
            w(d[0],b.read16(p+36)); d[1]=0;
            if(uint16_t(d[0])>=0x1ff8) {
                b.write16(m-0x4000,10); w(d[0],d[0]-0x1ff8); if(uint16_t(d[0])>=0x1ff8) w(d[0],0x1ff8);
                b.write16(m-0x3ffe,d[0]); d[1]=b.read32(m-0x3ffc); d[1]<<=3; swap(1);
            }
            w(d[1],d[1]<<3); d[0]=0; byte(d[0],b.read8(p+44)); a[0]=0x160ea+s(d[0]); w(d[0],b.read16(a[0]));
            w(d[1],d[1]-d[0]); b.write16(p+30,d[1]); call(0x157ba); call(0x15f48); call(0x16038); call(0x1581a);
            if(d[0]) { call(0x14ff2); return; }
        }
        d[0]=0; byte(d[0],b.read8(p+44)); a[0]=0x160e8+s(d[0]); byte(d[0],b.read8(p+52)); w(d[0],d[0]&1);
        b.write8(p+3,b.read8(a[0]+s(d[0]))); w(d[0],b.read16(0x29c0a4)); b.write32(p+12,b.read32(a[0]+s(d[0])+4));
        b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]);
        auto flags=b.read8(p+45); b.write8(p+45,flags^1);
        if(flags&1) { b.write8(p+44,b.read8(p+44)+16); if(b.read8(p+44)>=0xa0) b.write8(p+44,0x90); }
        OMainPlayer::append(b,r); return;
    }
    if(entry==0x15f48) {
        // $015F48-$015F72: sway keyed by packed depth byte.
        w(d[0],0); byte(d[0],b.read8(p+2)); asr(d[0],2); a[1]=0x33ed8; byte(d[0],b.read8(a[1]+s(d[0]))); w(d[0],static_cast<int8_t>(d[0]));
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,b.read16(0x29c0c6)); d[0]=b.read32(m-0x3ffc); asrl(d[0],6); b.write16(p+24,b.read16(p+24)+d[0]); return;
    }
    if(entry==0x15f74) {
        // $015F74-$016036: inverse projection followed by road rotation.
        w(d[7],b.read16(p+36)); w(d[7],uint16_t(d[7])>>5); w(d[7],d[7]+32); b.write32(m,0x7fe0); b.write16(m+20,d[7]); b.write16(m+4,b.read16(m+8));
        d[1]=10; w(d[0],b.read16(p+4)); w(d[0],d[0]-b.read16(0x29c08e)); ext(0); d[0]<<=d[1]; b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+24,d[0]);
        w(d[0],b.read16(p+6)); w(d[0],d[0]+b.read16(0x29c08c)); w(d[0],d[0]-0x70); ext(0); d[0]<<=d[1]; b.write32(m+16,d[0]); b.write16(p+30,b.read16(m+8));
        w(d[0],b.read16(0x29c09e)); w(d[0],d[0]&0xff); a[0]=0x33e98; byte(d[1],b.read8(a[0]+s(d[0]))); byte(d[2],b.read8(a[0]+s(d[0])-64));
        w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2])); w(d[3],d[1]); w(d[4],d[2]);
        b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,b.read16(p+24)); d[2]=b.read32(m-0x3ffc); w(d[6],b.read16(p+30));
        b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[6]); d[1]=b.read32(m-0x3ffc); d[1]+=d[2]; asrl(d[1],7);
        b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,b.read16(p+24)); d[3]=b.read32(m-0x3ffc); b.write16(m-0x4000,d[4]); b.write16(m-0x3ffe,d[6]);
        d[4]=b.read32(m-0x3ffc); d[3]-=d[4]; asrl(d[3],7); w(d[3],d[3]+b.read16(0x29c080)); b.write16(p+24,d[1]); b.write16(p+30,d[3]); return;
    }
    if(entry==0x16038) {
        // $016038-$0160A6: shared world-offset pair (not a guessed flat Y).
        a[0]=0x29c1a8+s(b.read16(p+30)); b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+24));
        d[0]=b.read32(m-0x3ffc); d[0]+=b.read32(a[0]); a[0]+=4; asrl(d[0],7); b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[7]);
        d[0]=b.read32(m-0x3ffc); d[0]<<=6; swap(0); w(d[0],d[0]+b.read16(0x29c08e)); b.write16(p+4,d[0]);
        b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+24)); d[0]=b.read32(m-0x3ffc); d[0]-=b.read32(a[0]); asrl(d[0],7);
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[7]); d[0]=b.read32(m-0x3ffc); d[0]<<=6; swap(0); w(d[0],d[0]+0x70); w(d[0],d[0]-b.read16(0x29c08c)); b.write16(p+6,d[0]); return;
    }
    throw std::runtime_error("unported enemy weapon handler");
}

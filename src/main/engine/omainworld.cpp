#include "engine/omainworld.hpp"
#include "engine/omainplayer.hpp"
#include "engine/omainsound.hpp"
#include <stdexcept>

namespace {
void w(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
void byte(uint32_t& r, uint8_t v) { r=(r&0xffffff00u)|v; }
int16_t s(uint32_t v) { return static_cast<int16_t>(v); }
void asr(uint32_t& r, unsigned n) { w(r,n>=16 ? (s(r)<0 ? 0xffff : 0) : s(r)>>n); }
void asrl(uint32_t& r, unsigned n) { r=static_cast<int32_t>(r)>>n; }
// $008B3E-$008B60 / $009220-$009242 / $00924E-$009270.
void sway(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5], m=r.a[6]; auto& d=r.d;
    w(d[6],d[7]); asr(d[6],4); r.a[0]=0x33ed8;
    byte(d[6],b.read8(r.a[0]+s(d[6]))); w(d[6],static_cast<int8_t>(d[6]));
    b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c0c6));
    d[6]=b.read32(m-0x3ffc); asrl(d[6],6); b.write16(p+42,b.read16(p+42)+d[6]);
}
// $008B64-$008B90 / $009274-$0092A0.
void horizontal(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5], m=r.a[6]; auto& d=r.d;
    b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]+=d[4]; asrl(d[0],7);
    b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[7]); d[0]=b.read32(m-0x3ffc);
    asrl(d[0],8); asrl(d[0],2); w(d[0],d[0]+b.read16(0x29c08e)); b.write16(p+4,d[0]);
}
// $008BBC-$008BEC / $0092BA-$0092EA.
void vertical(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5], m=r.a[6]; auto& d=r.d;
    b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]-=d[5]; asrl(d[0],7);
    b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[7]); d[0]=b.read32(m-0x3ffc);
    asrl(d[0],8); asrl(d[0],2); w(d[0],d[0]+0x70); w(d[0],d[0]-b.read16(0x29c08c)); b.write16(p+6,d[0]);
}
}

void OMainWorld::release(MainBus& b, OMainObjects::Registers& r) {
    // $009556-$009572: free record and return size-slot byte to free list.
    b.write16(r.a[5]+36,0); b.write8(r.a[5],b.read8(r.a[5])&~0x40); b.write16(r.a[5],b.read16(r.a[5])&0x7c00);
    r.a[0]=0xffff8d80; b.write8(r.a[0],b.read8(r.a[0])+1); w(r.d[0],0); byte(r.d[0],b.read8(r.a[0]));
    b.write8(r.a[0]+s(r.d[0]),b.read8(r.a[5]+20));
}

void OMainWorld::initialize(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    // $0088C2-$0088DC: runtime world offset pair and initial depth.
    b.write32(p+16,0x8af2); b.write8(p+1,0); b.write16(p+36,b.read16(0x29c0c2));
    r.a[0]=b.read32(p+54); d[4]=b.read32(r.a[0]); r.a[0]+=4; d[5]=b.read32(r.a[0]);
    // $0088DE-$008912: horizontal chip product then perspective divide.
    b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]+=d[4]; asrl(d[0],7); d[0]<<=5;
    w(d[2],b.read16(p+36)); asr(d[2],5); w(d[2],d[2]+0x20);
    b.write32(m,d[0]); b.write16(m+20,d[2]); w(d[0],b.read16(m+8));
    w(d[0],d[0]+b.read16(0x29c08e)); b.write16(p+4,d[0]);
    // $008916-$008944: vertical product reuses perspective divisor.
    b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]-=d[5]; asrl(d[0],7); d[0]<<=5;
    b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); w(d[0],d[0]+0x70);
    w(d[0],d[0]-b.read16(0x29c08c)); b.write16(p+6,d[0]);
}

void OMainWorld::update(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    // $008AF2-$008B20: move depth and compute scale, delete past origin.
    b.write32(p+8,b.read32(p+4)); w(d[1],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<0) { release(b,r); return; }
    w(d[1],d[1]+0x20); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]);
    // $008B22-$008B3C: packed object scale, runtime world offset pair.
    w(d[1],d[0]); asr(d[0],3); b.write8(p+2,d[0]); byte(d[0],b.read8(p+21)); asr(d[1],d[0]&63);
    b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[1]);
    r.a[0]=b.read32(p+54); d[4]=b.read32(r.a[0]); r.a[0]+=4; d[5]=b.read32(r.a[0]); sway(b,r); horizontal(b,r);
    // $008B94-$008BB8: clipping reads the sub CPU's scaled-width table.
    r.a[1]=0x2a3800; w(d[1],0); byte(d[1],b.read8(p+20)); w(d[1],d[1]+d[1]); w(d[1],d[1]+d[1]);
    w(d[1],b.read16(r.a[1]+s(d[1]))); asr(d[1],1); w(d[1],d[1]+0xa0);
    if(s(d[0])>s(d[1])) { release(b,r); return; }
    w(d[1],-d[1]); if(s(d[0])<=s(d[1])) { release(b,r); return; }
    vertical(b,r);
    // $008BF0-$008C14: animation offset and flip flags come from shared RAM.
    r.a[0]=0x29c0a2; w(d[0],0); byte(d[0],b.read8(p+59)); w(d[1],b.read16(r.a[0]+s(d[0])));
    r.a[1]=b.read32(p+60); b.write32(p+12,b.read32(r.a[1]+s(d[1])));
    b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); OMainPlayer::append(b,r);
}

void OMainWorld::explosion(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    // $0091C0-$0091FE: previous position, pause gate, age/depth and scale.
    b.write32(p+8,b.read32(p+4)); if(b.read8(0xffff91d0)&1) { OMainPlayer::append(b,r); return; }
    b.write16(p+38,b.read16(p+38)+1); w(d[1],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<0) { release(b,r); return; }
    w(d[1],d[1]+0x20); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]);
    // $009200-$00921E: unsigned native MULU, packed zoom and byte shift.
    asr(d[0],2); w(d[2],d[0]); asr(d[0],1); b.write8(p+2,d[0]);
    d[2]=uint32_t(uint16_t(d[2]))*b.read16(p+50); asrl(d[2],8); byte(d[1],b.read8(p+21)); w(d[1],d[1]&3);
    w(d[2],d[2]<<(d[1]&63)); b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[2]);
    // $009220-$009270: the ROM really applies the sway twice.
    sway(b,r); r.a[0]=b.read32(p+54); d[4]=b.read32(r.a[0]); r.a[0]+=4; d[5]=b.read32(r.a[0]); sway(b,r);
    horizontal(b,r);
    // $0092A4-$009302: horizontal delete, vertical hidden/removed limits.
    w(d[1],d[7]); asr(d[1],4); w(d[1],d[1]+0xa0);
    if(s(d[0])>s(d[1])) { release(b,r); return; }
    w(d[1],-d[1]); if(s(d[0])<=s(d[1])) { release(b,r); return; }
    vertical(b,r); w(d[1],d[7]); asr(d[1],3); w(d[1],-d[1]);
    if(s(d[0])<=s(d[1])) return;
    w(d[1],-d[1]); w(d[1],d[1]+0xe0); if(s(d[0])>s(d[1])) { release(b,r); return; }
    // $009306-$00934E: signed timer, frame progression and runtime payload.
    auto old=b.read16(p+48); b.write16(p+48,old-1);
    if(s(old)<1) {
        byte(d[1],b.read8(p+46));
        if(static_cast<int8_t>(d[1])>static_cast<int8_t>(b.read8(p+47))) {
            b.write8(p+47,b.read8(p+47)+1); w(d[0],0); byte(d[0],b.read8(p+47)); w(d[0],d[0]<<3);
            r.a[0]=b.read32(p+60); b.write32(p+12,b.read32(r.a[0]+s(d[0])));
            b.write8(p+3,b.read8(r.a[0]+s(d[0])+4)); w(d[1],0); byte(d[1],b.read8(r.a[0]+s(d[0])+6)); b.write16(p+48,d[1]);
            w(d[1],0); byte(d[1],b.read8(r.a[0]+s(d[0])+7)); b.write16(p+50,d[1]); b.write8(p+21,b.read8(r.a[0]+s(d[0])+5));
        }
    }
    // $009354-$009364: shared flips and request.
    b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); OMainPlayer::append(b,r);
}

void OMainWorld::effect_initialize(MainBus& b, OMainObjects::Registers& r, bool ground) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    // $008D44/$008D58-$008DA4: select effect, rotate noise, read ROM animation.
    b.write32(p+16,ground ? 0x8e18 : 0x8ece);
    if(ground) { b.write8(p,b.read8(p)&0xfc); b.write8(p,b.read8(p)|0x40); }
    b.write8(p+1,0); w(d[0],b.read16(0xffff8d32));
    w(d[0],(uint16_t(d[0])>>1)|(d[0]<<15)); w(d[2],d[0]); b.write16(0xffff8d32,d[0]);
    w(d[0],d[0]&7); w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]); r.a[0]=0x345c8;
    r.a[0]=b.read32(r.a[0]+s(d[0])); w(d[0],b.read16(r.a[0])); r.a[0]+=2;
    b.write8(p+46,d[0]); b.write32(p+60,r.a[0]); b.write8(p+47,0);
    w(d[2],static_cast<int8_t>(d[2])); asr(d[2],1); b.write16(p+42,b.read16(p+42)+d[2]);
    b.write32(p+54,0x29c190); b.write16(p+40,0); b.write16(p+38,0);
    // $008DA8-$008E16: perspective projection preserves the divisor for Y.
    r.a[0]=b.read32(p+54); d[4]=b.read32(r.a[0]); r.a[0]+=4; d[5]=b.read32(r.a[0]);
    b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]+=d[4]; asrl(d[0],7); d[0]<<=5;
    w(d[2],b.read16(p+36)); asr(d[2],5); w(d[2],d[2]+0x20);
    b.write32(m,d[0]); b.write16(m+20,d[2]); w(d[0],b.read16(m+8));
    w(d[0],d[0]+b.read16(0x29c08e)); b.write16(p+4,d[0]);
    b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42));
    d[0]=b.read32(m-0x3ffc); d[0]-=d[5]; asrl(d[0],7); d[0]<<=5;
    b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); w(d[0],d[0]+0x70);
    w(d[0],d[0]-b.read16(0x29c08c)); b.write16(p+6,d[0]);
}

void OMainWorld::effect_update(MainBus& b, OMainObjects::Registers& r, bool ground) {
    auto& d=r.d; const auto p=r.a[5],m=r.a[6];
    // $008E18-$008E82 / $008ECE-$008F38: age, depth, scale growth through age 4.
    b.write32(p+8,b.read32(p+4)); if(b.read8(0xffff91d0)&1) { OMainPlayer::append(b,r); return; }
    b.write16(p+38,b.read16(p+38)+1); w(d[1],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[1]);
    w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<0) { release(b,r); return; }
    w(d[1],d[1]+0x20); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]);
    asr(d[0],2); w(d[1],d[0]); asr(d[0],1); b.write8(p+2,d[0]);
    w(d[2],b.read16(p+38)); if(s(d[2])>4) w(d[2],4);
    b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,d[1]); d[1]=b.read32(m-0x3ffc); asr(d[1],2);
    b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[1]);
    // $008E84-$008ECA / $008F3A-$008F64: ground special drift or normal sway.
    r.a[0]=b.read32(p+54); d[4]=b.read32(r.a[0]); r.a[0]+=4; d[5]=b.read32(r.a[0]);
    if(ground && b.read16(0x29c33c)) { w(d[0],b.read16(0x29c33e)); b.write16(p+42,b.read16(p+42)+d[0]); }
    else sway(b,r);
    // $008F68-$008FF6: project and clip; below the top is hidden, not freed.
    horizontal(b,r); w(d[1],d[7]); asr(d[1],4); w(d[1],d[1]+0xa0);
    if(s(d[0])>s(d[1])) { release(b,r); return; }
    w(d[1],-d[1]); if(s(d[0])<=s(d[1])) { release(b,r); return; }
    vertical(b,r); w(d[1],d[7]); asr(d[1],3); w(d[1],-d[1]); if(s(d[0])<=s(d[1])) return;
    w(d[1],-d[1]); w(d[1],d[1]+0xe0); if(s(d[0])>s(d[1])) { release(b,r); return; }
    // $008FFA-$009044: one frame every other tick, palette before direction offset.
    byte(d[0],b.read8(p+46));
    if(uint8_t(d[0])!=b.read8(p+47)) {
        w(d[0],b.read16(p+38)); w(d[0],d[0]&1);
        if(!uint16_t(d[0])) b.write8(p+47,b.read8(p+47)+1);
    }
    byte(d[0],b.read8(p+47)); w(d[0],static_cast<int8_t>(d[0])); w(d[0],d[0]<<4); r.a[0]=b.read32(p+60);
    b.write8(p+3,b.read8(r.a[0]+s(d[0])+12)); w(d[0],d[0]+b.read16(0x29c0a4));
    b.write32(p+12,b.read32(r.a[0]+s(d[0]))); b.write8(p,b.read8(p)&0xcf);
    byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); OMainPlayer::append(b,r);
}

void OMainWorld::player_collision(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5];
    // $008CC8-$008CEA: only solid objects within the open depth interval.
    if(static_cast<int8_t>(b.read8(p+24))>=0 || b.read16(0x29c09a)) return;
    w(d[0],b.read16(0xffff8d18));
    const auto lhs=uint16_t(d[0]), rhs=b.read16(p+36); w(d[0],d[0]-rhs);
    const bool overflow=((lhs^rhs)&(lhs^uint16_t(d[0]))&0x8000)!=0;
    // $008CE2 BLE after SUB.W tests Z || N!=V, including signed overflow.
    if(!uint16_t(d[0]) || ((d[0]&0x8000)!=0)!=overflow || s(d[0])>=0x200) return;
    // $008CEE-$008D38: sub CPU's exact scaled width/height, strict overlap.
    r.a[1]=0x2a3800; w(d[1],0); byte(d[1],b.read8(p+20)); w(d[1],d[1]+d[1]); w(d[1],d[1]+d[1]); r.a[1]+=s(d[1]);
    w(d[2],b.read16(r.a[1])); r.a[1]+=2; asr(d[2],1); r.a[0]=b.read32(0x1c5c0);
    w(d[0],b.read16(p+4)); w(d[1],d[0]); w(d[0],d[0]-d[2]); w(d[1],d[1]+d[2]);
    if(s(d[0])>=s(b.read16(r.a[0]+4)) || s(d[1])<=s(b.read16(r.a[0]+4))) return;
    w(d[0],b.read16(p+6)); w(d[1],d[0]); w(d[0],d[0]-b.read16(r.a[1])); r.a[1]+=2;
    if(s(d[0])>=s(b.read16(r.a[0]+6)) || s(d[1])<=s(b.read16(r.a[0]+6))) return;
    // $008D3C: request player damage; the live handler owns the transition.
    b.write8(0xffff8d60,b.read8(0xffff8d60)|1);
}


void OMainWorld::run(MainBus& b, OMainObjects::Registers& r, uint32_t entry, const OMainObjects::Call& boundary) {
    auto& d=r.d; auto& a=r.a; const auto p=a[5],m=a[6];
    auto call=[&](uint32_t target) { run(b,r,target,boundary); };
    if(entry==0x9046) {
        // $009046-$0090AA: explosion allocation, projectile's position for gates.
        a[1]=0xffff8d80; w(d[0],0); byte(d[0],b.read8(a[1])); if(!uint8_t(d[0])) return;
        d[6]=b.read32(a[0]+42); a[0]=0xffffa000; byte(d[0],b.read8(a[1]+s(d[0]))); b.write8(a[1],b.read8(a[1])-1);
        w(d[0],d[0]<<6); a[0]+=s(d[0]); b.write8(a[0],0x80); b.write32(a[0]+16,0x90ac); byte(d[0],b.read8(p+24));
        if(uint8_t(d[0])==0x89 || uint8_t(d[0])==0x8a) b.write32(a[0]+42,d[6]);
        else { b.write16(a[0]+42,b.read16(p+42)); b.write16(a[0]+44,0x140); }
        b.write16(a[0]+36,b.read16(p+36)); w(d[0],d[0]&0x7f); b.write8(a[0]+24,d[0]); b.write16(a[0]+40,0); return;
    }
    if(entry==0x8c1c) {
        // $008C1C-$008CC6: strict scaled projectile bounds; never invent scoring.
        if(!b.read8(p+24)) return;
        a[0]=b.read32(0x1c5d8); w(d[7],b.read16(0x1c5de)); w(d[7],d[7]-1);
        do {
            if(b.read8(a[0])&0x80) {
                w(d[0],b.read16(a[0]+36));
                if(s(d[0])>s(b.read16(p+36))) {
                    w(d[0],d[0]-0x200);
                    if(s(d[0])<s(b.read16(p+36))) {
                        a[1]=0x2a3800; w(d[1],0); byte(d[1],b.read8(p+20)); w(d[1],d[1]+d[1]); w(d[1],d[1]+d[1]); a[1]+=s(d[1]);
                        w(d[2],b.read16(a[1])); a[1]+=2; asr(d[2],1); w(d[0],b.read16(p+4)); w(d[1],d[0]); w(d[0],d[0]-d[2]); w(d[1],d[1]+d[2]);
                        if(s(d[0])<s(b.read16(a[0]+4)) && s(d[1])>s(b.read16(a[0]+4))) {
                            w(d[0],b.read16(p+6)); w(d[1],d[0]); w(d[0],d[0]-b.read16(a[1])); a[1]+=2;
                            if(s(d[0])<s(b.read16(a[0]+6)) && s(d[1])>s(b.read16(a[0]+6))) {
                                call(0x9046);
                                if(!boundary) throw std::runtime_error("unported Phase 3e score boundary $E542");
                                boundary(0xe542,r);
                                if(b.read8(p+24)!=0x89 && b.read8(p+24)!=0x8a) release(b,r);
                                return;
                            }
                        }
                    }
                }
            }
            a[0]+=64; w(d[7],d[7]-1);
        } while(uint16_t(d[7])!=0xffff); return;
    }
    if(entry==0x8946) {
        // $008946-$0089BE: projectile/player checks, paired gate crossing.
        b.write32(p+8,b.read32(p+4)); if(b.read8(0xffff91d0)&1) { OMainPlayer::append(b,r); return; }
        call(0x8c1c); player_collision(b,r);
        if(b.read8(p+24)==0x89 || b.read8(p+24)==0x8a) {
            w(d[0],b.read16(p+36)); w(d[1],d[0]); w(d[0],d[0]-b.read16(0x29c0aa));
            if(s(d[0])<=s(b.read16(0xffff8d18)) && s(d[1])>=s(b.read16(0xffff8d18))) {
                b.write16(b.read8(p+24)==0x89 ? 0xffff8d70 : 0xffff8d72,b.read16(p+42));
                w(d[0],b.read16(0xffff8d70)); w(d[0],d[0]+b.read16(0xffff8d72)); asr(d[0],1); if(s(d[0])==-1) w(d[0],0); b.write16(0xffff8d74,d[0]);
            }
        }
        // $0089C0-$008A04: scale and runtime world-offset pair.
        w(d[1],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[1]); w(d[1],b.read16(p+36)); asr(d[1],5);
        if(s(d[1])<0) { release(b,r); return; }
        w(d[1],d[1]+32); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]); w(d[1],d[0]); asr(d[0],3);
        b.write8(p+2,d[0]); byte(d[0],b.read8(p+21)); asr(d[1],d[0]&63); b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[1]);
        a[0]=b.read32(p+54); d[4]=b.read32(a[0]); a[0]+=4; d[5]=b.read32(a[0]);
        // $008A06-$008A60: drift override or depth-dependent sway.
        w(d[0],b.read16(0x29c33e));
        if(uint16_t(d[0])) b.write16(p+42,b.read16(p+42)+d[0]);
        else {
            w(d[6],d[7]); asr(d[6],2); b.write16(m-0x4000,d[6]); w(d[0],b.read16(0x29c0c0)); w(d[1],d[0]); w(d[0],d[0]-b.read16(p+36));
            b.write16(m-0x3ffe,d[0]); b.write32(m,b.read32(m-0x3ffc)); b.write16(m+20,d[1]); w(d[6],b.read16(m+8)); asr(d[6],2); a[0]=0x33ed8;
            byte(d[6],b.read8(a[0]+s(d[6]))); w(d[6],static_cast<int8_t>(d[6])); b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c0c6));
            d[6]=b.read32(m-0x3ffc); asrl(d[6],6); b.write16(p+42,b.read16(p+42)+d[6]);
        }
        // $008A62-$008AF0: projection, shared animation and flip flags.
        horizontal(b,r); vertical(b,r); a[0]=0x29c0a2; w(d[0],0); byte(d[0],b.read8(p+59)); w(d[1],b.read16(a[0]+s(d[0])));
        a[1]=b.read32(p+60); b.write32(p+12,b.read32(a[1]+s(d[1]))); b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); OMainPlayer::append(b,r); return;
    }
    if(entry==0x90ac) {
        // $0090AC-$009120: runtime explosion type, first frame, both counters.
        b.write8(p+1,0); w(d[0],0); byte(d[0],b.read8(p+24)); byte(d[0],d[0]-1); w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]);
        a[0]=0x347be; a[0]=b.read32(a[0]+s(d[0])); w(d[0],b.read16(a[0])); a[0]+=2; b.write8(p+46,d[0]); b.write32(p+60,a[0]);
        b.write32(p+12,b.read32(a[0])); b.write8(p+3,b.read8(a[0]+4)); w(d[1],0); byte(d[1],b.read8(a[0]+6)); b.write16(p+48,d[1]);
        w(d[1],0); byte(d[1],b.read8(a[0]+7)); b.write16(p+50,d[1]); b.write8(p+21,b.read8(a[0]+5)); b.write8(p+47,0); b.write16(p+38,0); b.write16(p+48,0);
        byte(d[1],0x8a); byte(d[1],d[1]&0x7f); if(uint8_t(d[1])==b.read8(p+24)) { call(0x9366); return; }
        byte(d[1],0x89); byte(d[1],d[1]&0x7f); if(uint8_t(d[1])==b.read8(p+24)) { call(0x9366); return; }
        // $009122-$0091B4: initialize world projection, RNG-selected sound.
        b.write8(p,b.read8(p)|0x40); a[0]=b.read32(p+54); d[4]=b.read32(a[0]); a[0]+=4; d[5]=b.read32(a[0]);
        b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42)); d[0]=b.read32(m-0x3ffc); d[0]+=d[4]; asrl(d[0],7); d[0]<<=5;
        w(d[2],b.read16(p+36)); asr(d[2],5); w(d[2],d[2]+32); b.write32(m,d[0]); b.write16(m+20,d[2]); w(d[0],b.read16(m+8));
        w(d[0],d[0]+b.read16(0x29c08e)); b.write16(p+4,d[0]); b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42));
        d[0]=b.read32(m-0x3ffc); d[0]-=d[5]; asrl(d[0],7); d[0]<<=5; b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); w(d[0],d[0]+0x70); w(d[0],d[0]-b.read16(0x29c08c)); b.write16(p+6,d[0]);
        OMainObjects::random(b,r); w(d[0],d[0]&7); a[1]=0x91b8; byte(d[0],b.read8(a[1]+s(d[0]))); OMainSound::enqueue(b,r,true); b.write32(p+16,0x91c0); explosion(b,r); return;
    }
    if(entry==0x9366) {
        // $009366-$00939E: gate explosion initial projection and sound.
        auto old=b.read16(p+36), value=uint16_t(old-0x200); b.write16(p+36,value);
        bool overflow=((old^0x200)&(old^value)&0x8000)!=0;
        if(((value&0x8000)!=0)!=overflow) { release(b,r); return; }
        w(d[1],b.read16(p+36)); asr(d[1],5); w(d[1],d[1]+32); b.write16(m+4,d[1]); b.write32(m+16,0x7fe0); w(d[7],b.read16(m+8)); call(0x94b8);
        byte(d[0],0xf6); OMainSound::enqueue(b,r,true); b.write32(p+16,0x93a0); entry=0x93a0;
    }
    if(entry==0x93a0) {
        // $0093A0-$009426: gate explosion shares scale and double sway.
        b.write32(p+8,b.read32(p+4)); if(b.read8(0xffff91d0)&1) { OMainPlayer::append(b,r); return; }
        b.write16(p+38,b.read16(p+38)+1); w(d[1],b.read16(0x29c0aa)); b.write16(p+36,b.read16(p+36)-d[1]); w(d[1],b.read16(p+36)); asr(d[1],5);
        if(s(d[1])<0) { release(b,r); return; }
        w(d[1],d[1]+32); b.write32(m,0x7fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[7],d[0]); asr(d[0],2); w(d[2],d[0]); asr(d[0],1); b.write8(p+2,d[0]);
        d[2]=uint16_t(d[2])*uint32_t(b.read16(p+50)); asrl(d[2],8); byte(d[1],b.read8(p+21)); w(d[1],d[1]&3); w(d[2],d[2]<<(d[1]&63)); b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[2]);
        sway(b,r); a[0]=b.read32(p+54); d[4]=b.read32(a[0]); a[0]+=4; d[5]=b.read32(a[0]); sway(b,r); call(0x94b8);
        // $009458-$0094B6: animation timer signed BGE observes SUB overflow.
        auto old=b.read16(p+48), value=uint16_t(old-1); b.write16(p+48,value);
        if(((value&0x8000)!=0)!=(old==0x8000)) {
            byte(d[1],b.read8(p+46)); if(static_cast<int8_t>(d[1])<=static_cast<int8_t>(b.read8(p+47))) { release(b,r); return; }
            b.write8(p+47,b.read8(p+47)+1); w(d[0],0); byte(d[0],b.read8(p+47)); w(d[0],d[0]<<3); a[0]=b.read32(p+60);
            auto frame=a[0]+s(d[0]); b.write32(p+12,b.read32(frame)); b.write8(p+3,b.read8(frame+4)); w(d[1],0); byte(d[1],b.read8(frame+6)); b.write16(p+48,d[1]);
            w(d[1],0); byte(d[1],b.read8(frame+7)); b.write16(p+50,d[1]); b.write8(p+21,b.read8(frame+5));
        }
        b.write8(p,b.read8(p)&0xcf); byte(d[0],b.read8(0x29c0a0)); b.write8(p,b.read8(p)|d[0]); OMainPlayer::append(b,r); return;
    }
    if(entry==0x94b8) {
        // $0094B8-$009554: gate explosion rotation uses shared +$8A/+$88.
        w(d[0],0); byte(d[0],b.read8(0x29c09f)); a[0]=0x33e98; byte(d[1],b.read8(a[0]+s(d[0]))); byte(d[2],b.read8(a[0]+s(d[0])-64));
        w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2])); w(d[3],d[1]); w(d[4],d[2]); b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,b.read16(p+42));
        d[2]=b.read32(m-0x3ffc); w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080)); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[6]); d[1]=b.read32(m-0x3ffc); d[1]+=d[2]; asrl(d[1],7);
        b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[7]); d[1]=b.read32(m-0x3ffc); asrl(d[1],8); asrl(d[1],2); w(d[1],d[1]+b.read16(0x29c08a)); b.write16(p+4,d[1]);
        b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,b.read16(p+42)); d[3]=b.read32(m-0x3ffc); b.write16(m-0x4000,d[4]); b.write16(m-0x3ffe,d[6]); d[4]=b.read32(m-0x3ffc); d[3]-=d[4]; asrl(d[3],7);
        b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,d[7]); d[3]=b.read32(m-0x3ffc); asrl(d[3],8); asrl(d[3],2); w(d[3],d[3]+0x70); w(d[3],d[3]-b.read16(0x29c088)); b.write16(p+6,d[3]); return;
    }
    throw std::runtime_error("unported world graph entry");
}

#include "engine/omaindamage.hpp"
#include "engine/omainplayer.hpp"
#include "engine/omainsound.hpp"
#include <stdexcept>
#include <utility>

namespace {
void w(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
void byte(uint32_t& r, uint8_t v) { r=(r&0xffffff00u)|v; }
int16_t s(uint32_t v) { return static_cast<int16_t>(v); }
void asr(uint32_t& r, unsigned n) { w(r,s(r)>>n); }
void asrl(uint32_t& r, unsigned n) { r=static_cast<int32_t>(r)>>n; }
// Memory ADDQ.L/ADDI.L write low word first, unlike MOVE.L.
void add_long(MainBus& b, uint32_t a, uint32_t n) {
    auto v=b.read32(a)+n; b.write16(a+2,v); b.write16(a,v>>16);
}
// $00FDC2-$00FDEC and the three $00FFE6..$01023A initializer blocks.
void seed_companion(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; auto p=r.a[5], a=r.a[0];
    b.write32(p+26,a); b.write32(p+12,b.read32(a)); w(d[0],b.read16(a+6)); w(d[0],d[0]+1); b.write16(p+32,d[0]);
    byte(d[0],b.read8(a+5)); w(d[0],d[0]&0xff); b.write16(p+34,d[0]); b.write8(p+3,b.read8(a+4));
}
// $00FEBC-$00FEEE and $010064/$01016E/$010284: SUBQ's BGT tests N==V.
void advance_companion(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; auto p=r.a[5]; auto old=b.read16(p+32), value=uint16_t(old-1); b.write16(p+32,value);
    if(value && ((value&0x8000)!=0)==(old==0x8000)) return;
    r.a[0]=b.read32(p+26); b.write16(p+30,b.read16(p+30)+1); w(d[0],b.read16(p+30)); w(d[0],d[0]<<3);
    auto a=r.a[0]+s(d[0]); b.write32(p+12,b.read32(a)); b.write16(p+32,b.read16(a+6));
    byte(d[1],b.read8(a+5)); w(d[1],d[1]&0xff); b.write16(p+34,d[1]); b.write8(p+3,b.read8(a+4));
}
// $00FBFA-$00FC90 / $00FF08-$00FFBC, with the latter's horizontal removal.
void project_companion(MainBus& b, OMainObjects::Registers& r, bool clip) {
    auto& d=r.d; auto p=r.a[5], m=r.a[6];
    b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42)); d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080)); b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c090));
    d[6]=b.read32(m-0x3ffc); d[0]+=d[6]; asrl(d[0],7); d[0]<<=5;
    w(d[2],d[7]); w(d[2],d[2]+0x20); b.write32(m,d[0]); b.write16(m+20,d[2]); w(d[0],b.read16(m+8));
    w(d[0],d[0]+b.read16(0x29c08a)); b.write16(p+4,d[0]);
    if(clip) {
        w(d[1],b.read16(p)); w(d[1],d[1]&0x3ff); asr(d[1],2); w(d[1],d[1]+0xa0);
        if(s(d[0])>s(d[1])) { OMainDamage::run(b,r,0x102f2); return; }
        w(d[1],-d[1]); if(s(d[0])<=s(d[1])) { OMainDamage::run(b,r,0x102f2); return; }
    }
    b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42)); d[0]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080)); b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c092));
    d[6]=b.read32(m-0x3ffc); d[0]-=d[6]; asrl(d[0],7); d[0]<<=5;
    b.write32(m,d[0]); b.write16(m+20,d[2]); w(d[0],b.read16(m+8)); w(d[0],d[0]+0x70); w(d[0],d[0]-b.read16(0x29c088)); b.write16(p+6,d[0]);
}
}

void OMainDamage::run(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    switch(entry) {
    case 0x137e2:
        // $0137E2-$01381A: paired text tiles from a runtime ROM string.
        w(d[1],0); w(d[2],0); w(d[0],b.read16(r.a[0])); r.a[0]+=2;
        r.a[1]=0xd0000+s(d[0]); byte(d[2],b.read8(r.a[0]++));
        w(d[2],(uint16_t(d[2])>>7)|(uint16_t(d[2])<<9)); w(d[2],d[2]|0x8100);
        byte(d[1],b.read8(r.a[0]++));
        do {
            w(d[0],0); byte(d[0],b.read8(r.a[0]++));
            if(uint8_t(d[0])==0x20) { w(d[0],0); b.write16(r.a[1],d[0]); r.a[1]+=2; }
            else { w(d[0],d[0]+d[0]); w(d[0],d[0]+d[2]); b.write16(r.a[1],d[0]); r.a[1]+=2; w(d[0],d[0]+1); }
            b.write16(r.a[1]+126,d[0]); w(d[1],d[1]-1);
        } while(uint16_t(d[1])!=0xffff);
        return;
    case 0xe662:
        // $00E662-$00E6AC: damage text, saved bank and control requests.
        r.a[0]=0x2bb36; run(b,r,0x137e2); b.write8(0x29c200,b.read8(0x29c200)&~0x40);
        b.write16(0xffff9118,0); b.write16(p+50,0); byte(d[0],b.read8(0xffff8d4d)); w(d[0],static_cast<int8_t>(d[0]));
        b.write16(p+56,d[0]); b.write16(p+52,4); b.write16(p+54,4);
        b.write16(0xffff912e,0); b.write16(0xffff9132,0x20); b.write16(0xffff9130,0xffc0);
        if(s(b.read16(p+56))>=0) b.write16(0xffff9130,-b.read16(0xffff9130));
        // $00E6B0-$00E70A: projection, hit count and intact/breakup decision.
        run(b,r,0xe7e0); b.write8(p+3,0xf2); b.write16(0xffff9124,0); b.write16(0xffff9116,b.read16(0xffff9116)+1);
        { bool intact=false;
          if(b.read16(0x29c060)!=5) {
              if(!b.read16(0x29c060)) intact=b.read16(0xffff9116)==1;
              else if(b.read8(0x29c202)&1) b.write16(0xffff9130,0);
              else { OMainObjects::random(b,r); w(d[0],d[0]&0xff); w(d[0],d[0]-0x48); intact=s(d[0])<=0; }
          }
          if(intact) {
              // $00E8A6-$00E8CE: intact jet retains optional angular motion.
              b.write8(0xffff911a,b.read8(0xffff911a)&~1); b.write8(p+34,b.read8(p+34)&~1);
              if(d[0]&0x10) b.write8(p+34,b.read8(p+34)|1);
              b.write32(p+16,0xe8d0); b.write32(p-48,0xfb08); run(b,r,0xe8d0); return;
          }
        }
        // $00E70E-$00E740: breakup sound, trajectories and first controller tick.
        b.write8(0xffff911a,b.read8(0xffff911a)|1); b.write8(p+38,b.read8(p+38)&~1);
        byte(d[0],0xda); OMainSound::enqueue(b,r,true); b.write16(0xffff91b4,0); b.write16(p+36,0xc60);
        b.write32(p+16,0xe94e); b.write32(p-48,0xe7da); run(b,r,0xe744); run(b,r,0xe94e); return;
    case 0xe8d0: case 0xe94e:
        // $00E8D0/$00E94E: history and enabled/draw-only controller gates.
        b.write32(p+8,b.read32(p+4));
        if(!(b.read8(0x29c200)&4)) { run(b,r,0xecac); return; }
        if(b.read8(0xffff91d0)&4) {
            if(entry==0xe8d0) { w(d[0],b.read16(0xffff9124)); if(s(d[0])<5) OMainPlayer::append(b,r); }
            else if(!b.read16(0xffff9124)) OMainPlayer::append(b,r);
            return;
        }
        // $00E8FE/$00E974: original tick/state and runtime branch table.
        b.write16(0x29c09a,b.read16(0x29c09a)+1); b.write16(p+50,b.read16(p+50)+1);
        if(entry==0xe94e) b.write16(0xffff91b4,b.read16(p+50));
        w(d[0],b.read16(0xffff9124)); w(d[0],d[0]<<2); r.a[0]=entry==0xe8d0 ? 0xe91e : 0xe99a;
        { auto target=r.a[0]+s(d[0]);
          if(b.read16(target)!=0x6000) throw std::runtime_error("invalid damage state branch");
          target=target+2+s(b.read16(target+2)); run(b,r,target); }
        return;
    case 0xe9ca:
        // $00E9CA-$00EA0A: first intact interval and angular-motion choice.
        if(s(b.read16(p+50))<=30) {
            run(b,r,0xf99e); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf3e2); OMainPlayer::append(b,r); return;
        }
        if(b.read8(p+34)&1) { run(b,r,0xea30); return; }
        b.write16(p+50,0); b.write16(p+52,1); b.write16(p+54,2); b.write16(0xffff9132,0xd0); b.write16(0xffff9124,1);
        [[fallthrough]];
    case 0xea10:
        // $00EA10-$00EA2E: second intact interval.
        if(s(b.read16(p+50))<=24) {
            run(b,r,0xf99e); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf3e2); OMainPlayer::append(b,r); return;
        }
        [[fallthrough]];
    case 0xea30:
        // $00EA30-$00EA34: begin short interval.
        b.write16(p+50,0); b.write16(0xffff9124,2);
        [[fallthrough]];
    case 0xea3a:
        // $00EA3A-$00EA80: short interval, then begin falling.
        if(s(b.read16(p+50))<=8) {
            run(b,r,0xf99e); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf3e2); OMainPlayer::append(b,r); return;
        }
        b.write16(p+50,0); b.write8(p+3,0xf2); b.write16(0xffff9130,0);
        if(b.read8(p+34)&1) { b.write16(0xffff9132,0xd0); b.write16(p+52,1); }
        b.write16(0xffff9124,3);
        [[fallthrough]];
    case 0xea86:
        // $00EA86-$00EAC6: fall until world Y >=320, impact palette/sound.
        if(s(b.read16(p+44))<320) {
            if(b.read8(p+34)&1) run(b,r,0xf58e);
            run(b,r,0xf99e); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf43c); OMainPlayer::append(b,r); return;
        }
        b.write16(p+50,0); b.write8(p+3,0x2c); byte(d[0],0xda); OMainSound::enqueue(b,r,true); b.write16(0xffff9124,4);
        [[fallthrough]];
    case 0xeacc:
        // $00EACC-$00EB16: roll settles inside +/-96 for sixteen ticks.
        if(s(b.read16(0x29c0ae))>96 || s(b.read16(0x29c0ae))<-96 || s(b.read16(p+50))<=16) {
            run(b,r,0xf99e); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf3e2); OMainPlayer::append(b,r); return;
        }
        b.write16(p+50,0); b.write16(0xffff9124,5);
        [[fallthrough]];
    case 0xeb1c:
        // $00EB1C-$00EB3C: thirty hidden decay ticks, then state six.
        if(s(b.read16(p+50))<=30) {
            run(b,r,0xf9ce); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf4ba); return;
        }
        b.write16(p+50,0); b.write16(0xffff9124,6);
        [[fallthrough]];
    case 0xeb42:
        // $00EB42-$00EB5C: forty-five hidden ticks before the life-flow request.
        if(s(b.read16(p+50))>45) { run(b,r,0xec62); return; }
        run(b,r,0xf9ce); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf4ba); return;
    case 0xeb5e:
        // $00EB5E-$00EB94: initial breakup, death flag at tick thirty.
        if(s(b.read16(p+50))<=60) {
            if(b.read16(p+50)==30) b.write8(0xffff911a,b.read8(0xffff911a)|2);
            run(b,r,0xf99e); run(b,r,0xf9e2); run(b,r,0xfaa0);
            if(s(b.read16(p+36))>0) { run(b,r,0xf640); OMainPlayer::append(b,r); }
            return;
        }
        // $00EB96-$00EBB2: settle bank/pitch and advance to breakup decay.
        b.write16(p+52,1); b.write16(p+54,1); b.write16(0xffff9132,0x80); b.write16(0xffff9130,0);
        b.write16(p+50,0); b.write16(0xffff9124,b.read16(0xffff9124)+1);
        [[fallthrough]];
    case 0xebb6:
        // $00EBB6-$00EBD0: breakup decay, then request life-flow state seven.
        if(s(b.read16(p+50))>75) { run(b,r,0xec62); return; }
        run(b,r,0xf9ce); run(b,r,0xf9e2); run(b,r,0xfaa0); run(b,r,0xf4ba); return;
    case 0xe744:
        // $00E744-$00E77C: RNG orientation, four entries popped from pool.
        OMainObjects::random(b,r); w(d[0],d[0]&1); w(d[0],d[0]<<2); w(d[7],d[0]);
        r.a[1]=0xffff8e7f; w(d[5],0); byte(d[5],b.read8(r.a[1]));
        if(s(d[5])<4) return;
        b.write8(r.a[1],b.read8(r.a[1])-4); w(d[5],d[5]+1); r.a[1]+=s(d[5]);
        for(unsigned i=0;i<4;++i) w(d[i],0);
        for(int i=3;i>=0;--i) { --r.a[1]; byte(d[i],b.read8(r.a[1])); }
        for(unsigned i=0;i<4;++i) w(d[i],d[i]<<6);
        // $00E77E-$00E7B8: active debris records and their waiting handlers.
        r.a[0]=0xffffa000;
        for(unsigned i=0;i<4;++i) b.write8(r.a[0]+s(d[i]),b.read8(r.a[0]+s(d[i]))|0x80);
        for(unsigned i=0;i<4;++i) b.write32(r.a[0]+s(d[i])+16,0xebd2+i*0x24);
        // $00E7BA-$00E7D8: ROM trajectory pointers, including fuselage.
        r.a[1]=0xe89e; r.a[1]=b.read32(r.a[1]+s(d[7]));
        b.write32(p+26,b.read32(r.a[1])); r.a[1]+=4;
        for(unsigned i=0;i<4;++i) { b.write32(r.a[0]+s(d[i])+26,b.read32(r.a[1])); r.a[1]+=4; }
        return;
    case 0xe7e0:
        // $00E7E0-$00E824: fixed damage depth/divisor, perspective X/Y.
        b.write16(p+36,0x400); w(d[7],0x1fc); d[1]=10;
        w(d[0],b.read16(p+4)); w(d[0],d[0]-b.read16(0x29c08a));
        d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63);
        b.write16(m+4,d[7]); b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); b.write16(p+42,d[0]);
        w(d[0],b.read16(p+6)); w(d[0],d[0]+b.read16(0x29c088)); w(d[0],d[0]-0x70);
        d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63); b.write32(m+16,d[0]); b.write16(p+44,b.read16(m+8));
        // $00E826-$00E844: signed sine/cosine from the runtime ROM.
        w(d[0],b.read16(0x29c09e)); w(d[0],d[0]&0xff); r.a[0]=0x33e98;
        byte(d[1],b.read8(r.a[0]+s(d[0]))); byte(d[2],b.read8(r.a[0]+s(d[0])-64));
        w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2])); w(d[3],d[1]); w(d[4],d[2]);
        // $00E846-$00E89C: four chip products, rotation and shared position.
        b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,b.read16(p+42)); d[2]=b.read32(m-0x3ffc);
        w(d[6],b.read16(p+44)); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[6]); d[1]=b.read32(m-0x3ffc);
        d[1]+=d[2]; asrl(d[1],7);
        b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,b.read16(p+42)); d[3]=b.read32(m-0x3ffc);
        b.write16(m-0x4000,d[4]); b.write16(m-0x3ffe,d[6]); d[4]=b.read32(m-0x3ffc);
        d[3]-=d[4]; asrl(d[3],7); w(d[3],d[3]+b.read16(0x29c080));
        b.write16(p+42,d[1]); b.write16(p+44,d[3]); b.write16(0xffff8d14,d[1]); b.write16(0xffff8d16,d[3]);
        return;
    case 0xebd2: case 0xebf6: case 0xec1a: case 0xec3e:
        // $00EBD2/$00EBF6/$00EC1A/$00EC3E: wait four damage ticks.
        if(s(b.read16(0xffff91b4))<4) return;
        b.write8(p+52,b.read8(p+52)|1); b.write16(p+36,b.read16(0xffff91b6));
        b.write32(p+16,0xf766); run(b,r,0xf766); return;
    case 0xec62:
        // $00EC62-$00EC76: hide packed scale, advance to life-flow state 7.
        b.write16(p,b.read16(p)&0xfc00); b.write8(p+3,0xf2); b.write16(p+50,0); b.write16(0xffff9124,7); return;
    case 0xecac:
        // $00ECAC-$00ECE6: recovery transition below the $EC78 lives caller.
        b.write8(0xffff911a,b.read8(0xffff911a)&~2);
        b.write16(0x29c0ae,0); b.write16(0x29c09a,0); b.write8(p,b.read8(p)&0xcf); b.write32(p+16,0x9576);
        b.write8(p-64,b.read8(p-64)&~0x80); b.write8(p+64,b.read8(p+64)&~0x80);
        b.write32(p-48,0x9d34); b.write8(0x29c200,b.read8(0x29c200)|0x40); return;
    case 0xf58e:
        // $00F58E-$00F5BE: signed angular acceleration, saturating at +/-12.
        if(s(b.read16(p+32))<0) {
            if(s(b.read16(p+32))>-12) b.write16(p+32,b.read16(p+32)-1);
        } else if(s(b.read16(p+32))<12) b.write16(p+32,b.read16(p+32)+1);
        w(d[0],b.read16(p+32)); b.write16(p+30,b.read16(p+30)+d[0]); return;
    case 0xf3e2:
        // $00F3E2-$00F43A: intact X slew, projection/frame selection/jitter.
        w(d[0],b.read16(0xffff9130)); asr(d[0],2); w(d[0],d[0]-b.read16(p+42));
        if(s(d[0])>4) d[0]=4; else if(s(d[0])<-4) d[0]=0xfffffffc;
        b.write16(p+42,b.read16(p+42)+d[0]); w(d[7],b.read16(p+36)); asr(d[7],5);
        run(b,r,0xf878); run(b,r,0xf528); run(b,r,0xf93e);
        run(b,r,b.read16(0xffff9124)==1 && s(b.read16(p+50))>=30 ? 0xf966 : 0xf95e); return;
    case 0xf43c: case 0xf4ba:
        // $00F43C/$00F4BA: falling or hidden-decay depth and perspective scale.
        if(entry==0xf43c) b.write16(p+44,b.read16(p+44)+6);
        w(d[1],b.read16(0x29c0aa)); asr(d[1],3); b.write16(p+36,b.read16(p+36)+d[1]);
        if(entry==0xf4ba) b.write16(p+36,b.read16(p+36)+0x30);
        w(d[7],b.read16(p+36)); asr(d[7],5); w(d[1],d[7]); w(d[1],d[1]+0x20);
        b.write16(m+4,d[1]); b.write32(m+16,0x1fe0); w(d[0],b.read16(m+8)); b.write8(p+2,d[0]); b.write8(p+1,d[0]);
        // $00F470/$00F4F0: depth-dependent sine and shared angular correction.
        w(d[6],d[7]); asr(d[6],2); r.a[0]=0x33e98; byte(d[6],b.read8(r.a[0]+s(d[6]))); w(d[6],static_cast<int8_t>(d[6]));
        b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c0c4)); d[6]=b.read32(m-0x3ffc); asrl(d[6],7);
        b.write16(p+42,b.read16(p+42)-d[6]); run(b,r,0xf878);
        run(b,r,entry==0xf43c && (b.read8(p+34)&1) ? 0xf5c0 : 0xf528);
        run(b,r,0xf93e); run(b,r,0xf966); return;
    case 0xf528:
        // $00F528-$00F586: bank/pitch select intact frame from runtime tables.
        if(b.read16(0xffff9124)==4) return;
        b.write8(p,b.read8(p)&~0x20); w(d[0],b.read16(p+48)); if(s(d[0])>=0) b.write8(p,b.read8(p)|0x20);
        d[0]=static_cast<int32_t>(s(d[0])); b.write16(m+4,24); b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); w(d[0],d[0]<<2);
        w(d[1],0); w(d[2],b.read16(0x29c096)); w(d[2],d[2]-b.read16(0x29c098)); b.write16(0xffff8d4e,d[2]);
        r.a[2]=0x2b9a4;
        do { const auto threshold=b.read16(r.a[2]); r.a[2]+=2; if(s(d[2])>s(threshold)) break; w(d[1],d[1]+1); } while(true);
        w(d[1],d[1]<<2); r.a[0]=0x2b9ae; r.a[0]=b.read32(r.a[0]+s(d[1])); b.write32(p+12,b.read32(r.a[0]+s(d[0]))); return;
    case 0xf5c0:
        // $00F5C0-$00F616: angular frame, mirrored quadrants and DIVU remainder.
        if(b.read16(0xffff9124)==4) return;
        r.a[0]=0x34be0; w(d[0],b.read16(p+48)); w(d[1],b.read16(p+30)); w(d[1],d[1]&0xff);
        w(d[0],d[0]+d[1]); w(d[0],d[0]&0xff); w(d[0],-d[0]); w(d[1],d[0]);
        b.write8(p,b.read8(p)&0xcf); if(d[1]&0x80) b.write8(p,b.read8(p)|0x20);
        w(d[1],d[1]+0x40); if(d[1]&0x80) b.write8(p,b.read8(p)|0x10);
        w(d[0],static_cast<int8_t>(d[0])); if(s(d[0])<0) w(d[0],-d[0]); d[0]=static_cast<int32_t>(s(d[0]));
        d[0]=((d[0]%5)<<16)|(d[0]/5); w(d[0],d[0]<<2);
        // $00F618-$00F638: pitch row then angular column.
        w(d[1],0); w(d[2],b.read16(0x29c096)); r.a[2]=0x2b9a4;
        do { const auto threshold=b.read16(r.a[2]); r.a[2]+=2; if(s(d[2])>s(threshold)) break; w(d[1],d[1]+1); } while(true);
        w(d[1],d[1]<<2); r.a[0]=b.read32(r.a[0]+s(d[1])); b.write32(p+12,b.read32(r.a[0]+s(d[0]))); return;
    case 0xf640:
        // $00F640-$00F662: fuselage X approaches one quarter of bank request.
        w(d[0],b.read16(0xffff9130)); asr(d[0],2); w(d[0],d[0]-b.read16(p+42));
        if(s(d[0])>4) d[0]=4; else if(s(d[0])<-4) d[0]=0xfffffffc;
        b.write16(p+42,b.read16(p+42)+d[0]);
        // $00F666-$00F6C0: tick-indexed eight-byte fuselage trajectory.
        b.write8(p,b.read8(p)&~0x20); w(d[0],b.read16(p+48));
        if(s(d[0])>=0) b.write8(p,b.read8(p)|0x20);
        w(d[6],0x3f);
        if(!(b.read8(p+38)&1)) {
            w(d[0],b.read16(p+50)); const int tick=s(d[0])-4; w(d[0],tick);
            // BLT follows SUBQ.W: use N==V, including signed overflow.
            if(tick>=0) {
                w(d[0],d[0]<<3); r.a[0]=b.read32(p+26);
                if(b.read16(r.a[0]+s(d[0]))==0xffff) b.write8(p+38,b.read8(p+38)|1);
                else {
                    b.write32(p+12,b.read32(r.a[0]+s(d[0]))); w(d[1],b.read16(r.a[0]+s(d[0])+6));
                    b.write16(p+36,b.read16(p+36)-d[1]); w(d[6],0); byte(d[6],b.read8(r.a[0]+s(d[0])+5));
                    b.write8(p+3,b.read8(r.a[0]+s(d[0])+4));
                }
            }
        }
        // $00F6C6-$00F706: depth quotient, chip scale product and packed zoom.
        w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<0) w(d[1],0); w(d[1],d[1]+0x20);
        b.write16(m+20,d[1]); b.write32(m+16,0x3fe0); b.write16(m-0x4000,b.read16(m+8)); b.write16(m-0x3ffe,d[6]);
        d[0]=b.read32(m-0x3ffc); asrl(d[0],6); if(s(d[0])>0x1ff) w(d[0],0x1ff);
        b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[0]); asr(d[0],1); b.write8(p+2,d[0]);
        // $00F70A-$00F728: project and publish fuselage/debris shared inputs.
        w(d[7],0x20); run(b,r,0xf878); run(b,r,0xf93e);
        b.write16(0xffff91b8,b.read16(p+42)); b.write16(0xffff91ba,b.read16(p+44));
        b.write16(0xffff91b6,b.read16(p+36)); b.write16(0xffff91bc,b.read16(p+48));
        // $00F72E-$00F754: signed byte countdown and runtime ROM sound pairs.
        { const int countdown=static_cast<int8_t>(b.read8(0xffff9118))-1;
          b.write8(0xffff9118,countdown);
          if(countdown>0) return; }
        OMainObjects::random(b,r); byte(d[1],d[0]); byte(d[1],(uint8_t(d[1])<<1)|(uint8_t(d[1])>>7));
        w(d[1],d[1]&0xe); w(d[0],d[0]&0xe);
        b.write8(0xffff9118,b.read8(0xf756+s(d[1]))); byte(d[0],b.read8(0xf757+s(d[0])));
        OMainSound::enqueue(b,r,true); return;
    case 0xf766:
        // $00F766-$00F79A: history, enabled/draw-only gates and bank flip.
        b.write32(p+8,b.read32(p+4));
        if(!(b.read8(0x29c200)&4)) { run(b,r,0xf85a); return; }
        if(b.read8(0xffff91d0)&4) { OMainPlayer::append(b,r); return; }
        b.write8(p,b.read8(p)&~0x20); if(s(b.read16(0xffff91bc))>=0) b.write8(p,b.read8(p)|0x20);
        w(d[6],0x3f); r.a[0]=b.read32(p+26);
        // $00F79A-$00F7F4: delay/end sentinels and runtime animation record.
        if(b.read16(r.a[0])==0xeeee) { add_long(b,p+26,2); return; }
        if(b.read16(r.a[0])==0xffff) { run(b,r,0xf85a); return; }
        b.write32(p+12,b.read32(r.a[0])); w(d[1],b.read16(0xffff91b6));
        { const int sum=s(d[1])+s(b.read16(r.a[0]+6)); w(d[1],sum);
          // $F7C0 BGE tests N==V after ADD.W, including signed overflow.
          if(sum<0) w(d[1],0); }
        b.write16(p+36,d[1]);
        w(d[1],b.read16(0xffff91b8)); w(d[1],d[1]+b.read16(r.a[0]+8)); b.write16(p+42,d[1]);
        w(d[1],b.read16(0xffff91ba)); w(d[1],d[1]+b.read16(r.a[0]+10)); b.write16(p+44,d[1]);
        w(d[6],0); byte(d[6],b.read8(r.a[0]+5)); b.write8(p+3,b.read8(r.a[0]+4)); add_long(b,p+26,16);
        // $00F7F6-$00F83A: depth quotient/zoom through hardware chips.
        w(d[1],b.read16(p+36)); asr(d[1],5); if(s(d[1])<0) w(d[1],0); w(d[1],d[1]+0x20);
        b.write16(m+20,d[1]); b.write32(m+16,0x3fe0); b.write16(m-0x4000,b.read16(m+8)); b.write16(m-0x3ffe,d[6]);
        d[0]=b.read32(m-0x3ffc); asrl(d[0],6); if(s(d[0])>0x1ff) w(d[0],0x1ff);
        b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[0]); asr(d[0],1); b.write8(p+2,d[0]); w(d[7],0x20);
        // $00F83E-$00F858: first projection seeds history and suppresses draw.
        run(b,r,0xf878);
        { bool first=b.read8(p+52)&1; b.write8(p+52,b.read8(p+52)&~1);
          if(first) b.write32(p+8,b.read32(p+4)); else OMainPlayer::append(b,r); }
        return;
    case 0xf85a:
        // $00F85A-$00F876: clear/remove and return slot to the debris pool.
        b.write16(p+36,0); b.write8(p+1,0); b.write8(p,b.read8(p)&~0x80); r.a[0]=0xffff8e7f;
        b.write8(r.a[0],b.read8(r.a[0])+1); w(d[0],0); byte(d[0],b.read8(r.a[0])); b.write8(r.a[0]+s(d[0]),b.read8(p+20)); return;
    case 0xf878:
        // $00F878-$00F8C4: rotated X, depth division and screen origin.
        b.write16(m-0x4000,b.read16(0x29c092)); b.write16(m-0x3ffe,b.read16(p+42)); d[0]=b.read32(m-0x3ffc);
        w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080));
        b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c090)); d[6]=b.read32(m-0x3ffc);
        d[0]+=d[6]; asrl(d[0],7); d[0]<<=5; w(d[2],d[7]); w(d[2],d[2]+0x20);
        b.write16(m+4,d[2]); b.write32(m+16,d[0]); w(d[0],b.read16(m+8)); w(d[0],d[0]+b.read16(0x29c08a)); b.write16(p+4,d[0]);
        // $00F8C6-$00F910: rotated Y, reused divisor and screen origin.
        b.write16(m-0x4000,b.read16(0x29c090)); b.write16(m-0x3ffe,b.read16(p+42)); d[0]=b.read32(m-0x3ffc);
        w(d[6],b.read16(p+44)); w(d[6],d[6]-b.read16(0x29c080));
        b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c092)); d[6]=b.read32(m-0x3ffc);
        d[0]-=d[6]; asrl(d[0],7); d[0]<<=5; b.write16(m+4,d[2]); b.write32(m+16,d[0]);
        w(d[0],b.read16(m+8)); w(d[0],d[0]+0x70); w(d[0],d[0]-b.read16(0x29c088)); b.write16(p+6,d[0]);
        // $00F912-$00F93C: bank slew capped at +/-96, shared animation bank.
        w(d[0],b.read16(0xffff9130)); w(d[0],d[0]-b.read16(p+48));
        if(s(d[0])>96) d[0]=96; else if(s(d[0])<-96) d[0]=0xffffffa0;
        b.write16(p+48,b.read16(p+48)+d[0]); b.write16(0xffff8d4c,b.read16(p+48)); return;
    case 0xf93e:
        // $00F93E-$00F95C: publish damage positions/depth to shared player state.
        for(auto pair : {std::pair<unsigned,uint32_t>{42,0xffff8d14},{44,0xffff8d16},{36,0xffff8d18},{4,0xffff8d1a},{6,0xffff8d1c}})
            b.write16(pair.second,b.read16(p+pair.first));
        return;
    case 0xf95e: case 0xf966:
        // $00F95E/$00F966-$00F99C: RNG jitter, one or three pixels per axis.
        w(d[2],entry==0xf95e ? 1 : 3); OMainObjects::random(b,r); w(d[1],d[0]); w(d[1],d[1]&d[2]);
        b.write16(p+4,b.read16(p+4)+(static_cast<int8_t>(d[0])<0 ? -d[1] : d[1]));
        byte(d[0],(uint8_t(d[0])<<4)|(uint8_t(d[0])>>4)); w(d[1],d[0]); w(d[1],d[1]&d[2]);
        b.write16(p+6,b.read16(p+6)+(static_cast<int8_t>(d[0])<0 ? -d[1] : d[1])); return;
    case 0xf99e:
        // $00F99E-$00F9CC: speed target is 1.5*request+$180, +/-4 slew.
        w(d[0],b.read16(0xffff912e)); w(d[1],d[0]); asr(d[1],1); w(d[0],d[0]+d[1]); w(d[0],d[0]+0x180);
        w(d[0],d[0]-b.read16(0x29c0aa)); if(s(d[0])>4) d[0]=4; if(s(d[0])<-4) d[0]=0xfffffffc;
        b.write16(0x29c0aa,b.read16(0x29c0aa)+d[0]); return;
    case 0xf9ce:
        // $00F9CE-$00F9E0: MULU is long, ASR acts only on the low word.
        w(d[0],b.read16(0x29c0aa)); d[0]=uint16_t(d[0])*31u; asr(d[0],5); b.write16(0x29c0aa,d[0]); return;
    case 0xf9e2:
        // $00F9E2-$00FA12: recenter residual roll in steps of eight.
        if(s(b.read16(0x29c0ae))>0) {
            auto old=b.read16(0x29c0ae); auto v=uint16_t(old-8); b.write16(0x29c0ae,v);
            // BGT uses SUBQ overflow flags, not merely the wrapped result.
            if(s(old)<=8) b.write16(0x29c0ae,0);
        } else if(s(b.read16(0x29c0ae))<0) {
            auto old=b.read16(0x29c0ae); auto v=uint16_t(old+8); b.write16(0x29c0ae,v);
            if(s(old)>=-8) b.write16(0x29c0ae,0);
        }
        // $00FA14-$00FA44: bank approaches 3/8 request, with object slew limit.
        w(d[2],b.read16(p+52)); w(d[0],b.read16(0xffff9130)); asr(d[0],2); w(d[1],d[0]); asr(d[1],1);
        w(d[0],d[0]+d[1]); w(d[0],d[0]-b.read16(0x29c0b4));
        if(s(d[0])>s(d[2])) w(d[0],d[2]);
        else { w(d[2],-d[2]); if(s(d[0])<=s(d[2])) w(d[0],d[2]); }
        b.write16(0x29c0b4,b.read16(0x29c0b4)+d[0]);
        // $00FA46-$00FA70: second bank follows by one per tick.
        w(d[0],b.read16(0x29c0b4)); w(d[0],d[0]-b.read16(0x29c0b6)); w(d[1],1);
        if(s(d[0])>s(d[1])) w(d[0],d[1]);
        else { w(d[1],-d[1]); if(s(d[0])<=s(d[1])) w(d[0],d[1]); }
        b.write16(0x29c0b6,b.read16(0x29c0b6)+d[0]);
        // $00FA72-$00FA9E: shared roll and eight-entry road history.
        w(d[0],b.read16(0x29c0b4)); w(d[0],d[0]+b.read16(0x29c0ae)); b.write16(0x29c09e,d[0]); w(d[0],d[0]+d[0]);
        r.a[0]=0x29c630; b.write16(r.a[0],b.read16(r.a[0])+2); w(d[1],b.read16(r.a[0])); w(d[1],d[1]&0xe);
        r.a[1]=0x29c620; b.write16(r.a[1]+s(d[1]),d[0]); return;
    case 0xfaa0:
        // $00FAA0-$00FACE: map pitch request and clamp to [-88,89].
        w(d[2],b.read16(p+54)); w(d[0],b.read16(0xffff9132)); w(d[0],-d[0]); w(d[0],d[0]+0xff); w(d[0],d[0]-0x80);
        if(s(d[0])<-88) w(d[0],-88); else if(s(d[0])>89) w(d[0],89);
        // $00FACE-$00FB06: preserve pitch delta, object slew, shared altitude.
        w(d[0],d[0]-b.read16(0x29c096)); b.write16(0x29c098,d[0]);
        if(s(d[0])>s(d[2])) w(d[0],d[2]);
        else { w(d[2],-d[2]); if(s(d[0])<=s(d[2])) w(d[0],d[2]); }
        b.write16(0x29c096,b.read16(0x29c096)+d[0]); w(d[0],b.read16(0x29c096)); w(d[0],d[0]+0); b.write16(0x29c094,d[0]); return;
    case 0xe7da: case 0xfbd4:
        // $00E7DA/$00FBD4: NOP, NOP, RTS.
        return;
    case 0xfb08:
        // $00FB08-$00FB14: allocate the first companion emission.
        run(b,r,0xffbe); b.write32(p+16,0xfb16); return;
    case 0xfb16:
        // $00FB16-$00FB3E: enable/pause gates and state thresholds.
        if(!(b.read8(0x29c200)&4) || (b.read8(0xffff91d0)&4)) return;
        if(s(b.read16(0xffff9124))>=7) { run(b,r,0xfbc4); return; }
        if(s(b.read16(0xffff9124))>=6) return;
        // $00FB40-$00FB98: speed-scaled unsigned quotient/remainder.
        d[0]=0; w(d[0],b.read16(0x29c0aa));
        if(b.read16(0x29c0ae)) {
            auto state=s(b.read16(0xffff9124));
            if(state>2) { if(state!=3) w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]); }
            w(d[0],d[0]+d[0]);
        } else if(s(b.read16(0xffff9124))>4) w(d[0],d[0]+d[0]);
        w(d[0],d[0]+d[0]); w(d[0],d[0]+b.read16(p+52));
        d[0]=((d[0]%0x600)<<16)|(d[0]/0x600);
        d[0]=(d[0]<<16)|(d[0]>>16); b.write16(p+52,d[0]);
        d[0]=(d[0]<<16)|(d[0]>>16);
        if(uint16_t(d[0])) run(b,r,0xfb9c);
        return;
    case 0xfb9c: case 0xffbe:
        // $00FB9C-$00FBC2 / $00FFBE-$00FFE4: pop one free slot.
        r.a[1]=0xffff8e7f; w(d[0],0); byte(d[0],b.read8(r.a[1]));
        if(!uint8_t(d[0])) return;
        r.a[0]=0xffffa000; byte(d[0],b.read8(r.a[1]+s(d[0])));
        b.write8(r.a[1],b.read8(r.a[1])-1); w(d[0],d[0]<<6); r.a[0]+=s(d[0]);
        b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write32(r.a[0]+16,entry==0xfb9c ? 0xfbda : 0xffe6); return;
    case 0xfbc4:
        // $00FBC4-$00FBD2: reset this emission pool, then idle.
        run(b,r,0x10310); b.write16(0xffff8d4a,0); b.write32(p+16,0xfbd4); return;
    case 0x10310:
        // $010310-$010346: ROM-defined pool only; no lives/HUD callee.
        r.a[0]=b.read32(0x1c5d0); w(d[1],b.read16(0x1c5d6)); w(d[1],d[1]-1);
        do { b.write8(r.a[0],b.read8(r.a[0])&~0x80); r.a[0]+=64; w(d[1],d[1]-1); } while(uint16_t(d[1])!=0xffff);
        r.a[0]=0xffff8e7f; w(d[0],b.read16(0x1c5d4)); w(d[1],b.read16(0x1c5d6));
        b.write8(r.a[0]++,d[1]); w(d[1],d[1]-1);
        do { b.write8(r.a[0]++,d[0]); w(d[0],d[0]+1); w(d[1],d[1]-1); } while(uint16_t(d[1])!=0xffff);
        return;
    case 0xfbda:
        // $00FBDA-$00FBF8: seed world coordinates from published damage inputs.
        b.write8(p,b.read8(p)&0xf3); b.write16(p+42,b.read16(0xffff8d14));
        b.write16(p+44,b.read16(0xffff8d16)); b.write16(p+36,b.read16(0xffff8d18)); b.write16(p+40,0);
        w(d[7],b.read16(p+36)); asr(d[7],5);
        // $00FBFA-$00FC90: unconditionally project both axes.
        project_companion(b,r,false);
        // $00FC94-$00FCB2: initialize frame/tick, fall into active update.
        b.write32(p+8,b.read32(p+4)); b.write8(p+3,0xe9); b.write32(p+12,0x164b0);
        b.write16(p+50,0); b.write32(p+16,0xfcb4);
        [[fallthrough]];
    case 0xfcb4: case 0xfdfa: {
        // $00FCB4/$00FDFA: history, disable/removal and draw-only gates.
        b.write32(p+8,b.read32(p+4));
        if(!(b.read8(0x29c200)&4)) { run(b,r,0x102f2); return; }
        if(b.read8(0xffff91d0)&4) { OMainPlayer::append(b,r); return; }
        bool animation=entry==0xfdfa;
        if(!animation && s(b.read16(0xffff9124))>=5) { run(b,r,0xfda6); return; }
        // $00FCDA-$00FD4A / $00FE16-$00FE74: depth, zoom and first-eight-tick ramp.
        w(d[1],b.read16(0x29c0aa));
        if(!animation) asr(d[1],b.read16(0xffff9124)==4 ? 1 : 3);
        b.write16(p+36,b.read16(p+36)-d[1]); w(d[7],b.read16(p+36)); asr(d[7],5); w(d[1],d[7]);
        if(s(d[1])<0) { run(b,r,0x102f2); return; }
        w(d[1],d[1]+0x20); b.write16(m+20,d[1]); b.write32(m+16,0x1fe0); w(d[0],b.read16(m+8));
        w(d[1],d[0]); w(d[1],d[1]+0x10); if(s(d[1])>255) w(d[1],255); b.write8(p+2,d[1]);
        if(s(b.read16(p+50))<=7) {
            b.write16(p+50,b.read16(p+50)+1); w(d[1],b.read16(p+50)); w(d[1],d[1]+8);
            d[0]=uint16_t(d[0])*uint16_t(d[1]); asr(d[0],4);
        }
        asr(d[0],1); b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|uint16_t(d[0]));
        // $00FD4C-$00FD74 / $00FE76-$00FE9E: runtime lateral table, projection.
        w(d[6],d[7]); asr(d[6],2); r.a[0]=0x33e98; byte(d[6],b.read8(r.a[0]+s(d[6]))); w(d[6],static_cast<int8_t>(d[6]));
        b.write16(m-0x4000,d[6]); b.write16(m-0x3ffe,b.read16(0x29c0c6)); d[6]=b.read32(m-0x3ffc); asrl(d[6],4);
        b.write16(p+42,b.read16(p+42)+d[6]); run(b,r,0xff08);
        // $00FD76-$00FDA4 / $00FEA0-$00FF06: vertical clipping and emission.
        if(!animation) w(d[1],0);
        w(d[1],b.read16(p)); w(d[1],d[1]&0x3ff); asr(d[1],1); w(d[1],-d[1]);
        if(s(d[0])<=s(d[1])) return;
        w(d[1],-d[1]); w(d[1],d[1]+0xe0);
        if(s(d[0])>s(d[1])) { run(b,r,0x102f2); return; }
        if(animation) {
            advance_companion(b,r);
            w(d[0],b.read16(p)); w(d[0],d[0]&0x3ff); d[0]=uint16_t(d[0])*b.read16(p+34); asrl(d[0],7);
            b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|uint16_t(d[0]));
        } else { w(d[0],b.read16(p+50)); w(d[0],d[0]+0xe8); b.write8(p+3,d[0]); }
        OMainPlayer::append(b,r); return;
    }
    case 0xff08:
        // $00FF08-$00FFBC: horizontal clipping can remove then return to caller.
        project_companion(b,r,true); return;
    case 0xfda6:
        // $00FDA6-$00FDFC: alternating sound, animation setup, first tick.
        b.write8(0xffff9118,b.read8(0xffff9118)+1); byte(d[0],b.read8(0xffff9118));
        if(!(d[0]&1)) { w(d[0],0x85); OMainSound::enqueue(b,r,true); }
        b.write16(p+30,0); r.a[0]=0x2bc34; seed_companion(b,r);
        b.write16(p+50,0); b.write32(p+16,0xfdfa); run(b,r,0xfdfa); return;
    case 0xffe6: case 0x100f4: case 0x101fe:
        // $00FFE6/$0100F4/$0101FE: screen-following animation initializers.
        b.write16(p+4,b.read16(0xffff8d1a)); b.write16(p+6,b.read16(0xffff8d1c)); b.write32(p+8,b.read32(p+4));
        b.write16(p+30,0); r.a[0]=entry==0xffe6 ? 0x2bba8 : entry==0x100f4 ? 0x2bbea : 0x2bb5e;
        seed_companion(b,r);
        if(entry==0xffe6) b.write16(p+50,0);
        w(d[0],0x85); OMainSound::enqueue(b,r,true);
        b.write32(p+16,entry==0xffe6 ? 0x1003e : entry==0x100f4 ? 0x10148 : 0x10252); return;
    case 0x100dc: case 0x101e6:
        // $0100DC-$0100F2 / $0101E6-$0101FC: wait for damage state 2 / 4.
        if(!(b.read8(0x29c200)&4)) { run(b,r,0x102f2); return; }
        if(s(b.read16(0xffff9124))>=(entry==0x100dc ? 2 : 4)) run(b,r,entry==0x100dc ? 0x100f4 : 0x101fe);
        return;
    case 0x1003e: case 0x10148: case 0x10252:
        // $01003E/$010148/$010252: history, enable and draw-only gates.
        b.write32(p+8,b.read32(p+4));
        if(!(b.read8(0x29c200)&4)) { run(b,r,0x102f2); return; }
        if(b.read8(0xffff91d0)&4) { OMainPlayer::append(b,r); return; }
        if(entry==0x10252) {
            // $01026E-$010282: continuous follow through state four.
            b.write16(p+4,b.read16(0xffff8d1a)); b.write16(p+6,b.read16(0xffff8d1c));
            if(s(b.read16(0xffff9124))>4) { run(b,r,0x102f2); return; }
        } else if(s(b.read16(p+50))>=(entry==0x1003e ? 8 : 16)) {
            // $0100CE/$0101D8: enter the next state-wait handler.
            b.write16(p+50,0); b.write32(p+16,entry==0x1003e ? 0x100dc : 0x101e6); return;
        }
        // $010064/$01016E/$010284: frame advance, parent depth and chip zoom.
        advance_companion(b,r); b.write16(p+50,b.read16(p+50)+1); r.a[0]=b.read32(0x1c5c0);
        w(d[0],0); byte(d[0],b.read8(r.a[0]+2)); w(d[0],d[0]+1); b.write8(p+2,d[0]);
        if(entry!=0x10252) asr(d[0],1);
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,b.read16(p+34)); d[0]=b.read32(m-0x3ffc); asrl(d[0],7);
        b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|uint16_t(d[0])); OMainPlayer::append(b,r); return;
    case 0x102f2:
        // $0102F2-$01030E: clear depth/zoom/active and return slot to pool.
        b.write16(p+36,0); b.write16(p,b.read16(p)&0xfc00); b.write8(p,b.read8(p)&~0x80);
        r.a[0]=0xffff8e7f; b.write8(r.a[0],b.read8(r.a[0])+1); w(d[0],0); byte(d[0],b.read8(r.a[0]));
        b.write8(r.a[0]+s(d[0]),b.read8(p+20)); return;
    case 0xec78: throw std::runtime_error("unported Phase 3e lives/HUD boundary $EC78/$CF46");
    case 0xed90: case 0xee6a: case 0xefa6: case 0xf04e:
        throw std::runtime_error("unported later damage/sequence controller state 8..11");
    default: throw std::runtime_error("unported damage entry");
    }
}

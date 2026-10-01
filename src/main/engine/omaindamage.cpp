#include "engine/omaindamage.hpp"
#include "engine/omainplayer.hpp"
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
}

void OMainDamage::run(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    switch(entry) {
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
    default: throw std::runtime_error("unported damage entry");
    }
}

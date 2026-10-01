#include "engine/omainsequence.hpp"
#include "engine/omainplayer.hpp"
#include "engine/omainweapons.hpp"
#include "engine/omainsound.hpp"
#include <stdexcept>
#include <utility>

namespace {
void w(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
void byte(uint32_t& r, uint8_t v) { r=(r&0xffffff00u)|v; }
int16_t s(uint32_t v) { return static_cast<int16_t>(v); }
void asr(uint32_t& r, unsigned n) { w(r,s(r)>>n); }
void asrl(uint32_t& r, unsigned n) { r=static_cast<int32_t>(r)>>n; }
void swap(uint32_t& r) { r=(r<<16)|(r>>16); }
}

void OMainSequence::run(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    auto ram=[](uint16_t a) { return 0xffff0000u+a; };
    auto add=[&](uint16_t a,uint16_t value) { b.write16(ram(a),b.read16(ram(a))+value); };
    auto call=[&](uint32_t target) { run(b,r,target); };
    switch(entry) {
    case 0x10348:
        // $010348-$0103C4: pools, carrier motion and runtime controller slot.
        OMainWeapons::pool(b,r,false,false); OMainWeapons::pool(b,r,true,false);
        b.write16(ram(0x9084),0xaf0); b.write16(ram(0x908a),0);
        b.write16(ram(0x9094),0xaf0); b.write16(ram(0x909a),0);
        for(auto a:{0x9082,0x9088,0x9086,0x9092,0x9098,0x9090,0x9096,0x90a2,0x90a8,0x90ae,0x90b0}) b.write16(ram(a),0);
        b.write16(ram(0x90aa),0x90); b.write32(0x29c094,0xffefffef); b.write16(ram(0x90a0),0xffff);
        r.a[0]=0xffffa000; w(d[0],b.read16(0x1c5bc)); w(d[0],d[0]<<6); r.a[0]+=s(d[0]);
        b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write32(r.a[0]+16,0x103c6); return;
    case 0x103c6:
        // $0103C6-$0103E8: wait for selector fifteen, allocate carrier parts.
        if(b.read16(0x29c09c)!=15) return;
        call(0x10b82); call(0x10ba8); b.write32(p+16,0x103ea); b.write16(p+4,10); return;
    case 0x103ea: {
        // $0103EA-$010444: speed/pitch easing then the runtime state table.
        call(0x106f6); call(0x1071c); w(d[0],b.read16(ram(0x90b0))); w(d[0],d[0]&15);
        w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]); r.a[1]=0x10408;
        auto a=r.a[1]+s(d[0]);
        if(b.read16(a)!=0x6000) throw std::runtime_error("invalid sequence state branch");
        call(a+2+s(b.read16(a+2))); return;
    }
    case 0x10448:
        // $010448-$0104B2: delay, approach speed and text request.
        { auto v=uint16_t(b.read16(p+4)-1); b.write16(p+4,v); if(s(v)>=0) return; }
        w(d[0],b.read16(ram(0x90a2))); w(d[0],-d[0]); b.write16(0x29c0aa,d[0]);
        b.write16(ram(0x90ac),s(b.read16(ram(0x9084)))>0x7d0 ? 0x100 : 0x14);
        w(d[0],b.read16(ram(0x90a0))); add(0x90a2,d[0]);
        if(s(b.read16(ram(0x90a2)))<-24) { b.write16(ram(0x90a2),-24); b.write16(ram(0x90a0),0); }
        if(s(b.read16(ram(0x9084)))<=0x250) {
            b.write16(ram(0x90ac),4); b.write16(ram(0x90ae),0); b.write16(ram(0x90a2),0); add(0x90b0,1); call(0x10782);
        } return;
    case 0x104b4: case 0x104d2:
        // $0104B4-$0104F6: two twenty-tick waits; second emits sound.
        add(0x90ae,1); if(s(b.read16(ram(0x90ae)))<20) return;
        b.write16(ram(0x90ae),0); b.write16(ram(0x9090),1); add(0x90b0,1);
        if(entry==0x104d2) { w(d[0],0xef); OMainSound::enqueue(b,r,true); } return;
    case 0x104f8:
        // $0104F8-$010516: departure acceleration.
        call(0x1069c); if(s(b.read16(ram(0x9094)))<0x320) return;
        b.write16(ram(0x90ac),0x180); b.write16(ram(0x90a0),0xffff); add(0x90b0,1); return;
    case 0x10518: case 0x1053c: case 0x10564:
        // $010518-$0105A6: three depth gates and vertical acceleration.
        if(entry==0x10564) {
            w(d[0],b.read16(ram(0x9096))); add(0x9098,d[0]);
            if(s(b.read16(ram(0x9098)))<-20) { b.write16(ram(0x9098),-20); b.write16(ram(0x9096),0); }
        }
        call(0x1069c); call(0x106ba); if(entry!=0x10518) call(0x106d8);
        if(s(b.read16(ram(0x9094)))<(entry==0x10518 ? 0x514 : entry==0x1053c ? 0x640 : 0x76c)) return;
        if(entry==0x10518) { b.write16(ram(0x90aa),0); b.write16(ram(0x9086),1); }
        else { b.write16(ram(0x9086),entry==0x1053c ? 2 : 3); b.write16(ram(0x9096),entry==0x1053c ? -1 : 2); }
        add(0x90b0,1); return;
    case 0x105a8:
        // $0105A8-$0105DA: settle vertical speed, six-tick wait.
        w(d[0],b.read16(ram(0x9096))); add(0x9098,d[0]);
        if(s(b.read16(ram(0x9098)))>=0) { b.write16(ram(0x9098),0); b.write16(ram(0x9096),0); }
        add(0x90ae,1); if(s(b.read16(ram(0x90ae)))<6) return;
        b.write16(ram(0x90ae),0); b.write16(ram(0x9098),0); b.write16(ram(0x9096),0); add(0x90b0,1); return;
    case 0x105dc:
        // $0105DC-$0105F6: return speed at depth $8E8.
        if(s(b.read16(ram(0x9094)))<0x8e8) return;
        b.write16(ram(0x90aa),0xff); b.write16(ram(0x90a0),-4); add(0x90b0,1); return;
    case 0x105f8: case 0x10634:
        // $0105F8-$010660: return-speed clamps and final depth gate.
        w(d[0],b.read16(ram(0x90a0))); add(0x90a2,d[0]);
        if((entry==0x105f8 && s(b.read16(ram(0x90a2)))<-192) ||
           (entry==0x10634 && s(b.read16(ram(0x90a2)))>-68)) {
            b.write16(ram(0x90a2),entry==0x105f8 ? -192 : -68); b.write16(ram(0x90a0),0);
        }
        call(0x1069c);
        if(entry==0x10634) { if(!b.read16(ram(0x9094))) add(0x90b0,1); }
        else if(s(b.read16(ram(0x9094)))<0x4d8) { b.write16(ram(0x90aa),0x80); b.write16(ram(0x90a0),4); add(0x90b0,1); }
        return;
    case 0x10662:
        // $010662-$010686: completion hands the object back to weapons.
        add(0x90ae,1); if(s(b.read16(ram(0x90ae)))<20) return;
        b.write16(ram(0x90ae),0); add(0x90b0,1); b.write32(p+16,0x9d34);
        b.write8(p+64,b.read8(p+64)|0x80); b.write16(ram(0x8c30),0x40);
        // $01068C-$010698: selector request, verified HUD, sequence text clear.
        // This exact request is checked here; general stage/credit flow is 3e.
        b.write16(0x29c09c,b.read16(0x29c09c)+1); OMainWeapons::missile_hud(b,r); call(0x107b6); return;
    case 0x1069c: case 0x106ba: case 0x106d8:
        // $01069C-$0106F4: word arithmetic, including $106BA's $9092 test.
        w(d[0],b.read16(ram(entry==0x1069c ? 0x9090 : entry==0x106ba ? 0x90a0 : 0x9086)));
        add(entry==0x1069c ? 0x9092 : entry==0x106ba ? 0x90a2 : 0x9088,d[0]);
        if(entry==0x1069c && s(b.read16(ram(0x9092)))>64) { b.write16(ram(0x9092),64); b.write16(ram(0x9090),0); }
        if(entry==0x106ba && s(b.read16(ram(0x9092)))<-32) { b.write16(ram(0x9092),-32); b.write16(ram(0x90a0),0); }
        if(entry==0x106d8 && s(b.read16(ram(0x9088)))<4) { b.write16(ram(0x9088),4); b.write16(ram(0x9086),0); } return;
    case 0x106f6:
        // $0106F6-$01071A: MOVEQ clamps replace the whole D0 long.
        w(d[0],b.read16(ram(0x90ac))); w(d[0],d[0]-b.read16(0x29c0aa));
        if(s(d[0])>4) d[0]=4; if(s(d[0])<-4) d[0]=uint32_t(-4);
        b.write16(0x29c0aa,b.read16(0x29c0aa)+d[0]); return;
    case 0x1071c:
        // $01071C-$010780: pitch goal, delta, asymmetric -4/+6 easing.
        w(d[0],b.read16(ram(0x90aa))); w(d[0],-d[0]); w(d[0],d[0]+0xff); w(d[0],d[0]-0x80);
        if(s(d[0])<-160) w(d[0],-160); else if(s(d[0])>89) w(d[0],89);
        w(d[0],d[0]-b.read16(0x29c096)); b.write16(0x29c098,d[0]);
        if(s(d[0])>6) w(d[0],6); else if(s(d[0])<=-4) w(d[0],-4);
        b.write16(0x29c096,b.read16(0x29c096)+d[0]); w(d[0],b.read16(0x29c096)); b.write16(0x29c094,d[0]); return;
    case 0x10782: case 0x107b6: {
        // $010782-$0107D6: ROM text blocks; zero delimiters, negative end.
        if(entry==0x10782) { byte(d[1],b.read8(0x29c076)); w(d[1],d[1]&0x38); if(uint8_t(d[1])!=0x38) return; }
        r.a[0]=0x107d8;
        for(;;) {
            byte(d[0],b.read8(r.a[0]++)); if(static_cast<int8_t>(d[0])<0) return;
            w(d[0],d[0]<<8); byte(d[0],b.read8(r.a[0]++)); r.a[1]=0xd0000+s(d[0]);
            if(entry==0x10782) {
                byte(d[0],b.read8(r.a[0]++)); w(d[0],d[0]<<8);
                for(;;) { byte(d[0],b.read8(r.a[0]++)); if(!uint8_t(d[0])) break; b.write16(r.a[1],d[0]); r.a[1]+=2; }
            } else {
                ++r.a[0]; d[0]=0x20;
                while(b.read8(r.a[0]++)) { b.write16(r.a[1],d[0]); r.a[1]+=2; }
            }
        }
    }
    case 0x10822:
        // $010822-$010846: carrier hull history, depth integration/removal.
        b.write32(p+8,b.read32(p+4)); w(d[0],b.read16(ram(0x9082))); w(d[0],d[0]+b.read16(ram(0x90a2)));
        { auto v=uint16_t(b.read16(ram(0x9084))+d[0]); add(0x9084,d[0]);
          // BGE after ADD.W observes overflow, not just the result sign.
          const auto old=uint16_t(v-d[0]); bool overflow=(~(old^uint16_t(d[0]))&(old^v)&0x8000)!=0;
          if(((v&0x8000)!=0)!=overflow && s(b.read16(p+6))>344) { call(0x10e2a); return; }
        }
        call(0x10848); OMainPlayer::append(b,r); return;
    case 0x10848:
        // $010848-$0108AA: carrier perspective and screen Y.
        w(d[1],b.read16(ram(0x9084))); if(s(d[1])<0) w(d[1],0); asr(d[1],5); w(d[1],d[1]+0x20);
        b.write32(m,0x1fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8));
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,0x190); d[1]=b.read32(m-0x3ffc); d[1]>>=8;
        b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[1]); b.write16(p+4,-4); w(d[1],d[0]);
        w(d[2],b.read16(ram(0x9088))); w(d[2],d[2]+b.read16(ram(0x90a8))); add(0x908a,d[2]);
        w(d[2],b.read16(ram(0x908a))); w(d[2],d[2]+0x40); b.write16(m-0x3ffe,d[2]);
        d[2]=b.read32(m-0x3ffc); asrl(d[2],8); w(d[2],d[2]+0x78); b.write16(p+6,d[2]); return;
    case 0x108ac:
        // $0108AC-$0108D4: hull frame and handler, position as one long.
        b.write16(p,0x8000); b.write8(p+3,0x6f); b.write8(p+2,0xd2); b.write32(p+12,0x19c70);
        b.write32(p+16,0x10822); b.write32(p+4,0xfffc0089); return;
    case 0x108d6:
        // $0108D6-$010934: twin sequence parts and their original call order.
        r.a[4]=0xffffa000; w(d[0],0); byte(d[0],b.read8(p+60)); w(d[0],d[0]<<6); r.a[4]+=s(d[0]);
        if(b.read16(ram(0x90b0))==12) { call(0x10e3e); return; }
        for(auto a:{p,r.a[4]}) { b.write16(a+8,b.read16(a+4)); b.write16(a+10,b.read16(a+6)); }
        w(d[0],b.read16(ram(0x9092))); w(d[0],d[0]+b.read16(ram(0x90a2)));
        { auto old=b.read16(ram(0x9094)), v=uint16_t(old+d[0]); add(0x9094,d[0]);
          bool overflow=(~(old^uint16_t(d[0]))&(old^v)&0x8000)!=0;
          if(((v&0x8000)!=0)!=overflow) b.write16(ram(0x9094),0);
        }
        call(0x10946); call(0x109de); call(0x10a46); b.write32(r.a[4]+12,b.read32(p+12)); call(0x10bde); OMainPlayer::append(b,r); return;
    case 0x10936:
        // $010936-$010944: secondary part hidden from state five.
        if(s(b.read16(ram(0x90b0)))<5) OMainPlayer::append(b,r); return;
    case 0x10946:
        // $010946-$0109DC: twin zoom and independently integrated Y.
        w(d[1],b.read16(ram(0x9094))); asr(d[1],5); w(d[1],d[1]+0x20);
        b.write32(m,0x1fe0); b.write16(m+20,d[1]); w(d[0],b.read16(m+8)); w(d[1],d[0]); asr(d[0],1);
        b.write8(p+1,d[0]); b.write8(r.a[4]+1,d[0]); b.write16(p+4,0); b.write16(r.a[4]+4,0);
        if(s(b.read16(ram(0x90b0)))>=8) { if(b.read16(p+6)!=0x70) b.write16(p+6,b.read16(p+6)+1); return; }
        w(d[0],d[1]); w(d[2],b.read16(ram(0x9098))); w(d[2],d[2]+b.read16(ram(0x90a8))); add(0x909a,d[2]);
        w(d[2],b.read16(ram(0x909a))); w(d[2],d[2]+0x70); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[2]);
        d[1]=b.read32(m-0x3ffc); asrl(d[1],8); w(d[1],d[1]+0x7e); b.write16(p+6,d[1]);
        w(d[2],b.read16(ram(0x908a))); w(d[2],d[2]+0x70); b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[2]);
        d[1]=b.read32(m-0x3ffc); asrl(d[1],8); w(d[1],d[1]+0x7e); asr(d[0],5); w(d[1],d[1]+d[0]); b.write16(r.a[4]+6,d[1]); return;
    case 0x109de:
        // $0109DE-$010A44: sequence palette; byte increments preserve D0.
        if(!b.read16(ram(0x90b0))) { b.write8(p+3,0xb3); return; }
        if(s(b.read16(ram(0x90b0)))>=8) { b.write8(p+3,6); return; }
        if(b.read16(ram(0x90b0))!=1) { b.write8(p+3,0xb6); return; }
        byte(d[0],0xb3);
        if(s(b.read16(ram(0x90ae)))>15) { byte(d[0],d[0]+1); if(s(b.read16(ram(0x90ae)))>20) { byte(d[0],d[0]+1); if(s(b.read16(ram(0x90ae)))>25) byte(d[0],d[0]+1); } }
        b.write8(p+3,d[0]); return;
    case 0x10a46: {
        // $010A46-$010B00: frame selection from runtime state branch table.
        w(d[0],b.read16(ram(0x90b0))); w(d[0],d[0]&15); w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]); r.a[1]=0x10a5c;
        auto a=r.a[1]+s(d[0]); if(b.read16(a)!=0x6000) throw std::runtime_error("invalid carrier frame branch");
        call(a+2+s(b.read16(a+2))); return;
    }
    case 0x10a9c: case 0x10aa8: case 0x10ab4: case 0x10ac0: case 0x10ae2:
        // $010A9C-$010B00: depth-dependent frame pointers, read at runtime.
        b.write32(p+12,entry==0x10a9c ? 0x100a0 : entry==0x10aa8 ? 0x100f0 : entry==0x10ab4 ? 0x10140 :
            entry==0x10ac0 ? (s(b.read16(ram(0x9094)))>=0x8c0 ? 0x100f0 : 0x10140) :
                            (s(b.read16(ram(0x9094)))>=0x12c ? 0x100f0 : 0x100a0)); return;
    case 0x10b02:
        // $010B02-$010B80: parent/secondary initialization and histories.
        b.write8(p,b.read8(p)&0xc3); b.write8(p+3,0xb6); b.write8(p+2,0xdf); b.write32(p+12,0x10140); b.write32(p+16,0x108d6);
        r.a[4]=p; w(d[0],0); byte(d[0],b.read8(p+60)); w(d[0],d[0]<<6); r.a[5]=0xffffa000+s(d[0]);
        b.write8(r.a[5],b.read8(r.a[5])|0x80); b.write8(r.a[5],b.read8(r.a[5])&0xc3);
        b.write8(r.a[5]+3,0x13); b.write8(r.a[5]+2,0xde); b.write32(r.a[5]+12,0x10140); b.write32(r.a[5]+16,0x10936);
        b.write16(ram(0x9094),0xaf0); b.write16(ram(0x909a),0); std::swap(r.a[4],r.a[5]); call(0x10946);
        for(auto a:{p,r.a[4]}) { b.write16(a+8,b.read16(a+4)); b.write16(a+10,b.read16(a+6)); } return;
    case 0x10b82:
        // $010B82-$010BA6: hull allocation from the vulcan pool.
        r.a[0]=0xffff8ee0; d[0]=0; byte(d[0],b.read8(r.a[0])); if(!uint8_t(d[0])) return;
        b.write8(r.a[0],b.read8(r.a[0])-1); byte(d[0],b.read8(r.a[0]+s(d[0]))); w(d[0],d[0]<<6);
        r.a[1]=0xffffa000+s(d[0]); b.write8(r.a[1],0x80); b.write32(r.a[1]+16,0x108ac); return;
    case 0x10ba8: case 0x10df4:
        // $010BA8-$010BDC / $010DF4-$010E28: two slots popped together.
        r.a[1]=0xffff8e7f; w(d[0],0); w(d[2],0); byte(d[2],b.read8(r.a[1])); if(s(d[2])<2) return;
        r.a[0]=0xffffa000; byte(d[1],b.read8(r.a[1]+s(d[2]))); byte(d[0],b.read8(r.a[1]+s(d[2])-1));
        b.write8(r.a[1],b.read8(r.a[1])-2); w(d[0],d[0]<<6); r.a[0]+=s(d[0]); b.write8(r.a[0],b.read8(r.a[0])|0x80);
        b.write8(r.a[0]+60,d[1]); b.write32(r.a[0]+16,entry==0x10ba8 ? 0x10b02 : 0x10c04); return;
    case 0x10bde:
        // $010BDE-$010C02: projected sequence coordinates, exhaust allocation.
        b.write16(ram(0x90c0),b.read16(p+4)); b.write16(ram(0x90c2),b.read16(p+6));
        if(s(b.read16(ram(0x90b0)))<8 && s(b.read16(ram(0x90b0)))>1) call(0x10df4); return;
    case 0x10c04:
        // $010C04-$010CA0: paired exhaust initializer (including original +4 histories).
        r.a[4]=0xffffa000; w(d[0],0); byte(d[0],b.read8(p+60)); w(d[0],d[0]<<6); r.a[4]+=s(d[0]);
        b.write8(p,b.read8(p)&0xf3); b.write8(r.a[4],b.read8(r.a[4])&0xf3); b.write8(p+3,10);
        std::swap(r.a[4],r.a[5]); b.write8(r.a[5]+3,10); std::swap(r.a[4],r.a[5]);
        b.write32(p+12,0x164b0); b.write32(r.a[4]+12,0x164b0); b.write32(p+16,0x10cac); b.write32(r.a[4]+16,0x10ca2);
        for(auto v:{std::pair{46,16}, {48,-1}, {50,0}, {52,1}, {54,32}, {56,16}}) b.write16(p+v.first,v.second);
        b.write16(p+36,b.read16(ram(0x9094))); b.write16(p+42,18); b.write16(p+44,b.read16(ram(0x909a))); call(0x10d46);
        for(auto a:{p,r.a[4]}) { b.write16(a+8,b.read16(a+4)); b.write16(a+10,b.read16(a+4)); }
        b.write8(r.a[4],b.read8(r.a[4])|0x80); return;
    case 0x10ca2:
        // $010CA2-$010CAA: secondary exhaust becomes append-only.
        b.write32(p+16,0x10d40); return;
    case 0x10cac:
        // $010CAC-$010D3E: three word ramps, signed depth and screen removal.
        r.a[4]=0xffffa000; w(d[0],0); byte(d[0],b.read8(p+60)); w(d[0],d[0]<<6); r.a[4]+=s(d[0]);
        for(auto a:{p,r.a[4]}) { b.write16(a+8,b.read16(a+4)); b.write16(a+10,b.read16(a+6)); }
        w(d[0],b.read16(p+48)); b.write16(p+46,b.read16(p+46)+d[0]);
        if(s(b.read16(p+46))<2) { b.write16(p+46,2); b.write16(p+48,0); }
        w(d[0],b.read16(p+56)); b.write16(p+54,b.read16(p+54)+d[0]);
        if(s(b.read16(p+54))>255) { b.write16(p+54,255); b.write16(p+56,0); }
        w(d[0],b.read16(p+52)); b.write16(p+50,b.read16(p+50)+d[0]);
        if(s(b.read16(p+50))>8) { b.write16(p+50,8); b.write16(p+52,0); }
        w(d[0],b.read16(ram(0x90a2)));
        { auto v=uint16_t(b.read16(p+36)+d[0]); b.write16(p+36,v); if(s(v)<0) { call(0x10e3e); return; } }
        call(0x10d46); if(s(b.read16(p+6))>=232) { call(0x10e3e); return; }
        [[fallthrough]];
    case 0x10d40:
        // $010D40-$010D44: emit parent/secondary request.
        OMainPlayer::append(b,r); return;
    case 0x10d46:
        // $010D46-$010DF2: twin exhaust scaling, widening and Y integration.
        w(d[0],b.read16(p+36)); asr(d[0],5); w(d[1],b.read16(p+54)); d[1]=uint16_t(d[1])*32u; w(d[0],d[0]+32);
        b.write32(m,d[1]); b.write16(m+20,d[0]); w(d[1],b.read16(m+8)); b.write8(p+1,d[1]); b.write8(r.a[4]+1,d[1]);
        asr(d[1],5); w(d[1],d[1]+0xe0); b.write8(p+2,d[1]); b.write8(r.a[4]+2,d[1]);
        w(d[0],b.read16(p+36)); asr(d[0],5); w(d[0],d[0]+32); b.write32(m,0x1fe0); b.write16(m+20,d[0]); w(d[1],b.read16(m+8));
        w(d[0],b.read16(p+50)); b.write16(p+42,b.read16(p+42)+d[0]); w(d[0],0xaf0); w(d[0],d[0]-b.read16(p+36));
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,b.read16(p+42)); b.write32(m,b.read32(m-0x3ffc)); b.write16(m+20,0xaf0);
        w(d[0],b.read16(m+8)); b.write16(p+4,d[0]); w(d[0],-d[0]); b.write16(r.a[4]+4,d[0]);
        w(d[0],b.read16(p+46)); b.write16(p+44,b.read16(p+44)+d[0]); w(d[2],b.read16(p+44)); w(d[2],d[2]+0x78);
        b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[2]); d[1]=b.read32(m-0x3ffc); asrl(d[1],8); w(d[1],d[1]+0x7e);
        b.write16(p+6,d[1]); b.write16(r.a[4]+6,d[1]); return;
    case 0x10e2a:
        // $010E2A-$010E3C: hull slot returned to vulcan pool.
        r.a[4]=0xffff8ee0; w(d[0],0); byte(d[0],b.read8(r.a[4])); b.write8(r.a[4],b.read8(r.a[4])+1); ++r.a[4]; r.a[4]+=s(d[0]);
        b.write8(r.a[4]++,b.read8(p+20)); b.write16(p,0); return;
    case 0x10e3e:
        // $010E3E-$010E4A: release both sequence parts in order.
        r.a[1]=p; call(0x10e4c); r.a[1]=r.a[4]; call(0x10e4c); return;
    case 0x10e4c:
        // $010E4C-$010E68: depth/zoom clear and missile-pool push.
        b.write16(r.a[1]+36,0); b.write8(r.a[1]+1,0); r.a[0]=0xffff8e7f; b.write8(r.a[0],b.read8(r.a[0])+1);
        w(d[0],0); byte(d[0],b.read8(r.a[0])); b.write8(r.a[0]+s(d[0]),b.read8(r.a[1]+20)); b.write8(r.a[1],b.read8(r.a[1])&~0x80); return;
    case 0x10e6a:
        // $010E6A-$010EAE: fifteen segments, not an enemy formation.
        r.a[1]=p; d[1]=14; d[0]=0x7f00e8; d[2]=121; d[3]=0xff580000; d[4]=0x17ffd0;
        do {
            b.write32(r.a[1],d[0]); b.write32(r.a[1]+16,0xb628); swap(d[3]); b.write16(r.a[1]+4,d[3]); swap(d[3]);
            b.write16(r.a[1]+6,d[2]); d[3]+=d[4]; r.a[1]+=64; w(d[1],d[1]-1);
        } while(uint16_t(d[1])!=0xffff);
        b.write8(p,0x80); b.write32(p+16,0x10eb0); return;
    case 0x10eb0:
        // $010EB0-$010ED6: shared enable flag; hidden secondary segments.
        if(!(b.read8(0x29c200)&0x20)) {
            d[0]=0; r.a[1]=p+64; d[1]=13;
            do { b.write8(r.a[1],d[0]); r.a[1]+=64; w(d[1],d[1]-1); } while(uint16_t(d[1])!=0xffff);
            return;
        }
        call(0x10fbc); call(0x10ed8); OMainPlayer::append(b,r); return;
    case 0x10ed8:
        // $010ED8-$010FBA: project the horizon endpoints, step fifteen parts.
        w(d[1],b.read16(0x29c0c2)); asr(d[1],5); w(d[1],d[1]+32); b.write32(m,0x7fe0); b.write16(m+20,d[1]);
        w(d[5],0xf0); w(d[5],d[5]-b.read16(0x29c080)); b.write16(m-0x4000,b.read16(m+8)); b.write16(m-0x3ffe,d[5]);
        d[5]=b.read32(m-0x3ffc); d[0]=10; asrl(d[5],d[0]); d[0]=0; byte(d[0],b.read8(0x29c09f)); r.a[0]=0x33e98;
        byte(d[1],b.read8(r.a[0]+s(d[0]))); byte(d[2],b.read8(r.a[0]+s(d[0])-64));
        w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2])); w(d[0],0xa8);
        b.write16(m-0x4000,d[0]); b.write16(m-0x3ffe,d[2]); d[2]=b.read32(m-0x3ffc);
        b.write16(m-0x4000,d[5]); d[3]=b.read32(m-0x3ffc); b.write16(m-0x3ffe,d[1]); d[1]=b.read32(m-0x3ffc);
        b.write16(m-0x4000,d[0]); d[4]=b.read32(m-0x3ffc); d[0]=d[1]; d[0]+=d[2]; d[1]-=d[2];
        d[5]=d[4]; d[4]-=d[3]; d[5]=-d[5]; d[5]-=d[3]; d[2]=7;
        asrl(d[0],d[2]); asrl(d[1],d[2]); asrl(d[4],d[2]); asrl(d[5],d[2]); w(d[1],d[1]-d[0]);
        w(d[0],d[0]+b.read16(0x29c08e)); swap(d[0]); w(d[0],0); w(d[5],d[5]-d[4]); w(d[4],d[4]+0x70);
        w(d[4],d[4]-b.read16(0x29c08c)); swap(d[4]); w(d[4],0); b.write16(m-0x4000,0x1249);
        b.write16(m-0x3ffe,d[1]); d[1]=b.read32(m-0x3ffc); b.write16(m-0x3ffe,d[5]); d[5]=b.read32(m-0x3ffc);
        r.a[1]=p; d[2]=14;
        do {
            b.write32(r.a[1]+8,b.read32(r.a[1]+4)); d[3]=d[0]; swap(d[4]); w(d[3],d[4]); swap(d[4]); b.write32(r.a[1]+4,d[3]);
            d[0]+=d[1]; d[4]+=d[5]; r.a[1]+=64; w(d[2],d[2]-1);
        } while(uint16_t(d[2])!=0xffff); return;
    case 0x10fbc:
        // $010FBC-$010FFE: quadrant flips and runtime frame table.
        byte(d[2],b.read8(p)); byte(d[2],d[2]&0xcf); d[0]=0; byte(d[0],b.read8(0x29c09f));
        if(static_cast<int8_t>(d[0])>=0) d[2]|=0x20; w(d[1],d[0]); byte(d[0],d[0]+0x40);
        if(static_cast<int8_t>(d[0])<0) d[2]|=0x10;
        w(d[1],d[1]&0x7e); w(d[1],d[1]+d[1]); r.a[0]=0x11000; d[0]=b.read32(r.a[0]+s(d[1])); r.a[1]=p; d[1]=14;
        do { b.write8(r.a[1],d[2]); b.write32(r.a[1]+12,d[0]); r.a[1]+=64; w(d[1],d[1]-1); } while(uint16_t(d[1])!=0xffff); return;
    case 0x161b8:
        // $0161B8-$016234: refuel compound setup uses the verified initializer.
        b.write8(0x29c200,b.read8(0x29c200)&~0x10); b.write8(0x29c200,b.read8(0x29c200)&~0x40); OMainObjects::player_prepare(b,r);
        b.write16(0xffff90e2,0); b.write16(0xffff90e0,0); b.write16(0xffff90f8,0); b.write16(0xffff90e4,0); b.write16(0xffff90e6,0);
        b.write16(0xffff90e8,30); b.write16(0xffff90ea,300); b.write16(0xffff90ec,0x80); b.write16(0xffff90ee,0x80);
        b.write16(p+56,4); b.write16(p+58,1); b.write16(0xffff912e,0x80); b.write16(0xffff9132,0x40); b.write16(0xffff9130,0);
        b.write32(p+16,0x16236); b.write8(p+2,0xff); b.write8(p-64,b.read8(p-64)&~0x80); b.write8(0xffff9102,b.read8(0xffff9102)&~1);
        b.write16(0xffff9104,b.read16(0xffff9104)+1); return;
    case 0x16236:
        // $016236 begins with stage/game-flow $E224, before any local write.
        throw std::runtime_error("unported Phase 3e refuel stage caller $16236/$E224");
    case 0x163c2:
        // $0163C2-$0163EA: signed acceleration bound clears only its increment.
        w(d[0],b.read16(0xffff90e8)); add(0x90e6,d[0]); w(d[0],b.read16(0xffff90ea));
        if(s(d[0])>0 ? s(d[0])<=s(b.read16(0xffff90e6)) : s(d[0])>=s(b.read16(0xffff90e6))) b.write16(0xffff90e8,0); return;
    case 0x163ec:
        // $0163EC-$01641A: refuel speed target; MOVEQ changes full D0.
        w(d[0],b.read16(0xffff912e)); w(d[1],d[0]); asr(d[1],1); w(d[0],d[0]+d[1]); w(d[0],d[0]+0x180); w(d[0],d[0]-b.read16(0x29c0aa));
        if(s(d[0])>2) d[0]=2; if(s(d[0])<-2) d[0]=uint32_t(-2); b.write16(0x29c0aa,b.read16(0x29c0aa)+d[0]); return;
    case 0x1641c: {
        // $01641C-$0164D8: recenter and bank slew, signed ADD/SUB flags.
        auto old=b.read16(0x29c0ae);
        if(s(old)>0) { auto value=uint16_t(old-6); b.write16(0x29c0ae,value); bool overflow=((old^6)&(old^value)&0x8000)!=0;
            if(!value || ((value&0x8000)!=0)!=overflow) b.write16(0x29c0ae,0); }
        else if(s(old)<0) { auto value=uint16_t(old+6); b.write16(0x29c0ae,value); bool overflow=(~(old^6)&(old^value)&0x8000)!=0;
            if(((value&0x8000)!=0)==overflow) b.write16(0x29c0ae,0); }
        w(d[2],b.read16(p+56)); w(d[0],b.read16(0xffff9130)); asr(d[0],2); w(d[1],d[0]); asr(d[1],1); w(d[0],d[0]+d[1]); w(d[0],d[0]-b.read16(0x29c0b4));
        if(s(d[0])>s(d[2])) w(d[0],d[2]); else { w(d[2],-d[2]); if(s(d[0])<=s(d[2])) w(d[0],d[2]); }
        b.write16(0x29c0b4,b.read16(0x29c0b4)+d[0]); w(d[0],b.read16(0x29c0b4)); w(d[0],d[0]-b.read16(0x29c0b6)); w(d[1],1);
        if(s(d[0])>s(d[1])) w(d[0],d[1]); else { w(d[1],-d[1]); if(s(d[0])<=s(d[1])) w(d[0],d[1]); }
        b.write16(0x29c0b6,b.read16(0x29c0b6)+d[0]); w(d[0],b.read16(0x29c0b4)); w(d[0],d[0]+b.read16(0x29c0ae)); b.write16(0x29c09e,d[0]);
        w(d[0],d[0]+d[0]); r.a[0]=0x29c630; b.write16(r.a[0],b.read16(r.a[0])+2); w(d[1],b.read16(r.a[0])); w(d[1],d[1]&14); r.a[1]=0x29c620; b.write16(r.a[1]+s(d[1]),d[0]); return;
    }
    case 0x164da:
        // $0164DA-$016538: original asymmetric pitch interval, no simplification.
        w(d[0],b.read16(0xffff9132)); w(d[0],-d[0]); w(d[0],d[0]+0xff); w(d[0],d[0]-0x80); w(d[0],d[0]-b.read16(0x29c096)); b.write16(0x29c098,d[0]);
        w(d[0],b.read16(0x29c0d0));
        if(uint16_t(d[0])!=b.read16(0xffff9132)) w(d[0],d[0]+(s(d[0])>s(b.read16(0xffff9132)) ? -1 : 1));
        if(s(d[0])<39 && s(d[0])>0) w(d[0],39); else if(s(d[0])>-38) w(d[0],-38);
        b.write16(0x29c0d0,d[0]); return;
    case 0x169d8: {
        // $0169D8-$016A48: palette distance and ROM frame-dependent goals.
        b.write32(p+8,b.read32(p+4));
        auto distance=[&]() { w(d[7],b.read16(0xffff90e4)); asr(d[7],3); w(d[7],d[7]-0x200); if(s(d[7])<0) w(d[7],-d[7]); asr(d[7],5); };
        distance(); if(s(d[7])>9) b.write8(p+3,0xf2); else { r.a[0]=0x16b8a; b.write8(p+3,b.read8(r.a[0]+s(d[7]))); }
        w(d[0],b.read16(0xffff90f0)); r.a[1]=0x16c36; w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]); w(d[1],0); w(d[2],0);
        w(d[1],b.read16(r.a[1]+s(d[0]))); w(d[2],b.read16(r.a[1]+s(d[0])+2)); distance(); if(s(d[7])<10) { w(d[1],0); w(d[2],-5); }
        // $016A4A-$016A98: alternating comparator-limited position updates.
        byte(d[0],b.read8(p+52));
        if(!uint8_t(d[0])) {
            w(d[0],d[1]); w(d[0],d[0]-b.read16(p+4)); b.write32(m+0x4000,0x1ffff); b.write16(m+0x4004,d[0]); w(d[0],b.read16(m+0x400e)); b.write16(p+4,b.read16(p+4)+d[0]);
            w(d[0],d[2]); w(d[0],d[0]-b.read16(p+50)); b.write16(m+0x4004,d[0]); w(d[0],b.read16(m+0x400e)); b.write16(p+50,b.read16(p+50)+d[0]);
            w(d[0],b.read16(p+50)); w(d[0],d[0]+0x70); b.write16(p+6,d[0]); b.write8(p+52,0xff);
        } else b.write8(p+52,0);
        // $016A9A-$016AF8: attitude slew, signed DIVS remainder/quotient.
        w(d[0],0); w(d[0],d[0]-b.read16(p+48)); w(d[1],6);
        if(s(d[0])>s(d[1])) w(d[0],d[1]); else { w(d[1],-d[1]); if(s(d[0])<s(d[1])) w(d[0],d[1]); }
        b.write16(p+48,b.read16(p+48)+d[0]); b.write16(0xffff8d4c,b.read16(p+48)); b.write8(p,b.read8(p)&~0x20);
        w(d[0],0); w(d[0],d[0]-b.read16(0x29c0b4)); if(s(d[0])<0) b.write8(p,b.read8(p)|0x20);
        d[0]=static_cast<int32_t>(s(d[0])); { auto value=static_cast<int32_t>(d[0]); d[0]=(uint32_t(uint16_t(value%3))<<16)|uint16_t(value/3); }
        if(s(d[0])>5) d[0]=5; else if(s(d[0])<-5) d[0]=uint32_t(-5); w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]);
        // $016AFA-$016B50: pitch profile lookup; retain runtime sentinel scan.
        w(d[1],b.read16(0xffff90fc));
        if(b.read16(0xffff90fc)!=2) {
            w(d[1],0); w(d[2],b.read16(0x29c096));
            if(s(d[2])>56) w(d[1],2);
            else {
                w(d[2],d[2]-b.read16(0x29c098)); w(d[2],d[2]+b.read16(p+50)); b.write16(0xffff8d4e,d[2]); r.a[2]=0x16b94;
                while(true) { auto threshold=b.read16(r.a[2]); r.a[2]+=2; if(s(d[2])>s(threshold)) break; w(d[1],d[1]+1); }
            }
        }
        b.write16(0xffff90fc,d[1]); w(d[1],d[1]+d[1]); w(d[1],d[1]+d[1]); r.a[0]=0x16b9e; r.a[0]=b.read32(r.a[0]+s(d[1])); b.write32(p+12,b.read32(r.a[0]+s(d[0])));
        // $016B56-$016B88: previous pitch ring and live screen coordinates.
        w(d[0],b.read16(p+6)); w(d[0],d[0]-0x70); w(d[3],b.read16(0xffff91fe)); w(d[3],d[3]&0xff); r.a[0]=0xffff9200;
        b.write16(r.a[0]+s(d[3]),d[0]); b.write16(r.a[0]+s(d[3])+2,d[2]); b.write16(0xffff91fe,b.read16(0xffff91fe)+4);
        b.write16(0xffff8d1a,b.read16(p+4)); b.write16(0xffff8d1c,b.read16(p+6)); OMainPlayer::append(b,r); return;
    }
    default: throw std::runtime_error("unported sequence entry");
    }
}

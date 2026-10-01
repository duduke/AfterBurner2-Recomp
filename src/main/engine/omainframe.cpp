#include "engine/omainframe.hpp"
bool OMainFrame::resume_logic(MainBus& b) {
    // $014856-$014878: expose BGE wait; count missed logic phases, then re-arm.
    if(int16_t(b.read16(0x2a3c00))>=0) return false;
    if(b.read16(0x2a3c00)!=0xffff) b.write16(0x3f8ba4,b.read16(0x3f8ba4)+1);
    b.write16(0x2a3c00,1);return true;
}
void OMainFrame::timers(MainBus& b) {
    // $0148BE-$0148FA: display-rate second counters, unsigned comparison.
    auto step=[&](uint32_t a) {
        uint16_t count=b.read16(a+4)+1;b.write16(a+4,count);
        if(count>0x3b) {b.write16(a+4,0);b.write32(a,b.read32(a)+1);}
    };
    step(0x3f8170);if(b.read16(0x3f880a)) step(0x3f8818);
}
void OMainFrame::buttons(MainBus& b) {
    // $01495C-$014974.
    b.write8(0x29c075,b.read8(0x29c074));b.write8(0x29c074,~b.read8(0x150001));
}
void OMainFrame::service_edge(MainBus& b) {
    // $0147E8-$014816.
    if(b.read8(0x3f8050)) return;
    uint8_t value=~b.read8(0x150001),previous=b.read8(0x3f8051);
    b.write8(0x3f8051,value);
    if(!(value&2) && (previous&2)) b.write8(0x3f8050,0xff);
}
OMainFrame::Result OMainFrame::interrupt(MainBus& b,const Call& call) {
    // $014674-$0146C4: acquire mutex and select the exact counter phase.
    b.write8(0x29c0d6,b.read8(0x29c0d6)|1);
    uint16_t previous=b.read16(0x2a3c00),count=previous-1;b.write16(0x2a3c00,count);
    // BGE uses N xor V; subtracting from $8000 overflows into $7FFF.
    bool logic=int16_t(previous)<=0;
    if(logic) {
        b.write16(0x3f8816,0);
        for(uint32_t a:{0x2a3c04u,0x2a3c14u,0x2a3c1cu,0x2a3c0cu}) {
            uint32_t second=b.read32(a+4),first=b.read32(a);
            b.write32(a+4,first);b.write32(a,second);
        }
        // $0146C6-$014728: one-shot palette requests, 3d HUD, scenery and I/O.
        call(0x14a86);call(0x13f92);
        for(auto pair:{std::pair{0x3f913du,2u},std::pair{0x3f913du,1u},std::pair{0x3f913eu,1u},std::pair{0x3f9140u,1u}}) {
            uint8_t old=b.read8(pair.first);b.write8(pair.first,old&~pair.second);
            if(old&pair.second) {
                if(pair.second==2) call(0x13bcc);
                else if(pair.first==0x3f913d) call(0x13bb6);
                else if(pair.first==0x3f913e) call(0x13bfa);
                else call(0x1329e);
            }
        }
        call(0x16c8a);call(0x18444);call(0x1495c);
        b.write8(0x140007,b.read8(0x3f8b6d));call(0x148be);call(0x17146);call(0x13e5c);
        // $014734-$01474C: interpolation is conditional on the display flag.
        if(b.read8(0x3f8b6f)&2) {call(0xb7d2);b.write8(0x110001,0xff);}
    } else {
        // $014750-$014790: alternate display phase, normal list.
        b.write16(0x3f8816,0x100);
        call(0x148be);call(0x1495c);b.write8(0x140007,b.read8(0x3f8b6d));
        call(0x14a86);call(0x13f92);call(0x18444);call(0x17146);call(0x13e5c);
        call(0xb7c2);b.write8(0x110001,0xff);
    }
    // $014798-$0147E6: release mutex, service-mode redirect or ordinary tail.
    b.write8(0x29c0d6,b.read8(0x29c0d6)&~1);
    int8_t service=b.read8(0x3f8050);
    if(service<0) {
        b.write8(0xcdd05,0);b.write8(0x3f8050,1);
        return {logic,true}; // Original RTE redirects to $01A130 with SR=$0700.
    }
    if(!service) {call(0x148b8);call(0x14990);call(0xc6f8);call(0xe2da);call(0x147e8);}
    return {logic,false};
}

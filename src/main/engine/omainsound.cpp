#include "engine/omainsound.hpp"
namespace {
void word(uint32_t& r,uint16_t v) { r=(r&0xffff0000u)|v; }
}
void OMainSound::consume(MainBus& b, OMainObjects::Registers& r) {
    // $00B7F8-$00B812: enabled queue, word cadence, odd phase, nonzero count.
    if(b.read8(0xffff8902)) {
        b.write16(0xffff8900,b.read16(0xffff8900)+1);
        if(!(b.read8(0xffff8901)&1)) return;
        if(!b.read8(0xffff8906)) return;
        // $00B816-$00B834: actual 315-5250 sound latch, then 64-byte ring wrap.
        r.a[0]=0xffff890c; word(r.d[1],b.read16(0xffff890a));
        b.write8(0xe8017,b.read8(r.a[0]+int16_t(r.d[1])));
        word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x3f);
        b.write16(0xffff890a,r.d[1]); b.write8(0xffff8906,b.read8(0xffff8906)-1);
    } else {
        // $00B836-$00B83E: disabled consumer clears its pointer and count.
        b.write16(0xffff890a,0); b.write8(0xffff8906,0);
    }
}
void OMainSound::enqueue(MainBus& b, OMainObjects::Registers& r, bool gameplay) {
    // $00B840/$00B84C-$00B85E: the gameplay entry suppresses selector 1.
    if(!b.read8(0xffff8902)) {
        // $00B87C-$00B884.
        b.write16(0xffff8908,0); b.write8(0xffff8906,0); return;
    }
    if(gameplay && b.read16(0x29c09c)==1) return;
    // $00B860-$00B87A: no invented capacity/drop rule; count wraps as a byte.
    word(r.d[1],b.read16(0xffff8908)); r.a[0]=0xffff890c;
    b.write8(r.a[0]+int16_t(r.d[1]),r.d[0]);
    word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x3f);
    b.write16(0xffff8908,r.d[1]); b.write8(0xffff8906,b.read8(0xffff8906)+1);
}

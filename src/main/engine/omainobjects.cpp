#include "engine/omainobjects.hpp"

namespace {
void word(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
uint32_t swap(uint32_t v) { return (v<<16)|(v>>16); }
}

void OMainObjects::random(MainBus& b, Registers& r) {
    // $00C5A8-$00C5BE: nonzero fallback, then two shift/add pairs.
    r.d[1]=b.read32(0xffff8802);
    if(!r.d[1]) r.d[1]=0x2a6d365a;
    r.d[0]=r.d[1]; r.d[1]<<=2; r.d[1]+=r.d[0];
    r.d[1]<<=3; r.d[1]+=r.d[0];
    // $00C5C0-$00C5CE: fold the product halves with WORD operations.
    word(r.d[0],r.d[1]); r.d[1]=swap(r.d[1]);
    word(r.d[0],r.d[0]+r.d[1]); word(r.d[1],r.d[0]);
    r.d[1]=swap(r.d[1]); b.write32(0xffff8802,r.d[1]);
}

std::vector<OMainObjects::Template> OMainObjects::inventory(MainBus& b) {
    // $01C176 is a count followed by ROM pointers to 64-byte templates.
    // Include inactive templates: a zero handler is valid in a free record.
    std::vector<Template> result;
    uint16_t count=b.read16(0x1c176);
    for(unsigned i=0;i<count;++i) {
        uint32_t p=b.read32(0x1c178+4*i);
        result.push_back({p,b.read32(p+16),b.read8(p)});
    }
    return result;
}

void OMainObjects::initialize(MainBus& b, Registers& r) {
    // $01488E-$01489C: source pointer table and sign-extended absolute.W RAM.
    r.a[0]=0x1c176; r.a[1]=0xffffa000;
    word(r.d[1],b.read16(r.a[0])); r.a[0]+=2;
    word(r.d[1],r.d[1]-1); word(r.d[0],0);
    do {
        // $01489E-$0148A8: copy exactly 32 words, with DBF's count + 1.
        r.a[2]=b.read32(r.a[0]); r.a[0]+=4;
        r.d[2]=31;
        do {
            uint16_t v=b.read16(r.a[2]); r.a[2]+=2;
            b.write16(r.a[1],v); r.a[1]+=2;
            word(r.d[2],r.d[2]-1);
        } while(uint16_t(r.d[2])!=0xffff);
        // $0148AA-$0148B4: size-table slot is byte +20, then next template.
        b.write8(r.a[1]-44,r.d[0]); word(r.d[0],r.d[0]+1);
        word(r.d[1],r.d[1]-1);
    } while(uint16_t(r.d[1])!=0xffff);
}

void OMainObjects::dispatch(MainBus& b, Registers& r, const Call& call) {
    // $014594-$01459C: $9FC0 is a sentinel BEFORE the first record.
    b.write16(0xffff8806,b.read16(0x1c176)); r.a[5]=0xffff9fc0;
    do {
        // $0145A0-$0145B0: bit 7 SET means active; pointer read is live.
        // Preserve the callee's A5: compound handlers may advance/restore it.
        r.a[5]+=64;
        if(b.read8(r.a[5])&0x80) {
            r.a[0]=b.read32(r.a[5]+16); call(r.a[0],r);
        }
        // $0145B2-$0145B8: signed BGT after word subtraction (N xor V).
        uint16_t old=b.read16(0xffff8806);
        b.write16(0xffff8806,old-1);
        if(int16_t(old)<=1) break;
    } while(true);
}

void OMainObjects::player_initialize(MainBus& b, Registers& r) {
    // $016892-$0168BE: fuselage; size-slot byte +20 is left intact.
    auto p=r.a[5];
    b.write8(p+3,0x46); b.write8(p,b.read8(p)&0xc3);
    b.write32(p+12,0x1d770); b.write32(p+16,0x1653a);
    b.write16(p+6,0xff40); b.write16(p+38,0); b.write16(p+40,0xfed8);
    r.a[4]=p; r.a[5]+=64; p=r.a[5];
    // $0168C4-$0168F8: first exhaust attachment.
    b.write8(p,b.read8(p)|0x80); b.write8(p+3,0x70);
    b.write8(p,b.read8(p)&0xc0);
    b.write32(p+12,0x1d780); b.write32(p+16,0x1656a);
    b.write16(p+8,0); b.write16(p+6,0xff40);
    b.write16(p+60,0x104); b.write16(p+62,0x2e);
    r.a[5]+=64; p=r.a[5];
    // $0168FE-$016936: second exhaust, with the horizontal-flip flag.
    b.write8(p,b.read8(p)|0x80); b.write8(p+3,0x70);
    b.write8(p,b.read8(p)&0xc0); b.write8(p,b.read8(p)|0x20);
    b.write32(p+12,0x1d780); b.write32(p+16,0x16572);
    b.write16(p+8,0); b.write16(p+6,0xff40);
    b.write16(p+60,0xff06); b.write16(p+62,0x2e);
    r.a[5]+=64; p=r.a[5];
    // $01693C-$016974: weapon pod and restored dispatcher A5.
    b.write8(p,b.read8(p)|0x80); b.write8(p+3,0x8c);
    b.write8(p,b.read8(p)&0xc0);
    b.write16(0xffff90f0,0); b.write16(0xffff90f2,0);
    b.write32(p+16,0x1657a); b.write16(p+8,0); b.write16(p+6,0xfda8);
    b.write16(p+60,0x20); b.write16(p+62,0); r.a[5]=r.a[4];
}

void OMainObjects::player_prepare(MainBus& b, Registers& r) {
    // $016976-$01698A: this is a ROM-selected object base, not slot zero.
    r.a[0]=b.read32(0x1c5c8); r.a[0]+=0x300;
    r.a[1]=r.a[0]+64; r.a[2]=r.a[1]+64; r.a[3]=r.a[2]+64;
    // $01698C-$0169B0: activate initializer and remember each size-slot byte.
    b.write16(r.a[0],0x8000); b.write32(r.a[0]+16,0x16892);
    for(unsigned i=0;i<4;++i) b.write8(0xffff90f4+i,b.read8(r.a[i]+20));
}

void OMainObjects::player_hide(MainBus& b, Registers& r) {
    // $0169B2-$0169C2: reconstruct the object address from the stored index.
    r.a[0]=0xffffa000; word(r.d[0],0);
    word(r.d[0],b.read8(0xffff90f4)); word(r.d[0],r.d[0]<<6);
    r.a[0]+=int16_t(r.d[0]); word(r.d[1],3);
    // $0169C4-$0169D6: four BCLR operations, DBF, then the player-flow flag.
    do {
        b.write8(r.a[0],b.read8(r.a[0])&0x7f); r.a[0]+=64;
        word(r.d[1],r.d[1]-1);
    } while(uint16_t(r.d[1])!=0xffff);
    b.write8(0xffff90fe,b.read8(0xffff90fe)|2);
}

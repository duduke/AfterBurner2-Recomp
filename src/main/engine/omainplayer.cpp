#include "engine/omainplayer.hpp"
#include "engine/omainsound.hpp"

namespace {
void w(uint32_t& r, uint16_t v) { r=(r&0xffff0000u)|v; }
void byte(uint32_t& r, uint8_t v) { r=(r&0xffffff00u)|v; }
int16_t s(uint32_t v) { return static_cast<int16_t>(v); }
void asr(uint32_t& r, unsigned n) { w(r,s(r)>>n); }
void asrl(uint32_t& r, unsigned n) { r=static_cast<int32_t>(r)>>n; }
uint32_t swap(uint32_t v) { return (v<<16)|(v>>16); }
}

void OMainPlayer::append(MainBus& b, OMainObjects::Registers& r) {
    // $00B628-$00B640: count/limit, sixteen-byte request, source object.
    w(r.d[0],b.read16(0x2a3cf0)); if(s(r.d[0])>=0xc0) return;
    w(r.d[0],r.d[0]<<4); r.a[0]=b.read32(0x2a3c04); r.a[0]+=s(r.d[0]); r.a[1]=r.a[5];
    // $00B642-$00B64E: fourth long's top byte becomes the size-table slot.
    for(unsigned i=0;i<3;++i) {
        uint32_t v=b.read32(r.a[1]); r.a[1]+=4; b.write32(r.a[0],v); r.a[0]+=4;
    }
    uint32_t v=b.read32(r.a[1]); r.a[1]+=4; b.write32(r.a[0],v);
    b.write8(r.a[0],b.read8(r.a[5]+20)); b.write16(0x2a3cf0,b.read16(0x2a3cf0)+1);
}

void OMainPlayer::initialize(MainBus& b, OMainObjects::Registers& r) {
    const auto p=r.a[5], m=r.a[6];
    // $009810-$009832: third compound record, flags and initial sprite.
    b.write8(0xffff911a,0); r.a[0]=p+0x80; b.write32(r.a[0]+16,0xae8c);
    b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write16(0xffff911c,0); b.write8(0xffff91c4,0);
    b.write8(p+3,0xf2); b.write32(p+12,0x100a0);
    // $00983A-$009868: logical position, previous Y, depth and divisor.
    w(r.d[0],0); b.write16(p+42,r.d[0]); b.write16(p+4,r.d[0]);
    b.write16(p+50,0); b.write16(p+44,0); w(r.d[0],b.read16(p+44)); w(r.d[0],r.d[0]+0x70);
    b.write16(p+6,r.d[0]); b.write16(p+10,r.d[0]); b.write16(p+36,0xc00);
    w(r.d[1],b.read16(p+36)); asr(r.d[1],5); w(r.d[1],r.d[1]+0x20);
    // $00986C-$00988C: depth quotient and packed flags/scale.
    b.write16(m+4,r.d[1]); b.write32(m+16,0x7fe0); w(r.d[0],b.read16(m+8));
    w(r.d[1],r.d[0]); b.write16(p+30,r.d[0]); w(r.d[0],uint16_t(r.d[0])>>1);
    b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|r.d[0]);
    asr(r.d[0],1); b.write8(p+2,r.d[0]);
    // $009890-$0098BE: shared player projection and second record handler.
    b.write16(0xffff8d14,b.read16(p+42)); b.write16(0xffff8d16,b.read16(p+44));
    b.write16(0xffff8d18,b.read16(p+36)); b.write32(p+16,0x98c0); b.write32(p+8,b.read32(p+4));
    b.write8(p+64,b.read8(p+64)|0x80); b.write32(p+80,0x9c34);
}

void OMainPlayer::update(MainBus& b, OMainObjects::Registers& r) {
    auto& d0=r.d[0]; auto& d1=r.d[1]; auto& d2=r.d[2]; auto& d3=r.d[3];
    const auto p=r.a[5], m=r.a[6];
    // $0098C0-$009900: sequence/damage transitions and collision grace timer.
    if(b.read8(0xffff90fe)&1) { b.write32(p+16,0x161b8); return; }
    if(b.read16(0x29c09a)) { b.write32(p+16,0xe662); return; }
    if(!b.read16(0xffff8d76)) {
        uint8_t flags=b.read8(0xffff8d60); b.write8(0xffff8d60,flags&~1);
        if((flags&1) && !(b.read8(0xffff89e3)&1)) {
            b.write16(0x29c09a,b.read16(0x29c09a)+1); b.write32(p+16,0xe662); return;
        }
    } else {
        b.write8(0xffff8d60,b.read8(0xffff8d60)&~1); b.write16(0xffff8d76,b.read16(0xffff8d76)-1);
    }
    // $009904-$009928: previous logical position and draw-only mode.
    b.write32(p+8,b.read32(p+4));
    if(b.read16(0x29c09c)==0xf || !(b.read8(0x29c200)&0x10)) return;
    if(b.read8(0xffff91d0)&0x10) { append(b,r); return; }
    // $00992C-$009984: slew X and Y independently, at most four per tick.
    w(d0,b.read16(0x29c0ce)); asr(d0,2); w(d0,d0-b.read16(p+4));
    if(s(d0)>4) d0=4; else if(s(d0)<-4) d0=0xfffffffc;
    b.write16(p+4,b.read16(p+4)+d0);
    w(d0,b.read16(0x29c0d0)); asr(d0,2); w(d0,d0-b.read16(p+50));
    if(s(d0)>4) d0=4; else if(s(d0)<-4) d0=0xfffffffc;
    b.write16(p+50,b.read16(p+50)+d0); w(d0,b.read16(p+50)); w(d0,d0+0x70); b.write16(p+6,d0);
    // $009988-$0099B8: animation bank slew uses signed WORD operations.
    w(d0,b.read16(0x29c0ce)); asr(d0,1); w(d1,d0); asr(d1,1); w(d0,d0+d1);
    w(d0,d0-b.read16(p+48)); w(d1,6);
    if(s(d0)>s(d1)) w(d0,d1);
    else { w(d1,-d1); if(s(d0)<s(d1)) w(d0,d1); }
    b.write16(p+48,b.read16(p+48)+d0); b.write16(0xffff8d4c,b.read16(p+48));
    // $0099BE-$009A08: flip and signed chip quotient select bank frame.
    b.write8(p,b.read8(p)&~0x20); w(d0,b.read16(0x29c0ce)); asr(d0,1); w(d0,d0-b.read16(0x29c0b4));
    if(s(d0)<0) b.write8(p,b.read8(p)|0x20);
    d0=static_cast<int32_t>(s(d0)); b.write16(m+4,3); b.write32(m+16,d0); w(d0,b.read16(m+8));
    if(s(d0)>5) d0=5; else if(s(d0)<-5) d0=0xfffffffb;
    r.a[0]=p+0x80; b.write16(r.a[0]+48,d0); w(d0,d0<<4);
    // $009A0A-$009A48: pitch thresholds and runtime sprite/attachment table.
    w(d1,0); w(d2,b.read16(0x29c096)); w(d2,d2-b.read16(0x29c098)); w(d2,d2+b.read16(p+50));
    b.write16(0xffff8d4e,d2); r.a[2]=0x28bda;
    while(true) { uint16_t v=b.read16(r.a[2]); r.a[2]+=2; if(s(d2)>s(v)) break; w(d1,d1+1); }
    r.a[0]=p+0x80; b.write16(r.a[0]+46,d1); w(d1,d1+d1); w(d1,d1+d1);
    r.a[0]=0x28c94; r.a[0]=b.read32(r.a[0]+s(d1)); r.a[0]+=s(d0);
    b.write32(p+12,b.read32(r.a[0])); r.a[0]+=4;
    // $009A4C-$009A70: reflect packed attachment offsets without undoing SWAP.
    d1=b.read32(r.a[0]); r.a[0]+=4; d2=b.read32(r.a[0]); r.a[0]+=4;
    if(b.read8(p)&0x20) { w(d1,-d1); d1=swap(d1); w(d1,-d1); }
    if(b.read8(p)&0x10) { w(d2,-d2); d2=swap(d2); w(d2,-d2); }
    b.write32(0xffff8d20,d1); b.write32(0xffff8d24,d2);
    // $009A74-$009AB4: exhaust position ring and bank/altitude products.
    w(d0,b.read16(p+6)); w(d0,d0-0x70); w(d3,b.read16(0xffff91fe)); w(d3,d3&0xff);
    r.a[0]=0xffff9200; b.write16(r.a[0]+s(d3),d0); b.write16(r.a[0]+s(d3)+2,d2);
    b.write16(0xffff91fe,b.read16(0xffff91fe)+4);
    b.write16(m-0x4000,b.read16(0xffff8d4e)); b.write16(m-0x3ffe,b.read16(0xffff8d4c));
    d0=b.read32(m-0x3ffc); asrl(d0,7); b.write16(0xffff8d62,d0);
    w(d0,b.read16(0xffff8d4e)); w(d0,d0-0x20); asr(d0,2); b.write16(0xffff8d64,d0);
    // $009AB8-$009AE0: third record inputs, projection, request emission.
    r.a[0]=p+0x80; b.write16(r.a[0]+42,b.read16(p+4)); b.write16(r.a[0]+44,b.read16(p+6));
    b.write8(r.a[0]+58,b.read8(p+2)); project(b,r);
    if(!(b.read8(0xffff8d77)&4)) append(b,r);
}

void OMainPlayer::project(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    // $009AE2-$009B08: logical coordinates and four attachment offsets.
    w(d[1],b.read16(p+4)); w(d[3],b.read16(p+6));
    b.write16(0x29c33a,d[1]); b.write16(0xffff8d1a,d[1]); b.write16(0xffff8d1c,d[3]);
    w(d[2],d[1]); w(d[4],d[3]);
    for(unsigned i=1;i<=4;++i) w(d[i],d[i]+b.read16(0xffff89d8+2*i));
    // $009B0C-$009B3A: decaying offset and speed-dependent separation.
    r.a[0]=0xffff8d5a; w(d[6],b.read16(r.a[0]));
    if(uint16_t(d[6])) { b.write16(r.a[0],b.read16(r.a[0])-1); asr(d[6],5); }
    w(d[5],b.read16(0x29c0aa)); if(s(d[5])<0x120) w(d[5],0x120);
    w(d[5],d[5]-0x220); asr(d[5],6); w(d[5],d[5]+d[6]);
    w(d[1],d[1]+d[5]); w(d[2],d[2]-d[5]); w(d[3],d[3]+d[5]); w(d[4],d[4]-d[5]);
    // $009B3C-$009B64: high-speed Y adjustment then attachment coordinates.
    if(s(b.read16(0x29c0aa))>0x220) {
        w(d[6],0x70); w(d[6],d[6]-b.read16(0xffff8d1c));
        if(s(d[6])>0) { asr(d[6],2); w(d[3],d[3]-d[6]); }
    }
    for(unsigned i=1;i<=4;++i) b.write16(0xffff8d1e + 2*i,b.read16(0xffff8d1e + 2*i)+d[i]);
    // $009B68-$009BA2: hardware perspective divide, with reused divisor.
    w(d[7],b.read16(p+30)); d[1]=10; w(d[0],b.read16(p+4)); w(d[0],d[0]-b.read16(0x29c08a));
    d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63); b.write16(m+4,d[7]); b.write32(m+16,d[0]);
    w(d[0],b.read16(m+8)); b.write16(p+42,d[0]); w(d[0],b.read16(p+6));
    w(d[0],d[0]+b.read16(0x29c088)); w(d[0],d[0]-0x70); d[0]=static_cast<int32_t>(s(d[0])); d[0]<<=(d[1]&63);
    b.write32(m+16,d[0]); b.write16(p+44,b.read16(m+8));
    // $009BA8-$009BC6: signed-byte sine/cosine from runtime ROM.
    w(d[0],b.read16(0x29c09e)); w(d[0],d[0]&0xff); r.a[0]=0x33e98;
    byte(d[1],b.read8(r.a[0]+s(d[0]))); byte(d[2],b.read8(r.a[0]+s(d[0])-64));
    w(d[1],static_cast<int8_t>(d[1])); w(d[2],static_cast<int8_t>(d[2])); w(d[3],d[1]); w(d[4],d[2]);
    // $009BC8-$009C08: four signed chip products and fixed-point rotation.
    b.write16(m-0x4000,d[2]); b.write16(m-0x3ffe,b.read16(p+42)); d[2]=b.read32(m-0x3ffc);
    w(d[6],b.read16(p+44)); b.write16(m-0x4000,d[1]); b.write16(m-0x3ffe,d[6]); d[1]=b.read32(m-0x3ffc);
    d[1]+=d[2]; asrl(d[1],7);
    b.write16(m-0x4000,d[3]); b.write16(m-0x3ffe,b.read16(p+42)); d[3]=b.read32(m-0x3ffc);
    b.write16(m-0x4000,d[4]); b.write16(m-0x3ffe,d[6]); d[4]=b.read32(m-0x3ffc);
    d[3]-=d[4]; asrl(d[3],7); w(d[3],d[3]+b.read16(0x29c080));
    // $009C0E-$009C1E: object screen position and shared history inputs.
    b.write16(p+42,d[1]); b.write16(p+44,d[3]); b.write16(0xffff8d14,d[1]); b.write16(0xffff8d16,d[3]);
}

void OMainPlayer::shadow_initialize(MainBus& b, OMainObjects::Registers& r) {
    auto p=r.a[5], m=r.a[6];
    // $009C34-$009C66: initial shadow scale, sprite and logical position.
    b.write8(p+1,0x7f); b.write8(p+3,0xf1); b.write32(p+12,0x11ea0);
    w(r.d[0],0); b.write16(p+42,r.d[0]); b.write16(p+4,r.d[0]); b.write16(p+44,0);
    w(r.d[0],b.read16(p+44)); w(r.d[0],r.d[0]+0x70); b.write16(p+6,r.d[0]); b.write16(p+10,r.d[0]);
    b.write16(p+36,0x400);
    // $009C6C-$009C9C: depth quotient, update then previous position.
    w(r.d[1],b.read16(p+36)); asr(r.d[1],5); w(r.d[1],r.d[1]+0x20);
    b.write16(m+4,r.d[1]); b.write32(m+16,0x1fe0); w(r.d[0],b.read16(m+8)); b.write8(p+2,r.d[0]);
    b.write32(p+16,0x9c9e); shadow_update(b,r); b.write32(p+8,b.read32(p+4));
}

void OMainPlayer::shadow_update(MainBus& b, OMainObjects::Registers& r) {
    auto p=r.a[5], m=r.a[6]; auto& d0=r.d[0];
    // $009C9E-$009CCC: gameplay/damage and draw-only gates.
    if(b.read16(0x29c09c)==0xf || !(b.read8(0x29c200)&0x10) || b.read16(0x29c09a)) return;
    b.write32(p+8,b.read32(p+4));
    if(b.read8(0xffff91d0)&0x10) { append(b,r); return; }
    // $009CD0-$009D02: derive shadow from parent X and height.
    r.a[4]=p-64; w(d0,b.read16(0xffff8d4e)); w(d0,d0+0x12);
    b.write16(m-0x4000,d0); b.write16(m-0x3ffe,0xae); d0=b.read32(m-0x3ffc);
    asr(d0,8); w(d0,d0+0x50); b.write16(p+6,d0); w(d0,b.read16(r.a[4]+4)); b.write16(p+4,d0);
    b.write16(0xffff8d46,b.read16(p+4)); b.write16(0xffff8d48,b.read16(p+6));
    // $009D08-$009D2A: attract has a distinct shadow frame.
    b.write32(p+12,b.read16(0x29c09c)==1 ? 0x1e5a0 : 0x11ea0); append(b,r);
}

void OMainPlayer::exhaust_initialize(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    const auto p=r.a[5];
    if(entry==0xae8c) {
        // $00AE8C-$00AEC0: activate the sibling and the attract-only tail.
        r.a[0]=p+64; b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write32(r.a[0]+16,0xaee4);
        r.a[0]+=64;
        if(b.read16(0x29c09c)==1) { b.write8(r.a[0],b.read8(r.a[0])|0x80); b.write32(r.a[0]+16,0xaf08); }
        else b.write8(r.a[0],b.read8(r.a[0])&~0x80);
        b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|0x7f); b.write8(p+3,0x80);
    } else {
        // $00AEE4-$00AF1E: sibling/tail initialization, same packed scale.
        b.write8(p+3,entry==0xaee4 ? 0x80 : 0xda);
        b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|0x7f);
        if(entry==0xaf08) {
            b.write8(p,b.read8(p)&~0x20); b.write8(p,b.read8(p)&~0x10); b.write32(p+12,0x1ef10);
        }
    }
    // $00AECE/$00AEF2/$00AF26: start offscreen, then change handler.
    b.write16(p+4,0xfec0); b.write16(p+8,0xfec0);
    b.write32(p+16,entry==0xae8c ? 0xaf3c : entry==0xaee4 ? 0xafcc : 0xb05c);
}

void OMainPlayer::exhaust_update(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    auto p=r.a[5]; auto& d=r.d;
    // $00AF3C/$00AFCC/$00B05C: remove on damage; draw while firing counter >0.
    if(b.read16(0x29c09a)) { b.write8(p,b.read8(p)&~0x80); return; }
    if(s(b.read16(0xffff8d78))<=0) return;
    if(entry==0xaf3c) b.write16(0xffff8d78,b.read16(0xffff8d78)-1);
    b.write32(p+8,b.read32(p+4));
    if(entry==0xb05c) {
        // $00B07C-$00B096: tail sprite follows the first exhaust's anchor.
        b.write8(p+2,0xfe); r.a[0]=p-128;
        w(d[0],b.read16(r.a[0]+42)); w(d[1],b.read16(r.a[0]+44));
        b.write16(p+4,d[0]); w(d[1],d[1]+0x38); b.write16(p+6,d[1]);
    } else {
        // $00AF60-$00AF86 / $00AFEC-$00B016: shared anchor/bank/frame inputs.
        const auto parent=entry==0xaf3c ? p : p-64;
        if(entry==0xafcc) r.a[0]=parent;
        byte(d[0],b.read8(parent+58)); byte(d[0],d[0]+1); b.write8(p+2,d[0]);
        w(d[1],b.read16(parent+46)); w(d[0],b.read16(parent+48));
        const bool flip=entry==0xaf3c ? s(d[0])<0 : s(d[0])<=0;
        b.write8(p,flip ? b.read8(p)|0x20 : b.read8(p)&~0x20);
        w(d[0],d[0]<<4); w(d[1],d[1]+d[1]); w(d[1],d[1]+d[1]);
        // $00AF88-$00AFB0 / $00B018-$00B040: runtime frame and attachment table.
        r.a[1]=(b.read8(0xffff8d79)&1) ? 0x29018 : 0x2939c;
        r.a[1]=b.read32(r.a[1]+s(d[1]));
        if(entry==0xaf3c) {
            b.write32(p+12,b.read32(r.a[1]+s(d[0])));
            w(d[4],b.read16(r.a[1]+s(d[0])+8)); w(d[3],b.read16(r.a[1]+s(d[0])+10));
            w(d[0],b.read16(parent+42)); w(d[1],b.read16(parent+44)); w(d[0],d[0]+d[4]);
        } else {
            b.write32(p+12,b.read32(r.a[1]+s(d[0])+4));
            w(d[2],b.read16(r.a[1]+s(d[0])+12)); w(d[3],b.read16(r.a[1]+s(d[0])+14));
            w(d[0],b.read16(parent+42)); w(d[1],b.read16(parent+44)); w(d[0],d[0]+d[2]);
        }
        b.write16(p+4,d[0]); w(d[1],d[1]+d[3]); b.write16(p+6,d[1]);
    }
    // $00B09A-$00B0A8: hidden on grace-counter bit 2.
    if(!(b.read8(0xffff8d77)&4)) append(b,r);
}

void OMainPlayer::sequence_update(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    if(entry==0x1653a) {
        // $01653A-$016562: project all four parts, copy previous positions.
        r.a[2]=r.a[5]+64; r.a[3]=r.a[2]+64; r.a[4]=r.a[3]+64; sequence_project(b,r);
        for(auto a : {r.a[5],r.a[2],r.a[3],r.a[4]}) b.write32(a+8,b.read32(a+4));
    } else if(entry==0x1657a) {
        // $01657A-$01658C: animation hold in sequence mode 3 at frame 10.
        if(b.read16(0xffff90e0)==3 && b.read16(0xffff90f0)==10) {
            // $0165CE-$0165F6: wait, then request the next sequence state.
            auto old=b.read16(0xffff90f2); b.write16(0xffff90f2,old-1);
            if(s(old)<=1) {
                b.write16(0xffff912e,0xff); b.write16(0xffff90e6,0); b.write16(0xffff90e8,4);
                b.write16(0xffff90ea,0x80); b.write16(0xffff90e2,0); b.write16(0xffff90e0,4);
                b.write16(0xffff90f0,b.read16(0xffff90f0)+1);
            }
        } else {
            // $01658C-$0165C2: signed timer decrement and ROM frame duration.
            auto old=b.read16(0xffff90f2); b.write16(0xffff90f2,old-1);
            if(s(old)<1) {
                r.a[0]=0x166ce; w(r.d[0],b.read16(0xffff90f0));
                if(uint16_t(r.d[0])==9) b.write8(0xffff9102,b.read8(0xffff9102)|1);
                if(s(r.d[0])>18) { b.write8(r.a[5],b.read8(r.a[5])&~0x80); return; }
                w(r.d[0],r.d[0]<<3); b.write16(0xffff90f2,b.read16(r.a[0]+s(r.d[0])));
                b.write16(0xffff90f0,b.read16(0xffff90f0)+1);
            }
        }
    }
    // $01656A/$016572/$0165C6/$0165FC: each active part emits its own request.
    append(b,r);
}

void OMainPlayer::sequence_project(MainBus& b, OMainObjects::Registers& r) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    // $01678E-$0167AC: sequence depth -> hardware divisor/scale.
    w(d[7],b.read16(0xffff90e4)); w(d[7],uint16_t(d[7])>>2); b.write16(p+36,d[7]);
    asr(d[7],5); w(d[1],d[7]); w(d[1],d[1]+0x20); b.write16(m+20,d[1]);
    b.write32(m+16,0x7fe0); w(d[0],b.read16(m+8)); w(d[7],d[0]); w(d[0],uint16_t(d[0])>>1);
    // $0167B4-$0167E8: packed scale and sequential depth bytes for all parts.
    for(auto a : {p,r.a[2],r.a[3],r.a[4]}) { b.write16(a,b.read16(a)&0xfc00); b.write16(a,b.read16(a)|d[0]); }
    w(d[0],uint16_t(d[0])>>1); if(uint16_t(d[0])>=0xff) d[0]=0xfffffffe;
    b.write8(p+2,d[0]); byte(d[0],d[0]-1); b.write8(r.a[2]+2,d[0]);
    byte(d[0],d[0]-1); b.write8(r.a[3]+2,d[0]); byte(d[0],d[0]+3); b.write8(r.a[4]+2,d[0]);
    // $0167EC-$016810: perspective-scaled left/right attachments.
    b.write16(m-0x3ffe,d[7]); b.write16(p+4,0xfffb); b.write16(m-0x4000,0x208);
    d[0]=b.read32(m-0x3ffc); d[0]<<=6; d[0]=swap(d[0]); w(d[1],d[0]); w(d[0],d[0]-5);
    b.write16(r.a[2]+4,d[0]); w(d[1],-d[1]); w(d[1],d[1]-5); b.write16(r.a[3]+4,d[1]);
    // $016814-$016844: fuselage/attachment height from logical Y.
    w(d[1],b.read16(p+40)); b.write16(m-0x4000,d[1]); d[0]=b.read32(m-0x3ffc);
    d[0]<<=6; d[0]=swap(d[0]); d[2]=0x70; w(d[0],d[0]+d[2]); b.write16(p+6,d[0]);
    w(d[0],d[1]); w(d[0],d[0]+0x50); b.write16(m-0x4000,d[0]); d[0]=b.read32(m-0x3ffc);
    d[0]<<=6; d[0]=swap(d[0]); w(d[0],d[0]+d[2]); b.write16(r.a[2]+6,d[0]); b.write16(r.a[3]+6,d[0]);
    // $016848-$016866: pod X from animation's runtime ROM table.
    w(d[0],b.read16(0xffff90f0)); if(s(d[0])>0) w(d[0],d[0]-1); w(d[0],d[0]+d[0]);
    r.a[1]=0x16766; b.write16(m-0x4000,b.read16(r.a[1]+s(d[0]))); d[3]=b.read32(m-0x3ffc);
    d[3]<<=6; d[3]=swap(d[3]); w(d[3],d[3]-5); b.write16(r.a[4]+4,d[3]);
    // $01686A-$016890: pod frame and perspective-scaled Y.
    r.a[1]=0x166ce; w(d[0],d[0]+d[0]); w(d[0],d[0]+d[0]);
    w(d[1],d[1]+b.read16(r.a[1]+s(d[0])+6)); b.write32(r.a[4]+12,b.read32(r.a[1]+s(d[0])+2));
    b.write16(m-0x4000,d[1]); d[0]=b.read32(m-0x3ffc); d[0]<<=6; d[0]=swap(d[0]);
    w(d[0],d[0]+d[2]); b.write16(r.a[4]+6,d[0]);
}

void OMainPlayer::recover(MainBus& b, OMainObjects::Registers& r, uint32_t entry) {
    auto& d=r.d; const auto p=r.a[5], m=r.a[6];
    // $009626-$009644: signed byte delay before activating a compound part.
    if(entry==0x9626) {
        b.write8(0xffff91c6,b.read8(0xffff91c6)+1);
        if(static_cast<int8_t>(b.read8(0xffff91c6))<=30) return;
        r.a[0]=p+128; b.write32(r.a[0]+16,0x402); b.write8(r.a[0],b.read8(r.a[0])|0x80);
        b.write32(p+16,0x964c);
    }
    // $00964C-$009682: grace timer, previous position and draw gates.
    if(b.read16(0xffff8d76)) {
        b.write8(0xffff8d60,b.read8(0xffff8d60)&~1); b.write16(0xffff8d76,b.read16(0xffff8d76)-1);
    }
    b.write32(p+8,b.read32(p+4)); if(b.read16(0x29c09c)==15 || !(b.read8(0x29c200)&0x10)) return;
    if(b.read8(0xffff91d0)&0x10) { append(b,r); return; }
    // $009686-$0096B6: complete the approach and enter ordinary live flight.
    b.write16(p+36,b.read16(p+36)+0x18);
    if(s(b.read16(p+36))>=0x400) {
        w(d[0],0xf1); OMainSound::enqueue(b,r,true);
        b.write8(0xffff91c4,b.read8(0xffff91c4)&~1); b.write8(0xffff89ec,b.read8(0xffff89ec)&~1);
        b.write8(p-64,b.read8(p-64)|0x80); b.write8(p+64,b.read8(p+64)|0x80); initialize(b,r); return;
    }
    // $0096BA-$00970A: hardware perspective and sine-derived approach scale.
    w(d[1],b.read16(p+36)); asr(d[1],5); w(d[1],d[1]+0x20);
    b.write16(m+4,d[1]); b.write32(m+16,0x1fe0); w(d[0],b.read16(m+8)); b.write8(p+2,d[0]);
    w(d[1],0x400); w(d[1],d[1]-b.read16(p+36)); w(d[2],d[1]); asr(d[2],4);
    if(s(d[2])>=0x40) w(d[3],0x7f);
    else {
        r.a[0]=0x33e98; w(d[3],0x41); w(d[3],d[3]-d[2]); w(d[4],0);
        byte(d[4],b.read8(r.a[0]+s(d[3]))); w(d[3],0x7f); w(d[3],d[3]-d[4]);
    }
    // $00970C-$00974A: approach above/below screen, packed scale and pitch.
    w(d[3],d[3]+d[3]); w(d[4],d[3]); w(d[3],d[3]+d[3]); w(d[3],d[3]+d[4]); w(d[0],d[0]+d[3]);
    b.write16(p,b.read16(p)&0xfc00); b.write16(p,b.read16(p)|d[0]); b.write16(0xffff91c8,d[0]);
    if(b.read8(p+52)&1) { w(d[4],d[4]+0x70); b.write16(p+6,d[4]); }
    else {
        w(d[4],-d[4]); w(d[4],d[4]+0x70); b.write16(p+6,d[4]);
        if(s(b.read16(p+36))>=0x200) b.write8(0xffff91c5,0x80);
    }
    // $009750-$00977C: bank cleared, divider input retains D0's upper word.
    b.write8(p,b.read8(p)&~0x20); b.write16(m+4,3); b.write32(m+16,d[0]); w(d[0],b.read16(m+8));
    r.a[0]=p+128; b.write16(r.a[0]+48,0); w(d[1],0);
    w(d[2],b.read16(0x29c096)); w(d[2],d[2]-b.read16(0x29c098)); w(d[2],d[2]+b.read16(p+50)); b.write16(0xffff8d4e,d[2]);
    // $009780-$0097A6: pitch threshold table; use its unbanked sprite pointer.
    r.a[2]=0x28bda;
    while(true) { uint16_t v=b.read16(r.a[2]); r.a[2]+=2; if(s(d[2])>s(v)) break; w(d[1],d[1]+1); }
    r.a[0]=p+128; b.write16(r.a[0]+46,d[1]); w(d[1],d[1]+d[1]); w(d[1],d[1]+d[1]);
    r.a[0]=0x28c94; r.a[0]=b.read32(r.a[0]+s(d[1])); b.write32(p+12,b.read32(r.a[0]));
    // $0097AC-$0097EC: exhaust history ring, bank product and altitude.
    w(d[0],b.read16(p+6)); w(d[0],d[0]-0x70); w(d[3],b.read16(0xffff91fe)); w(d[3],d[3]&0xff);
    r.a[0]=0xffff9200; b.write16(r.a[0]+s(d[3]),d[0]); b.write16(r.a[0]+s(d[3])+2,d[2]);
    b.write16(0xffff91fe,b.read16(0xffff91fe)+4);
    b.write16(m-0x4000,b.read16(0xffff8d4e)); b.write16(m-0x3ffe,b.read16(0xffff8d4c));
    d[0]=b.read32(m-0x3ffc); asrl(d[0],7); b.write16(0xffff8d62,d[0]);
    w(d[0],b.read16(0xffff8d4e)); w(d[0],d[0]-0x20); asr(d[0],2); b.write16(0xffff8d64,d[0]);
    // $0097F0-$00980E: third part inputs, original projection, append.
    r.a[0]=p+128; b.write16(r.a[0]+42,b.read16(p+4)); b.write16(r.a[0]+44,b.read16(p+6));
    b.write8(r.a[0]+58,b.read8(p+2)); project(b,r); append(b,r);
}

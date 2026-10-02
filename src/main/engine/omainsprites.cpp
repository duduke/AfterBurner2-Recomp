#include "engine/omainsprites.hpp"
void OMainSprites::initialize(MainBus& b) {
    // $013832-$013882: the reset routine's four pointer pairs.
    b.write32(0x2a3c08,0x29f800); b.write32(0x2a3c04,0x2a0800);
    b.write32(0x2a3c18,0x29c800); b.write32(0x2a3c14,0x29e000);
    b.write32(0x2a3c20,0x2a3cf2); b.write32(0x2a3c1c,0x2a3cf4);
    b.write32(0x2a3c10,0x2a3cf6); b.write32(0x2a3c0c,0x2a3cf8);
}
void OMainSprites::append(MainBus& b,uint32_t object) {
    // $00B628-$00B654: signed count bound, word shift and size-slot byte.
    uint16_t count=b.read16(0x2a3cf0);
    if(int16_t(count)>=0xc0) return;
    uint32_t dst=b.read32(0x2a3c04)+int16_t(uint16_t(count<<4));
    for(unsigned i=0;i<4;++i) b.write32(dst+i*4,b.read32(object+i*4));
    b.write8(dst+12,b.read8(object+20));
    b.write16(0x2a3cf0,b.read16(0x2a3cf0)+1);
}
bool OMainSprites::publish(MainBus& b) {
    OMainObjects::Registers r;
    return publish(b,r);
}
bool OMainSprites::publish(MainBus& b, OMainObjects::Registers& r) {
    // $00B676-$00B696: BGT wait is exposed to the host.
    if(int16_t(b.read16(0x2a3c00))>0) return false;
    r.a[0]=b.read32(0x2a3c0c);
    const uint32_t dst=r.a[0];
    b.write16(dst,b.read16(0x2a3cf0));
    b.write16(0x29c058,b.read16(dst)); b.write16(0x2a3cf0,0); return true;
}
void OMainSprites::blit(MainBus& b,bool interpolated) {
    // $00B7C2/$00B7D2-$00B7F6: DBF copies count+1, including terminator.
    uint32_t src=b.read32(0x2a3c14)+(interpolated?0xc00:0);
    uint32_t count_ptr=b.read32(0x2a3c1c);
    unsigned count=b.read16(count_ptr);
    for(unsigned i=0;i<=count;++i)
        for(unsigned j=0;j<4;++j) b.write32(0x100000+i*16+j*4,b.read32(src+i*16+j*4));
}

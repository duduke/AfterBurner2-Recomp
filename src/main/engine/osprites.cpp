#include "engine/osprites.hpp"
void OSprites::bind(MainBus& b,const uint8_t* r,uint32_t n) {
    bus=&b;rom=r;length=n;previous_x={};previous_y={};count=unsupported=0;
}
void OSprites::begin_frame() { count=0; }
void OSprites::append_sprite(const OEntry& obj,uint16_t bank,uint16_t offset,uint16_t pitch,
                            uint16_t,uint16_t zoom,uint16_t height,uint8_t palette,uint8_t) {
    if(!bus || !rom || !height || !zoom || count>=192) return;
    auto word=[&](uint32_t a) {return uint16_t((rom[a]<<8)|rom[a+1]);};
    // Native producers identify art by bank/offset/stride/rows. Resolve that
    // tuple to a real aligned ROM frame record; never manufacture frame data.
    uint32_t frame=0;
    for(uint32_t a=0x10000;a+16<=length;a+=16)
        if(((word(a+4)>>9)&7)==bank && word(a+6)==offset && word(a+8)==pitch && word(a+2)+1==height) {frame=a;break;}
    if(!frame) {++unsupported;return;}
    // Inverse of $0007CE's FE00/scale conversion. Native left/top coordinates
    // are adapted to $000852's center and origins. Depth truncation and
    // emission-slot history are explicit adapter limitations, not ROM AI.
    uint16_t scale=0xfe00/zoom;
    if(scale<0x40 || scale>0x3ff) {++unsupported;return;}
    uint16_t w=word(frame)*scale/0x7f,h=word(frame+2)*scale/0x7f;
    int16_t x=obj.x+(w+1)/2-(0x160-0xbe),y=obj.y+h/2;
    constexpr uint32_t scratch=0x3fa000; // temporary native record in the object table
    bus->write16(scratch,scale|(obj.control&0x2000));
    bus->write8(scratch+2,uint8_t(obj.z));bus->write8(scratch+3,palette);
    bus->write16(scratch+4,x);bus->write16(scratch+6,y);
    bus->write16(scratch+8,previous_x[count]);bus->write16(scratch+10,previous_y[count]);
    bus->write32(scratch+12,frame);bus->write8(scratch+20,uint8_t(count));
    main.append(*bus,scratch);previous_x[count]=x;previous_y[count]=y;++count;
}

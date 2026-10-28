#pragma once
#include "hwvideo/sprite_scene.hpp"

// Retained general-zoom decoder from before the unit-zoom optimization.
// Keep its accumulator traversal independent of the production fast path.
namespace SpriteDecodeReference {
using Presentation::EffectAssets;
template<class Pixel>
void decode(std::span<const uint8_t> bank,std::span<const uint8_t> rom,unsigned top,unsigned bottom,
            const EffectAssets& assets,Pixel emit,int clip_left=0,int clip_right=319,std::span<const int> anchors={}){
    if(bank.empty() || bank.size()%16 || (!anchors.empty()&&anchors.size()<bank.size()/16) || rom.empty() || rom.size()%0x40000 || top>bottom || bottom>=224 || clip_left>clip_right || clip_left < -512 || clip_right>831)throw std::invalid_argument("Invalid sprite scene source");
    for(unsigned slot=0;slot<bank.size()/16;++slot){
        std::array<uint16_t,8>d{};for(unsigned i=0;i<8;++i)d[i]=uint16_t(bank[slot*16+i*2])<<8|bank[slot*16+i*2+1];
        if(d[0]&0x8000)break;if(d[0]&0x5000)continue;
        const int ystart=(d[0]&511)-256,yd=(d[4]&0x8000)?1:-1,xd=(d[4]&0x2000)?1:-1;
        const unsigned rows=(d[5]&4095)+1;
        const int yend=ystart+yd*int(rows-1);
        const int ymin=yd>0?ystart:yend,ymax=yd>0?yend:ystart;
        if(ymax<int(top)||ymin>int(bottom))continue;
        const auto kind=assets.identify(d);
        int xs=d[2]&511;if(xs<128 && xd<0)xs+=512;xs-=190;if(!anchors.empty()&&anchors[slot]!=INT32_MIN)xs=anchors[slot];
        const unsigned hz=std::max(64u,unsigned(d[4]&2047)),vz=std::max(64u,unsigned(d[3]&2047));
        const int pitch=int16_t((d[2]>>1)|((d[4]&0x1000)<<3))>>8;
        const unsigned rb=((d[0]>>9)&7)%(rom.size()/0x40000),flags=((d[6]&255)<<4)|(((d[3]>>12)&7)<<12);
        const bool flip=(~d[4]>>14)&1;
        const uint8_t* bank_rom=rom.data()+rb*0x40000;
        const unsigned row_begin=yd>0?unsigned(std::max(0,int(top)-ystart)):unsigned(std::max(0,ystart-int(bottom)));
        const unsigned row_end=yd>0?std::min(rows-1,unsigned(int(bottom)-ystart)):std::min(rows-1,unsigned(ystart-int(top)));
        for(unsigned row=row_begin;row<=row_end;++row){int y=ystart+yd*int(row);
            uint16_t address=uint16_t(d[1]+pitch*int((row*vz)>>9));int x=xs;unsigned acc=0;
            while(xd>0?x<=clip_right:x>=clip_left){
                const size_t offset=unsigned(address)*4;
                uint32_t word=uint32_t(bank_rom[offset])|uint32_t(bank_rom[offset+1])<<8|uint32_t(bank_rom[offset+2])<<16|uint32_t(bank_rom[offset+3])<<24;
                address=uint16_t(address+(flip?-1:1));
                if(!word){while(acc<4096){x+=xd;acc+=hz;}acc-=4096;continue;}
                bool last=false;
                for(unsigned k=0;k<8;++k){const unsigned pen=(word>>(flip?k*4:(7-k)*4))&15;if(k==6)last=pen==15;
                    if(pen!=0&&pen!=15){const uint16_t raw_pixel=uint16_t(flags|pen);
                        while(acc<512){if(x>=clip_left&&x<=clip_right)emit(x,unsigned(y),raw_pixel,kind);x+=xd;acc+=hz;}
                    }else{
                        while(acc<512){x+=xd;acc+=hz;}
                    }
                    acc-=512;
                }
                if(last)break;
            }
        }
    }
}
}

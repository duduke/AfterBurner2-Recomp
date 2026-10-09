#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace ThreeScene {
struct Sprite {
    unsigned slot{}, priority{}, color{}, source_width{}, rows{}, hzoom{};
    int x{}, y{}, xdelta{}, ydelta{}, width{};
    std::vector<uint8_t> pixels;
};
// Read-only conversion of the already published X-Board list. Row-address
// stepping, nibble order, marker and zoom semantics follow hwsprites.cpp.
inline std::vector<Sprite> sprites(std::span<const uint8_t> bank, std::span<const uint8_t> rom, unsigned top, unsigned bottom) {
    if(bank.size()!=0x1000 || rom.size()<0x40000 || rom.size()%0x40000)throw std::runtime_error("invalid scene sprite source");
    std::vector<Sprite> result;
    for(unsigned slot=0;slot<256;++slot){
        std::array<uint16_t,8> d{};
        for(unsigned j=0;j<8;++j)d[j]=(unsigned(bank[16*slot+2*j])<<8)|bank[16*slot+2*j+1];
        if(d[0]&0x8000)break;
        if(d[0]&0x5000)continue;
        const int start=(d[0]&0x1ff)-0x100;
        const int yd=(d[4]&0x8000)?1:-1,xd=(d[4]&0x2000)?1:-1;
        int x=d[2]&0x1ff;if(x<0x80&&xd<0)x+=0x200;x-=0xbe;
        const unsigned rows=(d[5]&0xfff)+1, hz=std::max(0x40u,unsigned(d[4]&0x7ff)),vz=std::max(0x40u,unsigned(d[3]&0x7ff));
        const int pitch=static_cast<int16_t>((d[2]>>1)|((d[4]&0x1000)<<3))>>8;
        const unsigned gfxbank=((d[0]>>9)&7)%(rom.size()/0x40000);
        const bool flip=(~d[4]>>14)&1;
        unsigned first=0;while(first<rows&&(start+yd*int(first)<int(top)||start+yd*int(first)>int(bottom)))++first;
        if(first==rows)continue;
        unsigned end=first;while(end<rows&&start+yd*int(end)>=int(top)&&start+yd*int(end)<=int(bottom))++end;
        const int distance=xd>0?320-x:x+1;if(distance<=0)continue;
        const unsigned source_limit=(unsigned(distance)*hz+511)/512+8;
        Sprite s{slot,unsigned((d[3]>>12)&7),unsigned(d[6]&0xff),0,end-first,hz,x,start+yd*int(first),xd,yd,0,{}};
        std::vector<std::vector<uint8_t>> lines;
        for(unsigned row=first;row<end;++row){
            uint16_t address=uint16_t(d[1]+pitch*int((row*vz)>>9));
            std::vector<uint8_t> line;
            while(line.size()<source_limit){
                const size_t offset=gfxbank*0x40000+unsigned(address)*4;
                const uint32_t word=uint32_t(rom[offset])|(uint32_t(rom[offset+1])<<8)|(uint32_t(rom[offset+2])<<16)|(uint32_t(rom[offset+3])<<24);
                for(unsigned k=0;k<8;++k)line.push_back((word>>(flip?k*4:(7-k)*4))&15);
                address=uint16_t(address+(flip?-1:1));
                if(line[line.size()-2]==15)break;
            }
            s.source_width=std::max(s.source_width,unsigned(line.size()));lines.push_back(std::move(line));
        }
        s.width=int((s.source_width*512+hz-1)/hz);
        s.pixels.assign(s.source_width*s.rows,0);
        for(unsigned row=0;row<s.rows;++row)std::copy(lines[row].begin(),lines[row].end(),s.pixels.begin()+row*s.source_width);
        result.push_back(std::move(s));
    }
    return result;
}
}

#include "../tools/threejs_scene.hpp"
#include "hwvideo/hwsprites.hpp"
#include <iostream>

void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try{
    std::vector<uint8_t> rom(0x40000);
    // One bank with varying source pixels and real row terminators. Exercise
    // positive/negative pitch and both ROM nibble orders without arcade data.
    for(unsigned a=0;a<0x10000;++a){
        uint32_t word=0;for(unsigned k=0;k<8;++k)word|=uint32_t(1+(a+k)%14)<<(k*4);
        word=(word&~0x0f0000f0u)|0x0f0000f0u;
        for(unsigned b=0;b<4;++b)rom[4*a+b]=word>>(8*b);
    }
    unsigned cases=0;
    for(unsigned zoom:{0x40u,0x100u,0x200u,0x301u,0x7ffu})for(unsigned direction=0;direction<4;++direction)for(bool flip:{false,true})for(bool negative_pitch:{false,true}){
        std::array<uint8_t,0x1000> bank{};
        auto word=[&](unsigned i,unsigned v){bank[2*i]=v>>8;bank[2*i+1]=v;};
        const int pitch=negative_pitch?-1:1;
        word(0,0x100+50);word(1,0xfffe);word(2,240|((pitch&0x7f)<<9));word(3,zoom|(3<<12));
        word(4,zoom|((direction&1)?0x2000:0)|((direction&2)?0x8000:0)|(flip?0:0x4000)|(negative_pitch?0x1000:0));word(5,17);word(6,7);word(7,0xaaaa);word(8,0x8000);
        const auto original=bank;
        HWSprites native;native.init(rom.data(),rom.size());std::vector<uint16_t> expected(320*224,0xffff);
        native.render_bank(bank.data(),expected.data(),320,224,42,60);
        auto objects=ThreeScene::sprites(original,rom,42,60);
        require(original!=bank,"native fixture must exercise scaler writeback");
        std::vector<uint16_t> actual(320*224,0xffff);
        for(const auto& s:objects)for(unsigned row=0;row<s.rows;++row)for(int col=0;col<s.width;++col){
            const int x=s.x+s.xdelta*col,y=s.y+s.ydelta*int(row);const unsigned source=unsigned(col)*s.hzoom/512;
            if(x<0||x>=320||y<42||y>60||source>=s.source_width)continue;
            const auto pixel=s.pixels[row*s.source_width+source];if(pixel!=0&&pixel!=15)actual[y*320+x]=(s.priority<<12)|(s.color<<4)|pixel;
        }
        require(actual==expected,"scene source/transform differs from native hardware sprite scaler");++cases;
    }
    std::cout<<"Three.js scene extraction passed: "<<cases<<" native sprite comparisons, zoom/directions/nibble order/pitch/wrap/clip; published bank remains read-only.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

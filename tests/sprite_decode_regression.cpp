#include "hwvideo/sprite_scene.hpp"
#include "sprite_decode_reference.hpp"
#include <iostream>
#include <random>
#include <tuple>

int main()try {
    // Copyright-free asset tables. All classes resolve to a one-row frame;
    // no game ROM or captured pixels are embedded in this fixture.
    std::vector<uint8_t> main(0x35000),sub(0x18000),rom(8*0x40000);
    auto word=[](auto& bytes,unsigned a,unsigned v){bytes[a]=v>>8;bytes[a+1]=v;};
    auto pointer=[&](unsigned a,unsigned v){word(main,a,v>>16);word(main,a+2,v);};
    for(unsigned a:{0u,0x16500u,0x16550u})word(sub,a+8,1);
    pointer(0x28c64,0);
    for(unsigned i=0;i<10;++i)pointer(0x347be + i*4,0x1000);
    pointer(0x1002,0);
    for(unsigned i=0;i<2;++i)pointer(0xe89e + i*4,0x1100);
    for(unsigned i=0;i<5;++i)pointer(0x1100+i*4,0x1200);
    word(main,0x1200,0xffff);
    for(unsigned phase:{0x29018u,0x2939cu})for(unsigned i=0;i<5;++i)pointer(phase+i*4,0x1450);
    Presentation::EffectAssets assets(main,sub);
    std::mt19937 random(0x512);
    for(auto& v:rom)v=uint8_t(random());
    // Include long empty runs, address wrapping, and terminal words whose
    // eighth pen must still be emitted after the seventh pen's end marker.
    for(unsigned bank=0;bank<8;++bank){
        std::fill_n(rom.begin()+bank*0x40000,64,0);
        for(unsigned a:{16u,0xfffcu}){
            const unsigned offset=bank*0x40000+a*4;
            rom[offset]=0x1f;rom[offset+1]=0x23;rom[offset+2]=0x45;rom[offset+3]=0x67;
        }
    }
    using Pixel=std::tuple<int,unsigned,uint16_t,Presentation::Effect>;
    std::vector<Pixel> expected,actual;
    constexpr unsigned zooms[]{0,64,255,511,512,512,512,513,1024,2047};
    uint64_t pixels=0,unit_pixels=0;unsigned cases=0;
    for(unsigned n=0;n<12000;++n){
        std::vector<uint8_t> descriptors(5*16);std::array<int,5> anchors;anchors.fill(INT32_MIN);
        const auto put=[&](unsigned slot,unsigned index,unsigned value){word(descriptors,slot*16+index*2,value);};
        for(unsigned slot=0;slot<4;++slot){
            for(unsigned i=0;i<8;++i)put(slot,i,random());
            put(slot,0,((random()%8)<<9)|(random()%512));
            put(slot,1,n%3==0?0xfffc:n%3==1?0:random());
            put(slot,3,(random()&0xf800)|zooms[(n+slot)%10]);
            put(slot,4,(random()&0xf800)|zooms[n%10]);
            put(slot,5,random()%40);
            if(n%4==0)anchors[slot]=int(random()%1344)-512;
        }
        put(4,0,0x8000);
        const unsigned top=random()%224,bottom=top+random()%(224-top);
        const int left=n%3==0?-512:n%3==1?-39:0;
        const int right=n%3==0?831:n%3==1?358:319;
        const auto before=descriptors;
        expected.clear();actual.clear();
        SpriteDecodeReference::decode(descriptors,rom,top,bottom,assets,
            [&](int x,unsigned y,uint16_t raw,Presentation::Effect kind){expected.emplace_back(x,y,raw,kind);},left,right,anchors);
        Presentation::decode(descriptors,rom,top,bottom,assets,
            [&](int x,unsigned y,uint16_t raw,Presentation::Effect kind){actual.emplace_back(x,y,raw,kind);},left,right,anchors);
        if(actual!=expected||descriptors!=before)throw std::runtime_error("sprite decoder output/order/source changed at case "+std::to_string(n));
        pixels+=actual.size();if(zooms[n%10]==512)unit_pixels+=actual.size();++cases;
    }
    if(unit_pixels<10000)throw std::runtime_error("unit zoom coverage missing");
    std::cout<<"Sprite decoder: "<<cases<<" of "<<cases<<" ordered pixel streams identical, "<<pixels<<" pixels ("<<unit_pixels<<" unit zoom), 0 skipped; source descriptors unchanged\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

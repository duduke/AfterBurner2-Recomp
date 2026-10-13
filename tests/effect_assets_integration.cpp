#include "hwvideo/sprite_scene.hpp"
#include "roms.hpp"
#include <iostream>
#include <stdexcept>

void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main() try {
    require(roms.load_aburner2(),"ROMs required");
    const std::span<const uint8_t> main(roms.maincpu.rom,roms.maincpu.length),sub(roms.subcpu.rom,roms.subcpu.length);
    const auto word=[](auto data,unsigned a){return unsigned(data[a])<<8|data[a+1];};
    const auto pointer=[&](unsigned a){return (word(main,a)<<16|word(main,a+2))&0xffffff;};
    Presentation::EffectAssets assets(main,sub);unsigned fire=0,debris=0,target=0,exhaust=0,aircraft=0;
    const auto check=[&](unsigned frame,unsigned color,Presentation::Effect expected){
        for(unsigned z=0;z<64;++z){
            const auto a=frame+int16_t(word(sub,0x9e4+z*8+4));
            const auto pitch=word(sub,a+8);std::array<uint16_t,8> descriptor{};
            descriptor[0]=word(sub,a+4)&0xe00;descriptor[1]=word(sub,a+6);
            descriptor[2]=(pitch&0x7f)<<9;descriptor[4]=(pitch&0x80)?0x1000:0;descriptor[6]=color;
            require(assets.identify(descriptor)==expected,"ROM effect descriptor classification differs");
        }
    };
    for(unsigned variant=0;variant<2;++variant)for(unsigned stream=0;stream<5;++stream){
        auto a=pointer(pointer(0xe89e + variant*4)+stream*4);bool ended=false;
        for(unsigned n=0;n<256;++n){
            if(word(main,a)==0xffff){ended=true;break;}
            if(word(main,a)==0xeeee){a+=2;continue;}
            const auto color=main[a+4];
            if(color==0xa2 || color==0x75 || color==0x2c){check(pointer(a),color,Presentation::Effect::explosion);++fire;}
            else {require(color==0x15,"unreviewed crash palette");check(pointer(a),color,Presentation::Effect::opaque);++debris;}
            a+=16;
        }
        require(ended,"unterminated crash stream");
    }
    for(unsigned type=0;type<10;++type){const auto a=pointer(0x347be + type*4);
        for(unsigned n=0;n<=word(main,a);++n){const auto entry=a+2+n*8;check(pointer(entry),main[entry+4],Presentation::Effect::explosion);++target;}
    }
    for(unsigned phase=0;phase<2;++phase)for(unsigned pitch=0;pitch<5;++pitch)for(int bank=-5;bank<=5;++bank){
        const auto entry=pointer((phase?0x2939c:0x29018)+pitch*4)+bank*16;
        for(unsigned side=0;side<2;++side){check(pointer(entry+side*4),0x80,Presentation::Effect::afterburner);++exhaust;}
    }
    for(unsigned pitch=0;pitch<5;++pitch)for(int bank=-5;bank<=5;++bank){check(pointer(pointer(0x28c94+pitch*4)+bank*16),0xf2,Presentation::Effect::opaque);++aircraft;}
    // Decode a real exhaust frame: every drawn pen, including the fixed core,
    // must carry the effect. A neighbouring aircraft descriptor stays opaque.
    const auto exhaust_frame=pointer(pointer(0x29018));
    std::array<uint8_t,0x1000> list{};
    const auto put=[&](unsigned a,uint16_t value){list[a]=value>>8;list[a+1]=value;};
    put(0,0x100|(word(sub,exhaust_frame+4)&0xe00));put(2,word(sub,exhaust_frame+6));
    const auto pitch=word(sub,exhaust_frame+8);put(4,190|((pitch&0x7f)<<9));put(6,0x200);put(8,0xe200|((pitch&0x80)?0x1000:0));put(10,word(sub,exhaust_frame+2));put(12,0x80);put(16,0x8000);
    unsigned pixels=0;
    Presentation::decode(list,std::span(roms.sprites.rom,roms.sprites.length),0,223,assets,[&](unsigned,unsigned,uint16_t,Presentation::Effect kind){require(kind==Presentation::Effect::afterburner,"exhaust pixel classification differs");++pixels;});
    require(pixels && exhaust==220 && aircraft==55,"exhaust/aircraft coverage missing");
    std::cout<<"Actual exhaust: "<<exhaust<<" frame selections, "<<aircraft<<" opaque aircraft selections, "<<pixels<<" whole-flame pixels checked.\n";
    for(unsigned mode:{1u,13u,16u,20u})for(unsigned throttle:{0u,0x80u,0xc0u,0xfcu,0xfdu,0xffu,0xffffu})
        require(Presentation::full_throttle_flame(mode,throttle)==(mode==16 && (throttle==0xfd || throttle==0xff)),"full-throttle flame predicate differs");
    require(fire && debris && target,"effect assets lack fire/debris/target coverage");
    std::cout<<"Effect assets: "<<fire<<" crash fire/smoke, "<<debris<<" opaque aircraft debris, "<<target<<" target animation frames; all 64 LOD entries identical, 0 skipped; throttle boundaries passed.\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

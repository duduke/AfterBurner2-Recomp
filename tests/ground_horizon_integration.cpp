#include "engine/game_session.hpp"
#include "frontend/sprite_filter.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <chrono>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void image(const std::string& path,std::span<const uint32_t> pixels){std::ofstream f(path,std::ios::binary);f<<"P6\n320 224\n255\n";for(auto p:pixels)for(unsigned s:{16u,8u,0u})f.put(char(p>>s));}
int main()try {
    std::filesystem::create_directories("build/oracle/horizon");
    require(roms.load_aburner2(),"ROMs required");GameSession original(roms),enhanced(roms);original.capture_sprite_presentation(true);enhanced.capture_ground_horizon(true);
    Presentation::GroundHorizon family(std::span(roms.maincpu.rom,roms.maincpu.length),std::span(roms.subcpu.rom,roms.subcpu.length));
    const auto word=[](auto data,unsigned a){return unsigned(data[a])<<8|data[a+1];};
    require(family.normal(0)==std::array<int,2>{0,128},"ROM level-flight normal points at sky");
    require(family.normal(64)==std::array<int,2>{127,0},"ROM quarter-roll normal differs");
    require(family.normal(128)==std::array<int,2>{-1,-127},"ROM inverted normal differs");
    require(family.normal(192)==std::array<int,2>{-128,1},"ROM opposite quarter-roll normal differs");
    unsigned assets_checked=0;
    for(unsigned angle=0;angle<64;++angle)for(unsigned lod=0;lod<64;++lod){auto m=roms.maincpu.rom,s=roms.subcpu.rom;
        const auto a=((word(m,0x11000+4*angle)<<16)|word(m,0x11002+4*angle))+int16_t(word(s,0x9e4+lod*8+4));
        const auto pitch=word(s,a+8);std::array<uint16_t,8>d{};d[0]=word(s,a+4)&0xe00;d[1]=word(s,a+6);d[6]=0xe8;d[2]=(pitch&0x7f)<<9;d[4]=(pitch&0x80)?0x1000:0;
        require(family.matches(d),"ROM horizon family classification differs");++assets_checked;
    }
    Frontend::SpriteScaler scaler;uint64_t changed=0,frames=0,visible_changes=0;unsigned left=0,right=0,best=0,sample_frame=0;bool saved=false;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned f=0;f<1560;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;if(f>=600){c.stick_x=((f-600)/60)%2?1:-1;c.throttle=f<1080?1:0;c.cannon=c.missile=true;}
        original.advance(c);enhanced.advance(c);const auto a=original.frame(),b=enhanced.frame();
        require(a.info.number==b.info.number && a.info.time_ns==b.info.time_ns,"ground tap changed native time");
        require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"ground tap changed native RGB");
        require(original.take_audio()==enhanced.take_audio(),"ground tap changed native PCM");
        const auto p=original.sprite_presentation(),q=enhanced.sprite_presentation();require(p.ready&&q.ready,"ground tap presentation missing");
        require(std::equal(p.protected_pixels.begin(),p.protected_pixels.end(),q.protected_pixels.begin()),"ground tap changed layer protection");
        require(std::equal(p.sprites.begin(),p.sprites.end(),q.sprites.begin()),"ground tap changed aircraft/sprite pixels");
        unsigned delta=0;for(unsigned i=0;i<p.background.size();++i)if(p.background[i]!=q.background[i]){require(!q.protected_pixels[i],"ground correction changed foreground");++delta;}
        if(f==1155 || f==1275 || f==1338){const auto stem="build/oracle/horizon/revised-"+std::to_string(f);
            image(stem+"-before.ppm",scaler.compose(p.background,p.sprites,320,224,Frontend::SpriteFilter::none,p.protected_pixels));
            image(stem+"-after.ppm",scaler.compose(q.background,q.sprites,320,224,Frontend::SpriteFilter::none,q.protected_pixels));}
        if(delta){changed+=delta;++frames;if(c.stick_x<0)++left;else if(c.stick_x>0)++right;}
        unsigned visible=0;for(unsigned i=0;i<p.background.size();++i)if(!q.sprites[i] && !q.protected_pixels[i] && p.background[i]!=q.background[i])++visible;
        visible_changes+=visible;
        if(f>=600 && visible>std::max(10u,best)){best=visible;sample_frame=f;auto before=scaler.compose(p.background,p.sprites,320,224,Frontend::SpriteFilter::none,p.protected_pixels);image("build/oracle/horizon/ground-before.ppm",before);auto after=scaler.compose(q.background,q.sprites,320,224,Frontend::SpriteFilter::none,q.protected_pixels);image("build/oracle/horizon/ground-after.ppm",after);std::ofstream fixture("build/oracle/horizon/ground-plane.bin",std::ios::binary);fixture.write(reinterpret_cast<const char*>(q.background.data()),q.background.size()*4);fixture.write(reinterpret_cast<const char*>(q.sprites.data()),q.sprites.size()*4);fixture.write(reinterpret_cast<const char*>(q.protected_pixels.data()),q.protected_pixels.size());saved=true;}
    }
    require(changed>10000&&visible_changes>1000&&left&&right&&saved,"ground roll coverage missing");
    enhanced.capture_ground_horizon(false);for(unsigned f=0;f<10;++f){original.advance({});enhanced.advance({});auto p=original.sprite_presentation(),q=enhanced.sprite_presentation();require(std::equal(p.background.begin(),p.background.end(),q.background.begin()),"ground disable retained correction");original.take_audio();enhanced.take_audio();}
    original.restart();enhanced.restart();enhanced.capture_ground_horizon(true);for(unsigned f=0;f<20;++f){original.advance({});enhanced.advance({});auto p=original.sprite_presentation(),q=enhanced.sprite_presentation();require(std::equal(p.background.begin(),p.background.end(),q.background.begin()),"ground correction changed cold boot");original.take_audio();enhanced.take_audio();}
    std::cout<<"Ground correction: 1560 native RGB/PCM/time frames identical, 0 skipped; "<<changed<<" background pixels ("<<visible_changes<<" visibly exposed) across "<<frames<<" frames; left/right="<<left<<'/'<<right<<"; "<<assets_checked<<" ROM asset/LOD selections; disable/restart passed; sample="<<sample_frame<<"; "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

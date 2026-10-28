#include "engine/game_session.hpp"
#include "frontend/sprite_filter.hpp"
#include "frontend/native_practice.hpp"
#include "hwvideo/wide_scene.hpp"
#include "horizon_side_reference.hpp"
#include "ground_correction_reference.hpp"
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
    // Directed stage entry is a presentation regression, not whole-credit
    // progression acceptance. Preserve native RGB/PCM/time through the roll.
    enhanced.capture_wide_scene(true);
    Frontend::NativePractice original_practice,enhanced_practice;
    original_practice.start(original,6,{255,255,221});
    enhanced_practice.start(enhanced,6,{255,255,221});
    std::vector<std::unique_ptr<Presentation::WideScene>> stage6_scenes,stage6_plain;
    Presentation::EffectAssets side_assets({roms.maincpu.rom,roms.maincpu.length},{roms.subcpu.rom,roms.subcpu.length});
    std::array<uint64_t,3> side_counts{};
    for(unsigned width:{358u,398u}){stage6_scenes.push_back(std::make_unique<Presentation::WideScene>(roms,width));stage6_plain.push_back(std::make_unique<Presentation::WideScene>(roms,width));}
    unsigned triangle_witnesses=0,quadrant_crossings=0;uint8_t previous_angle=0;
    unsigned cache_cases=0;
    for(unsigned f=0;f<420;++f){GameControls c;c.throttle=1;if(f>=120)c.stick_x=((f-120)/120)%2?1:-1;
        original.advance(c);enhanced.advance(c);auto a=original.frame(),b=enhanced.frame();
        require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns&&
            std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"stage 6 ground observer changed native RGB/time");
        require(original.take_audio()==enhanced.take_audio(),"stage 6 ground observer changed native PCM");
        const auto scene=enhanced.wide_scene();require(scene.ready&&!scene.clips.empty(),"stage 6 scene missing");
        if(f%30==0)for(const auto& clip:scene.clips)for(unsigned width:{320u,398u}){
            auto bank=clip.sprites;std::vector<int> anchors;
            if(width==398){anchors.resize(bank.size()/16);for(unsigned i=0;i<anchors.size();++i)anchors[i]=int((i*37+f)%398)-39;}
            unsigned selected=0;
            for(unsigned slot=0;slot<bank.size()/16;++slot){
                std::array<uint16_t,8>d{};for(unsigned i=0;i<8;++i)d[i]=uint16_t(bank[slot*16+2*i])<<8|bank[slot*16+2*i+1];
                if(d[0]&0x8000)break;
                if(!(d[0]&0x5000)&&family.matches(d))++selected;
                else bank[slot*16]|=0x50;
            }
            if(!selected)continue;
            std::vector<uint8_t> pens(width*224),marks(width*224);
            std::vector<uint16_t> indices(width*224,0x1700);
            const int left=-int((width-320)/2);
            Presentation::decode(bank,{roms.sprites.rom,roms.sprites.length},0,223,side_assets,
                [&](int x,unsigned y,uint16_t word,auto){pens[y*width+unsigned(x-left)]=word&15;},left,left+int(width)-1,anchors);
            Presentation::GroundHorizon::Profile expected(width);
            for(unsigned y=0;y<224;++y)for(unsigned x=0;x<width;++x)if(pens[y*width+x])expected.pixel(x,y,pens[y*width+x]);
            expected.measured=true;
            auto reference=indices;const auto normal=family.normal(uint8_t(clip.roll));
            TestReference::ground_correction(reference,marks,clip.road,clip.top,clip.bottom,normal[0],normal[1],expected,left,clip.palette);
            family.apply(indices,marks,clip.road,4,clip.sprites,{roms.sprites.rom,roms.sprites.length},clip.top,clip.bottom,uint8_t(clip.roll),side_assets,width,left,anchors,clip.palette);
            require(indices==reference,"ground cached classification differs from full bank uncached correction");
            const auto& actual=family.cached_profile;
            require(actual.upper==expected.upper&&actual.lower==expected.lower&&actual.left==expected.left&&actual.right==expected.right,"ground compact cache differs from full bank raster/anchors");
            for(unsigned y=0;y<224;++y)require(actual.rows[y].direction()==expected.rows[y].direction(),"ground compact cache row ordering differs");
            for(unsigned x=0;x<width;++x)require(actual.columns[x].direction()==expected.columns[x].direction(),"ground compact cache column ordering differs");
            const auto key=family.cached_horizon;const auto key_anchors=family.cached_anchors;
            // Unrelated sprite animation and hardware feedback must reuse the
            // same horizon-only key; the source bank remains the native bank.
            auto changed=clip.sprites;
            for(unsigned slot=0;slot<changed.size()/16;++slot){
                if(changed[slot*16]&0x80)break;
                changed[slot*16+14]^=0x5a;changed[slot*16+15]^=0xa5;
            }
            for(unsigned i=0;i<indices.size();++i){indices[i]=uint16_t(0x1700+i%8);marks[i]=i%13==0?2:0;}
            reference=indices;
            TestReference::ground_correction(reference,marks,clip.road,clip.top,clip.bottom,normal[0],normal[1],expected,left,clip.palette);
            family.apply(indices,marks,clip.road,4,changed,{roms.sprites.rom,roms.sprites.length},clip.top,clip.bottom,uint8_t(clip.roll),side_assets,width,left,anchors,clip.palette);
            require(indices==reference,"ground geometry cache retained road fill/priority state");
            require(family.cached_horizon==key&&family.cached_anchors==key_anchors,"ground feedback invalidated compact cache key");++cache_cases;
        }
        const auto angle=uint8_t(scene.clips.back().roll);if((angle/64)!=(previous_angle/64))++quadrant_crossings;previous_angle=angle;
        for(unsigned index=0;index<stage6_scenes.size();++index){auto& renderer=stage6_scenes[index];renderer->render(scene.clips,scene.info.number,true);const auto v=renderer->view();
            stage6_plain[index]->render(scene.clips,scene.info.number,false);const auto plain=stage6_plain[index]->view();
            for(const auto& clip:scene.clips){try{auto counts=check_horizon_sides(clip,plain,v,family,side_assets);
                for(unsigned side=0;side<3;++side)side_counts[side]+=counts[side];
            }catch(const std::exception& e){throw std::runtime_error("frame="+std::to_string(f)+" width="+std::to_string(v.width)+" "+e.what());}}
            if(f==264||f==296){const unsigned x=(f==264?350:20)-(398-v.width)/2,y=f==264?150:40;
                require(x<v.width&&y<224,"stage 6 cloud witness outside viewport");
                require(v.ground_indices[y*v.width+x]>=0x1700&&v.ground_indices[y*v.width+x]<0x1708,"stage 6 cloud retained a blue tear");
            }
            if(f==256||f==286){const unsigned x=(f==256?100:310)-(398-v.width)/2,y=f==256?110:50;
                require(v.ground_indices[y*v.width+x]>=0x1720&&v.ground_indices[y*v.width+x]<0x1730,"stage 6 fast roll retained a white sky triangle");
                ++triangle_witnesses;
            }
        }
    }
    require(side_counts[0]>10000&&side_counts[1]>10000,"stage 6 measured side coverage missing");
    require(cache_cases>=20,"ground compact cache coverage missing");
    std::cout<<"Ground compact cache: "<<cache_cases<<" full-bank raster/ordering/anchor and feedback comparisons passed\n";
    std::cout<<"Stage 6 measured sides: sky="<<side_counts[0]<<" ground="<<side_counts[1]<<" ground without uniform donor="<<side_counts[2]<<"\n";
    require(triangle_witnesses==4&&quadrant_crossings>=4,"stage 6 fast-roll quadrant coverage missing");
    std::cout<<"Stage 6 horizon: 420/420 native RGB/PCM/time frames identical, zero skipped; "<<triangle_witnesses<<" sky-triangle witnesses across 358/398 pixels; quadrant crossings="<<quadrant_crossings<<"\n";
    std::cout<<"Ground correction: 1560 native RGB/PCM/time frames identical, 0 skipped; "<<changed<<" background pixels ("<<visible_changes<<" visibly exposed) across "<<frames<<" frames; left/right="<<left<<'/'<<right<<"; "<<assets_checked<<" ROM asset/LOD selections; disable/restart passed; sample="<<sample_frame<<"; "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

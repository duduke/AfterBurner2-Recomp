#include "hwvideo/wide_scene.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void word(uint8_t* bytes,unsigned at,uint16_t value){bytes[at]=value>>8;bytes[at+1]=value;}
static void pointer(uint8_t* bytes,unsigned at,uint32_t value){word(bytes,at,value>>16);word(bytes,at+2,value);}

// Generated graphics and asset records exercise the presentation decoder;
// no arcade ROM data or executable game behavior is embedded in this fixture.
static void synthetic_assets(Roms& rom){
    rom.maincpu.init(0x40000);rom.subcpu.init(0x20000);rom.tiles.init(0xc000);
    rom.road.init(0x10000);rom.sprites.init(0x40000);
    for(unsigned frame:{0u,0x16500u,0x16550u})word(rom.subcpu.rom,frame+8,1);
    // Empty crash streams and one-frame explosion lists are enough to build
    // the asset classifier; the fixture does not animate any of them.
    for(unsigned variant=0;variant<2;++variant)pointer(rom.maincpu.rom,0xe89e + variant*4,0x1000);
    for(unsigned stream=0;stream<5;++stream)pointer(rom.maincpu.rom,0x1000+stream*4,0x1100);
    word(rom.maincpu.rom,0x1100,0xffff);
    for(unsigned phase=0;phase<2;++phase)for(unsigned pitch=0;pitch<5;++pitch)
        pointer(rom.maincpu.rom,(phase?0x2939c:0x29018)+pitch*4,0x1250);
    // A synthetic solid pen-1 text tile and an eight-row sprite. The sprite's
    // transparent end marker leaves x=6 uncovered in each row.
    for(unsigned row=0;row<8;++row){
        rom.tiles.rom[0x41*8+row]=0xff;
        const uint32_t pixels=0x111111f1;
        for(unsigned b=0;b<4;++b)rom.sprites.rom[row*4+b]=pixels>>(b*8);
    }
}

static Presentation::SceneClip clip(unsigned top,unsigned bottom,uint64_t ns){
    Presentation::SceneClip result;result.top=top;result.bottom=bottom;result.ns=ns;result.display=true;
    result.sprites[0]=0x80;
    result.text[24]=0x0241;
    result.palette[0x1c09]=0x7fff;
    return result;
}
static void sprite(Presentation::SceneClip& c,unsigned priority,bool shadow){
    const uint16_t descriptor[]{0x0100,0,0x02be,uint16_t((priority<<12)|(shadow?0x4000:0)|0x0200),0xe200,7,0,0};
    for(unsigned i=0;i<8;++i)word(c.sprites.data(),i*2,descriptor[i]);
    c.sprites[16]=0x80;
}
static void sprite_pen(Roms& rom,bool shadow){
    for(unsigned row=0;row<8;++row){const uint32_t pixels=shadow?0xaaaaaafa:0x111111f1;
        for(unsigned b=0;b<4;++b)rom.sprites.rom[row*4+b]=pixels>>(b*8);}
}

int main()try{
    // Compare ownership to the native hardware predicate over every raw top
    // sprite word and tile-priority union, including category-1 text.
    for(unsigned category=0;category<2;++category)for(unsigned raw=0;raw<=0xffff;++raw){
        const auto got=Presentation::hud_occlusion(uint16_t(category<<15),uint16_t(raw));
        for(unsigned tile_priority=0;tile_priority<4;++tile_priority){
            const bool visible=raw!=0xffff && (1u<<((raw>>12)&3))>((category?8u:4u)|tile_priority);
            const unsigned expected=!visible?0:(raw&0x400f)==0x400a?1:2;
            require(got==expected,"HUD ownership disagrees with native sprite priority");
        }
    }
    Roms rom;synthetic_assets(rom);
    for(unsigned width:{320u,358u,398u}){
        Presentation::WideScene scene(rom,width);
        std::array<Presentation::SceneClip,2> clips{clip(0,3,0),clip(4,223,1)};
        clips[1].palette[0x1c09]=0x4f00;
        scene.render(clips,1,false);
        auto original=scene.view();require(original.ready,"original scene incomplete");
        require(original.hud.clips.empty()&&original.hud.occlusion.empty(),"original HUD captured unused replacement data");
        const unsigned left=(width-320)/2;
        require(original.background[left]==0xffffffff && original.background[4*width+left]==0xff0000ff,"native partial text palette changed");
        // Switching modes on the very same frame must replace the baked text.
        scene.render(clips,1,false,true,false);
        auto replaced=scene.view();require(replaced.ready,"replacement scene incomplete");
        require(replaced.background[left]==0xff000000 && replaced.background[4*width+left]==0xff000000,"original glyph remained in replacement underlay");
        require(replaced.hud.clips.size()==2&&replaced.hud.occlusion.size()==320*224,"HUD frontier extraction missing");
        require(replaced.hud.clips[0].top==0&&replaced.hud.clips[0].bottom==3&&replaced.hud.clips[1].top==4&&replaced.hud.clips[1].bottom==223,"HUD partial glyph clipping lost");
        require(replaced.hud.clips[0].entries[0]==0x0241&&replaced.hud.clips[0].colors[9]==0xffffffff&&replaced.hud.clips[1].colors[9]==0xff0000ff,"HUD text or per-frontier palette changed");
        require(replaced.hud.clips[0].colors[73]!=replaced.hud.clips[0].colors[9],"HUD effects palette missing");
        require(std::all_of(replaced.hud.occlusion.begin(),replaced.hud.occlusion.end(),[](auto p){return p==0;}),"empty sprite bank occluded HUD");
        scene.render(clips,1,false);
        require(scene.view().background[left]==0xffffffff&&scene.view().hud.clips.empty(),"same-frame original restoration failed");

        auto covered=clip(0,223,0);sprite(covered,3,false);
        scene.render(std::span(&covered,1),2,false,true,false);
        require(scene.view().hud.occlusion[0]==2&&scene.view().hud.occlusion[6]==0&&scene.view().hud.occlusion[7]==2,"native sprite coverage not retained");
        require(scene.view().sprites[left]!=0,"replacement underlay kept text priority");
        covered.text[24]|=0x8000;
        scene.render(std::span(&covered,1),3,false,true,false);
        require(scene.view().hud.occlusion[0]==0,"category-1 text occluded by priority-3 sprite");
        require(scene.view().sprites[left]!=0,"replacement underlay kept category-1 text priority");
        covered.text[24]&=0x7fff;sprite(covered,3,true);sprite_pen(rom,true);
        scene.render(std::span(&covered,1),4,false,true,false);
        require(scene.view().hud.occlusion[0]==1,"hardware HUD effects pixel lost");
        // Enhanced descriptors do not own native HUD occlusion.
        covered.sprites[0]=0x80;sprite_pen(rom,false);
        Presentation::WideDescriptor detached;
        auto source=clip(0,223,0);sprite(source,3,false);
        std::copy_n(source.sprites.begin(),16,detached.bytes.begin());detached.x=0;
        covered.extended_sprites.push_back(detached);
        scene.render(std::span(&covered,1),5,false,true,false);
        require(scene.view().hud.occlusion[0]==0,"detached sprite changed native HUD ownership");
        covered.display=false;
        scene.render(std::span(&covered,1),6,false,true,false);
        require(scene.view().hud.clips.empty()&&std::all_of(scene.view().hud.occlusion.begin(),scene.view().hud.occlusion.end(),[](auto p){return p==2;}),"disabled display exposed HUD");
        scene.reset();require(!scene.view().ready&&scene.view().hud.clips.empty()&&scene.view().hud.occlusion.empty(),"HUD data survived reset");
    }
    std::cout<<"HUD scene: exhaustive native priority/effects ownership, partial text/palette clips, text-free underlay, same-frame toggles, 320/358/398 safe areas, detached isolation and reset passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "engine/game_session.hpp"
#include "frontend/sprite_filter.hpp"
#include "hwvideo/wide_scene.hpp"
#include <iostream>
#include <string>

namespace {
constexpr unsigned native_width=320, height=224;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
uint32_t argb(RGB888 p) {return 0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;}
GameControls controls(unsigned frame) {
    GameControls value;
    value.coin=frame>=360&&frame<372;value.start=frame>=420&&frame<432;
    if(frame>=480){
        const auto phase=((frame-480)/24)%12;
        constexpr float x[]={0,-1,1,0,0,0,0,-1,1,0,0,0};
        constexpr float y[]={0,0,0,-1,1,0,0,1,-1,0,0,0};
        value.stick_x=x[phase];value.stick_y=y[phase];
        value.throttle=phase==5?-1:phase==6?1:0;
        value.cannon=phase==7||phase==9||phase==10;
        value.missile=phase==8||phase==9||phase==10;
    }
    return value;
}
void same_native(GameSession& original,GameSession& captured) {
    const auto a=original.frame(),b=captured.frame();
    require(a.width==native_width&&b.width==native_width&&a.height==height&&b.height==height,"HUD capture changed native dimensions");
    require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"HUD capture changed native frame/time");
    require(a.pixels.size()==native_width*height&&b.pixels.size()==a.pixels.size(),"HUD capture returned incomplete native pixels");
    require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"HUD capture changed native RGB");
    require(original.take_audio()==captured.take_audio(),"HUD capture changed native PCM");
}

// Decode the original planar tile ROM directly. This does not call HWTiles
// or the replacement HUD renderer: it independently reconstructs the native
// 8x8 text raster from the detached entries, palettes and ownership mask.
unsigned text_pen(uint16_t entry,unsigned x,unsigned y) {
    const unsigned third=roms.tiles.length/3,offset=(entry&0x1ff)*8+y;
    require(offset<third,"HUD tile code outside the original graphics bank");
    unsigned pen=0;
    for(unsigned plane=0;plane<3;++plane)pen|=((roms.tiles.rom[plane*third+offset]>>(7-x))&1)<<plane;
    return pen;
}
struct Witnesses {
    uint64_t clips{},glyphs{},transparent{},normal{},shadow{},hidden{},changed{};
    uint64_t categories[2]{};
};
void restore_native_hud(Presentation::WideScene::View view,std::vector<uint32_t>& output,Witnesses& witnesses) {
    require(view.hud.occlusion.size()==native_width*height,"detached HUD ownership dimensions changed");
    std::array<bool,height> rows{};
    for(const auto& clip:view.hud.clips){
        require(clip.top<=clip.bottom&&clip.bottom<height,"HUD partial-render bounds invalid");
        ++witnesses.clips;
        for(unsigned y=clip.top;y<=clip.bottom;++y){
            require(!rows[y],"HUD partial-render clips overlap");rows[y]=true;
            for(unsigned x=0;x<native_width;++x){
                const auto entry=clip.entries[(y/8)*40+x/8];
                const auto pen=text_pen(entry,x%8,y%8);
                const auto ownership=view.hud.occlusion[y*native_width+x];
                require(ownership<=2,"invalid detached HUD ownership");
                if(!pen){++witnesses.transparent;continue;}
                ++witnesses.glyphs;++witnesses.categories[entry>>15];
                if(ownership==2){++witnesses.hidden;continue;}
                if(ownership==1)++witnesses.shadow;else ++witnesses.normal;
                const auto i=y*view.width+view.origin+x;
                const auto color=clip.colors[((entry>>9)&7)*8+pen+64*ownership];
                witnesses.changed+=output[i]!=color;output[i]=color;
            }
        }
    }
    for(unsigned y=0;y<height;++y)if(!rows[y])for(unsigned x=0;x<native_width;++x)
        require(view.hud.occlusion[y*native_width+x]==2,"disabled HUD row retained visible text ownership");
}
struct Aspect {
    Presentation::WideScene original,replacement;
    Frontend::SpriteScaler original_scaler,replacement_scaler;
    explicit Aspect(unsigned width):original(roms,width),replacement(roms,width){}
    void reset(){original.reset();replacement.reset();}
};
void check_presentation(GameSession& captured,std::array<Aspect,3>& aspects,Witnesses& witnesses) {
    const auto scene=captured.wide_scene();require(scene.ready,"HUD scene capture missing after completed frame");
    const auto native=captured.frame();
    for(auto& aspect:aspects){
        aspect.original.render(scene.clips,scene.info.number,false,false);
        aspect.replacement.render(scene.clips,scene.info.number,false,false,false);
        const auto a=aspect.original.view(),b=aspect.replacement.view();
        require(a.ready&&b.ready,"HUD scene lost complete native row coverage");
        require(a.origin==(a.width-native_width)/2&&b.origin==a.origin,"HUD placement lost centered native viewport");
        require(a.hud.clips.empty()&&a.hud.occlusion.empty(),"Original mode unexpectedly detached its native HUD");
        const auto original=aspect.original_scaler.compose(a.background,a.sprites,a.width,height,Frontend::SpriteFilter::none,a.protection);
        const auto underlay=aspect.replacement_scaler.compose(b.background,b.sprites,b.width,height,Frontend::SpriteFilter::none,b.protection);
        std::vector<uint32_t> restored(underlay.begin(),underlay.end());
        restore_native_hud(b,restored,witnesses);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<a.width;++x){
            const auto i=y*a.width+x;
            if(restored[i]!=original[i])throw std::runtime_error("detached HUD/native planar reconstruction differs at frame "+std::to_string(scene.info.number)+" width "+std::to_string(a.width)+" x "+std::to_string(x)+" y "+std::to_string(y));
            if(x>=a.origin&&x<a.origin+native_width)
                require(original[i]==argb(native.pixels[y*native_width+x-a.origin]),"Original detached scene differs from native raster");
        }
    }
    // Detached glyph coordinates and native sprite ownership must be the same
    // for all three viewport widths; only their centered output origin moves.
    const auto reference=aspects.front().replacement.view().hud;
    for(unsigned i=1;i<aspects.size();++i){
        const auto current=aspects[i].replacement.view().hud;
        require(current.clips.size()==reference.clips.size()&&std::equal(current.occlusion.begin(),current.occlusion.end(),reference.occlusion.begin()),"aspect changed native HUD ownership");
        for(unsigned c=0;c<reference.clips.size();++c){
            const auto& a=reference.clips[c];const auto& b=current.clips[c];
            require(a.top==b.top&&a.bottom==b.bottom&&a.entries==b.entries&&a.colors==b.colors,"aspect changed detached HUD tile/palette frontier");
        }
    }
}
}

int main() try {
    require(roms.load_aburner2(),"ROMs required");
    GameSession original(roms),captured(roms);
    captured.capture_wide_scene(true,native_width);
    require(!captured.wide_scene().ready,"HUD capture ready before first rendering");
    std::array<Aspect,3> aspects{Aspect(320),Aspect(358),Aspect(398)};
    Witnesses witnesses;
    for(unsigned frame=0;frame<781;++frame){
        if(frame==520){
            const auto before=captured.info();captured.capture_wide_scene(false,native_width);
            require(!captured.wide_scene().ready&&captured.wide_scene().clips.empty(),"disabled HUD capture retained stale readiness");
            captured.set_paused(true);captured.capture_wide_scene(true,native_width);
            require(!captured.wide_scene().ready,"HUD capture re-enabled while paused retained stale data");
            for(unsigned p=0;p<5;++p)require(!captured.advance(controls(frame)),"paused HUD capture advanced simulation");
            require(captured.info().number==before.number&&captured.info().time_ns==before.time_ns&&captured.take_audio().empty(),"paused HUD capture changed native time/audio");
            require(!captured.wide_scene().ready,"paused HUD capture became ready without rendering");captured.set_paused(false);
        }
        require(original.advance(controls(frame))&&captured.advance(controls(frame)),"running HUD comparison did not advance");
        same_native(original,captured);check_presentation(captured,aspects,witnesses);
    }
    require(witnesses.glyphs&&witnesses.transparent&&witnesses.normal&&witnesses.changed,"HUD integration failed to exercise visible/transparent glyphs and text-free underlay");
    // Restart drops borrowed capture readiness; a paused restart resumes fresh
    // native output and the opted-in detached capture on the next frame.
    captured.set_paused(true);original.restart();captured.restart();
    require(!captured.paused()&&!captured.wide_scene().ready&&captured.take_audio().empty(),"HUD restart retained paused/audio/capture state");
    for(auto& aspect:aspects)aspect.reset();
    for(unsigned frame=0;frame<8;++frame){
        original.advance({});captured.advance({});same_native(original,captured);check_presentation(captured,aspects,witnesses);
    }
    std::cout<<"HUD presentation: 781 physical-input frames plus 8 restart frames exact native RGB/PCM/time, Original and text-free/native-planar reconstructions at 320/358/398, capture toggle and paused/restart readiness; "
             <<witnesses.clips<<" clips, "<<witnesses.glyphs<<" opaque glyph pixels (category0="<<witnesses.categories[0]<<", category1="<<witnesses.categories[1]
             <<"), "<<witnesses.transparent<<" transparent, "<<witnesses.normal<<" normal, "<<witnesses.shadow<<" shadow, "<<witnesses.hidden<<" hidden, "<<witnesses.changed<<" changed underlay pixels.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}

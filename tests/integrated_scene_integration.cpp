#include "engine/game_session.hpp"
#include "hwvideo/wide_scene.hpp"
#include <charconv>
#include <cstring>
#include <iostream>
#include <tuple>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
#include "integrated_scene_reference.hpp"
int main(int argc,char** argv)try{
    unsigned width=398;
    if(argc>2)throw std::runtime_error("Usage: integrated_scene_integration [width]");
    if(argc==2){const std::string_view value(argv[1]);const auto r=std::from_chars(value.data(),value.data()+value.size(),width);
        require(r.ec==std::errc{}&&r.ptr==value.data()+value.size(),"invalid width");}
    require(roms.load_aburner2(),"ROMs required");
    auto reference_owner=std::make_unique<GameSession>(roms),primary_owner=std::make_unique<GameSession>(roms);
    auto live_owner=std::make_unique<Presentation::WideScene>(roms,width),snapshot_owner=std::make_unique<Presentation::WideScene>(roms,width);
    auto& reference=*reference_owner;auto& primary=*primary_owner;
    auto& live=*live_owner;auto& snapshot=*snapshot_owner;
    bool ground=false,original_hud=true,retain_snapshots=true,check_frontiers=true;
    std::span<const Presentation::SceneClip> expected_frontiers;
    size_t frontier_index=0;
    unsigned callbacks=0,completions=0,compared=0,disabled=0,multiple=0,fragments=0,hud=0,aircraft=0,startup=0;
    auto consumer=[&](const Presentation::SceneClip& c,uint64_t frame,const Screen::ViewportLayers& layers){
        require(frame==primary.info().number+1,"primary callback has wrong frame identity");
        if(check_frontiers){
            require(frontier_index<expected_frontiers.size(),"extra primary frontier");
            const auto& x=expected_frontiers[frontier_index++];
            require(x.ns==c.ns&&x.top==c.top&&x.bottom==c.bottom&&x.display==c.display,"primary capture frontier differs");
            require((!retain_snapshots||x.tiles==c.tiles)&&x.text==c.text&&x.palette==c.palette&&x.road==c.road&&x.sprites==c.sprites&&x.registers==c.registers&&x.road_control==c.road_control,"primary capture state/feedback differs");
            require(retain_snapshots||c.tile_gen==0,"compact capture copied redundant tile snapshot");
            require(x.extended_sprites.size()==c.extended_sprites.size(),"primary visibility count differs");
            for(size_t j=0;j<x.extended_sprites.size();++j){const auto& p=x.extended_sprites[j];const auto& q=c.extended_sprites[j];
                require(p.bytes==q.bytes&&p.x==q.x&&p.depth==q.depth&&p.recovered==q.recovered&&p.kind==q.kind,"primary visibility descriptor differs");}
        }
        live.render_primary_clip(c,frame,layers,ground,true,original_hud);
        require(!live.view().ready,"partial primary frame published before completion");
        ++callbacks;disabled+=!c.display;
    };
    auto complete=[&](uint64_t frame){require(frame==primary.info().number,"primary completion preceded native frame frontier");live.finish_primary_frame(frame);++completions;};
    auto enable=[&]{reference.capture_wide_scene(true,width);primary.capture_wide_scene(true,width,consumer,retain_snapshots,complete);};
    enable();
    auto step=[&](GameControls input){
        reference.advance(input);
        const auto before=reference.wide_scene();expected_frontiers=before.clips;frontier_index=0;
        primary.advance(input);
        const auto a=reference.frame(),b=primary.frame();
        require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"primary scene changed native time");
        require(a.pixels.size()==b.pixels.size()&&!std::memcmp(a.pixels.data(),b.pixels.data(),a.pixels.size_bytes()),"primary scene changed native RGB");
        require(reference.take_audio()==primary.take_audio(),"primary scene changed native PCM");
        const auto after=primary.wide_scene();
        require(frontier_index==before.clips.size(),"primary capture frontier count differs");
        if(retain_snapshots)require(before.ready==after.ready&&before.clips.size()==after.clips.size(),"retained primary snapshot count differs");
        else require(!after.ready&&after.clips.empty(),"compact capture exposed stale snapshots");
        live.finish_primary_frame(b.info.number);
        if(before.ready){
            snapshot.render(before.clips,b.info.number,ground,true,original_hud);
            same_view(live.view(),snapshot.view());++compared;multiple+=before.clips.size()>1;
            fragments+=!live.view().fragments.empty();hud+=!live.view().hud.clips.empty();aircraft+=!live.view().player.draws.empty();
        }else ++startup;
    };
    for(unsigned f=0;f<780;++f){
        if(f==620){reference.capture_sprite_scene(true);primary.capture_sprite_scene(true);}
        if(f==640){reference.capture_wide_scene(false);primary.capture_wide_scene(false);}
        if(f==641){live.reset();snapshot.reset();enable();}
        if(f==660)original_hud=false;
        if(f==700)ground=true;
        if(f==720){live.player_aircraft(true);snapshot.player_aircraft(true);}
        if(f==760){retain_snapshots=false;primary.capture_wide_scene(true,width,consumer,false,complete);live.reset();snapshot.reset();}
        GameControls input;input.coin=f>=360&&f<372;input.start=f>=420&&f<432;
        if(f>=480){input.stick_x=(f/30)%2?1.f:-1.f;input.stick_y=(f/60)%2?1.f:-1.f;input.throttle=1;input.cannon=true;input.missile=f%60<4;}
        step(input);
        if(f==640)require(!primary.wide_scene().ready,"disabled primary capture retained readiness");
    }
    // Directed Practice entry supplies a neutral flight aircraft witness; the
    // earlier natural boot/input window alone may remain in unsupported poses.
    check_frontiers=false;
    reference.enter_practice_stage(1);
    primary.enter_practice_stage(1,[&]{live.finish_primary_frame(primary.info().number);primary.take_audio();});
    check_frontiers=true;
    live.reset();snapshot.reset();
    for(unsigned f=0;f<90;++f){GameControls input;input.throttle=1;input.cannon=true;step(input);}
    primary.set_paused(true);const auto paused=primary.info();const auto count=callbacks;
    require(!primary.advance({})&&primary.info().number==paused.number&&primary.info().time_ns==paused.time_ns&&callbacks==count,"paused primary capture advanced");
    require(primary.take_audio().empty(),"paused primary capture produced PCM");
    primary.restart();reference.restart();live.reset();snapshot.reset();
    for(unsigned f=0;f<12;++f)step({});
    require(compared>790&&callbacks>compared&&completions>=compared&&disabled&&fragments&&hud&&aircraft,"primary scene fixture missing required coverage");
    std::cout<<"Integrated live scene width="<<width<<": "<<compared<<" of "<<compared<<" native RGB/PCM/time and enhanced plane comparisons identical, 0 skipped; callbacks="<<callbacks<<" completions="<<completions<<" disabled_clips="<<disabled<<" multiple_clips="<<multiple<<" sprite_frames="<<fragments<<" HUD_frames="<<hud<<" aircraft_frames="<<aircraft<<" no_capture_frames="<<startup<<"; automatic completion, retained/compact capture, pause/disable/re-enable/restart and native sprite-scene observer coexistence passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}

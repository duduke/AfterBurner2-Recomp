#pragma once
#include "hwvideo/wide_scene.hpp"
#include <tuple>
// Exact differential comparison of every published presentation plane.
// The caller supplies require() so failures retain its local diagnostic policy.
template<class A,class B>static void same(const A& a,const B& b,const char* message){
    require(a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin()),message);
}
static auto geometry(const Presentation::PlayerAircraftDraw& d){
    return std::tie(d.frame_address,d.source_width,d.source_height,d.x,d.y,d.width,d.height,d.top,d.bottom,d.flip_x,d.flip_y,d.mask_offset,d.root_frame_address);
}
static void same_view(Presentation::WideScene::View a,Presentation::WideScene::View b){
    require(a.ready&&b.ready&&a.width==b.width&&a.origin==b.origin,"live scene geometry/readiness differs");
    same(a.background,b.background,"live background differs");same(a.sprites,b.sprites,"live sprites differ");
    same(a.protection,b.protection,"live protection differs");same(a.ground_indices,b.ground_indices,"live ground indices differ");
    same(a.heads,b.heads,"live fragment heads differ");
    require(a.fragments.size()==b.fragments.size(),"live fragment count differs");
    for(size_t i=0;i<a.fragments.size();++i){auto x=a.fragments[i],y=b.fragments[i];
        require(std::tie(x.color,x.previous,x.kind,x.raw)==std::tie(y.color,y.previous,y.kind,y.raw),"live fragment chain differs");}
    same(a.hud.occlusion,b.hud.occlusion,"live HUD occlusion differs");
    require(a.hud.clips.size()==b.hud.clips.size(),"live HUD clip count differs");
    for(size_t i=0;i<a.hud.clips.size();++i){const auto& x=a.hud.clips[i];const auto& y=b.hud.clips[i];
        require(x.top==y.top&&x.bottom==y.bottom&&x.entries==y.entries&&x.colors==y.colors,"live HUD clip differs");}
    same(a.player.visibility,b.player.visibility,"live aircraft visibility differs");
    require(a.player.draws.size()==b.player.draws.size(),"live aircraft draw count differs");
    for(size_t i=0;i<a.player.draws.size();++i)require(geometry(a.player.draws[i])==geometry(b.player.draws[i]),"live aircraft geometry differs");
}

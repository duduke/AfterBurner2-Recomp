#pragma once
#include "frontend/custom_stage.hpp"
#include "frontend/custom_runway_art.hpp"
#include "frontend/custom_stage_scene.hpp"
#include "frontend/interactive_stage_sprites.hpp"
#include "frontend/custom_stage_clouds.hpp"
#include <array>

namespace Frontend {
struct CustomStageView {
    SDL_Renderer* r;InteractiveStageSprites* sprites{};CustomStageScene* scene{};
    SDL_Texture* material{};SDL_Texture* runway_art{};
    SDL_Color sky_top{0,170,255,255},sky_edge{220,250,255,255};bool assistance=true,show_scenery=true,show_hud=true;
    unsigned smoke_puffs{},scenery_drawn{},trees_drawn{},waves_drawn{},sky_aircraft{},runway_rows{};bool carrier_drawn{},shadow_drawn{};double probe_error_px{};
    CustomStageClouds clouds{};unsigned clouds_drawn{};
    unsigned hud_tiles{},stage_number{1};bool game_font{};
    using S=CustomStage;
    static constexpr SDL_Color green{100,255,174,255},white{225,236,229,255},amber{255,191,86,255},muted{130,166,164,255},dark{10,23,29,255};
    void color(SDL_Color c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);}
    void box(float x,float y,float w,float h,SDL_Color c,bool fill=true){color(c);SDL_FRect rect{x,y,w,h};if(fill)SDL_RenderFillRect(r,&rect);else SDL_RenderRect(r,&rect);}
    void line(float x,float y,float a,float b,SDL_Color c){color(c);SDL_RenderLine(r,x,y,a,b);}
    void stroke(float x,float y,float a,float b,float width,SDL_Color c){
        if(width<=1){line(x,y,a,b,c);return;}const float length=std::hypot(a-x,b-y);if(length==0)return;
        const float nx=-(b-y)*width/(2*length),ny=(a-x)*width/(2*length);
        const SDL_FColor tint{c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f};
        const SDL_Vertex vertices[]={{{x+nx,y+ny},tint,{}},{{a+nx,b+ny},tint,{}},{{a-nx,b-ny},tint,{}},{{x-nx,y-ny},tint,{}}};
        const int indices[]={0,1,2,0,2,3};SDL_RenderGeometry(r,nullptr,vertices,4,indices,6);
    }
    void text(float x,float y,const std::string& s,SDL_Color c=white,float size=1){
        if(sprites && sprites->hud()){sprites->hud()->text(x,y,s,c,size);return;}
        color(c);SDL_SetRenderScale(r,size,size);SDL_RenderDebugText(r,x/size,y/size,s.c_str());SDL_SetRenderScale(r,1,1);
    }
    void centered(float y,const std::string& s,SDL_Color c=white,float size=1){text(480-float(s.size())*4*size,y,s,c,size);}
    void ring(float x,float y,float radius,SDL_Color c){for(int i=0;i<32;++i){const double a=i*6.283185/32,b=(i+1)*6.283185/32;line(x+radius*std::cos(a),y+radius*std::sin(a),x+radius*std::cos(b),y+radius*std::sin(b),c);}}
    void quad(std::array<StageCamera::Point,4> p,SDL_Color c,SDL_Texture* texture=nullptr,float offset=0){
        for(auto a:p)if(!a.visible)return;
        SDL_Vertex v[4];const SDL_FPoint uv[4]={{0,offset},{1,offset},{1,offset+1},{0,offset+1}};
        for(unsigned i=0;i<4;++i)v[i]={{p[i].x,p[i].y},{c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f},uv[i]};
        const int indices[]={0,1,2,0,2,3};SDL_RenderGeometry(r,texture,v,4,indices,6);
    }
    void surface(const StageCamera& c,double left,double right,double near,double far,double height,SDL_Color shade,SDL_Texture* texture=nullptr){
        near=std::max(near,-c.behind+2);if(far<=near)return;
        quad({c.project(left,height,near),c.project(right,height,near),c.project(right,height,far),c.project(left,height,far)},shade,texture);
    }
    void draw(const S& s,bool paused){
        hud_tiles=0;game_font=sprites && sprites->hud();if(game_font)sprites->hud()->begin();
        smoke_puffs=scenery_drawn=trees_drawn=waves_drawn=sky_aircraft=runway_rows=0;carrier_drawn=shadow_drawn=false;
        color(dark);SDL_RenderClear(r);SDL_Rect clip{40,78,880,392};SDL_SetRenderClipRect(r,&clip);
        StageCamera camera{s.x,s.refuel()?120+s.height:s.height,198+s.pitch*18};
        auto terrain_camera=camera;if(s.refuel())terrain_camera.horizon=405+s.pitch*18;
        const double horizon=terrain_camera.horizon;
        const float plane_base=s.refuel()?float(198+20*camera.focal/camera.behind):385.f;
        const float plane_y=plane_base-float(s.pitch*12);
        auto probe=camera.project(s.x+CustomStageGuidance::probe_right,120+s.height+CustomStageGuidance::probe_up,CustomStageGuidance::probe_forward);
        // The metric docking frame uses the neutral aircraft datum. Native
        // pose/bank changes must not drag the whole tanker through the sky.
        const auto contact=sprites?sprites->refuel_contact(480,plane_y,158,0,0):StagePoint{480,plane_y-30};
        const auto visible_contact=sprites?sprites->refuel_contact(480,plane_y,158,float(s.bank),float(s.pitch)):contact;
        StagePoint tanker_origin{};float tanker_scale=0;
        probe_error_px=0;clouds_drawn=0;
        if(scene){scene->begin();scene->draw_before(0,camera);}
        if(s.definition.rom_environment){
        // Sky and ground are sampled from the verified ROM road layer once.
        // Ground rows are reprojected by our camera; no native sequence runs.
        const bool land=s.definition.environment==S::D::Environment::runway || s.definition.environment==S::D::Environment::jungle;
        if(material){SDL_FRect source{0,land?0.f:32.f,320,land?28.f:60.f},dest{40,78,880,float(horizon-78)};SDL_RenderTexture(r,material,&source,&dest);}
        else box(40,78,880,float(horizon-78),{84,145,177,255});
        if(s.refuel()){
            // Native sky endpoints, blended ABOVE the horizon. Never paint a
            // detached pale stripe over the sea or stretch road bands into sky.
            const double start=78+(horizon-78)*.60;
            for(int y=78;y<int(std::ceil(horizon));++y){const double t=std::clamp((y-start)/(horizon-start),0.,1.);
                box(40,float(y),880,2,{Uint8(sky_top.r+(sky_edge.r-sky_top.r)*t),Uint8(sky_top.g+(sky_edge.g-sky_top.g)*t),Uint8(sky_top.b+(sky_edge.b-sky_top.b)*t),255});}
        }
        if(s.carrier())for(int y=78;y<int(std::ceil(horizon));++y){const auto c=s.definition.ocean.sky_at((y-78)/(horizon-78));box(40,float(y),880,2,{c[0],c[1],c[2],255});}
        const double ground=s.carrier()?-15:0;
        const double camera_height=camera.height+20-ground;
        for(int y=int(std::ceil(horizon));y<470;y+=s.carrier()?1:CustomSceneryField::band_height){
            if(s.carrier()){const auto c=s.definition.ocean.sea_at((y-horizon)/(470-horizon));box(40,float(y),880,2,{c[0],c[1],c[2],255});}
            else if(material){
                const double z=camera_height*camera.focal/std::max(1.,y-horizon)-camera.behind;
                const double travel=s.refuel()?s.elapsed*140:-s.range;
                const int row=s.carrier()?std::clamp(106+int((y-horizon)*117/(470-horizon)),106,223):s.refuel()?std::clamp(180+int((y-horizon)*43/(470-horizon)),180,223):160+int(std::fmod(std::max(0.,z+travel)*.6,60.));
                const float shift=float(std::fmod(s.x*2,40));
                SDL_FRect src{s.carrier()||s.refuel()?40+shift:0,float(row),s.carrier()||s.refuel()?240.f:28.f,1},dst{40,float(y),880,CustomSceneryField::band_height+1};SDL_RenderTexture(r,material,&src,&dst);
            }else box(40,float(y),880,CustomSceneryField::band_height+1,s.carrier()||s.refuel()?SDL_Color{34,85,116,255}:SDL_Color{111,110,61,255});
        }
        }
        if(s.definition.clouds && show_scenery)clouds_drawn=clouds.draw(r,horizon,s.x,s.pitch,s.refuel() && sky_top.r+sky_top.g+sky_top.b<100);
        if(scene)scene->draw_before(100,camera);
        const double ground=s.carrier()?-15:0;
        auto scenery=[&](double near,double length,bool sea){if(sprites && show_scenery){const auto n=sprites->draw_scenery(unsigned(s.definition.environment),terrain_camera,near,length,ground,s.definition.width,sea,s.ticks,&camera);scenery_drawn+=n.drawn;trees_drawn+=n.trees;waves_drawn+=n.waves;sky_aircraft+=n.aircraft;}};
        if(!s.refuel() && s.definition.rom_environment){
            const double near=s.surface_start(),far=near+s.definition.length;
            if(s.carrier()){
                scenery(near,s.definition.length,true);
                // The carrier owns its complete original deck, bridge, parked
                // jets and markings. Never draw the land runway on top of it.
                if(sprites){const double anchor=std::max(5.,near);auto p=camera.project(0,-10,anchor);
                    if(p.visible){const float width=float(camera.span(s.definition.width*1.3,anchor));
                        const float contact=sprites->carrier_contact_center(plane_y,width,-near/s.definition.length)+float(s.height*camera.focal/(anchor+camera.behind));
                        const float blend=float(std::clamp((100-near)/100.,0.,1.));
                        sprites->ship(p.x,p.y+(contact-p.y)*blend,width);carrier_drawn=true;}}
                else surface(camera,-s.definition.width/2,s.definition.width/2,near,far,0,{53,74,88,255});
            }else if(runway_art){
                // Original pavement/edge/stripe pixels, deprojected from the
                // parked ROM road layer and placed in the custom metric camera.
                // Scanline projection keeps the native stripes straight;
                // affine textured quads bend them across triangle diagonals.
                for(int y=int(camera.horizon)+1;y<470;y+=CustomSceneryField::band_height){
                    const double z=(camera.height+20)*camera.focal/(y+.5-camera.horizon)-camera.behind;
                    if(z<near || z>far)continue;
                    const auto left=camera.project(-s.definition.width/2,0,z),right=camera.project(s.definition.width/2,0,z);
                    const float v=float(1-std::fmod(z-near,64.)/64.);
                    SDL_FRect source{0,float(int(v*(CustomRunwayArt::height-1))),CustomRunwayArt::width,1};
                    SDL_FRect target{left.x,float(y),right.x-left.x,CustomSceneryField::band_height+1};SDL_RenderTexture(r,runway_art,&source,&target);++runway_rows;
                }
            }else{
                // Explicit ROM-free fallback, used only without local assets.
                surface(camera,-s.definition.width/2,s.definition.width/2,near,far,0,{79,85,82,255});
                for(int i=0;i<int(s.definition.length/35);++i)surface(camera,-.55,.55,near+i*35,near+i*35+15,.03,white);
            }
            if(!s.carrier())scenery(near,s.definition.length,false);
        }else if(s.refuel()){
            if(s.definition.rom_environment)scenery(-s.elapsed*140,2200,false);
            // The same physical basket/probe coordinates drive capture and
            // rendering. The ROM hose's last opaque pixel is the contact tip.
            auto basket=camera.project(CustomStageGuidance::probe_right,120+CustomStageGuidance::probe_up,s.range);
            // Calibrate the metric probe datum to the actual visible aircraft
            // silhouette. At capture, the nozzle overlaps the ROM aircraft itself.
            basket.x+=float(contact.x-probe.x);basket.y+=float(contact.y-probe.y);
            probe.x=contact.x;probe.y=contact.y;
            const float scale=float(camera.span(s.definition.tanker_span,s.range)/452.);
            const auto tip=sprites?sprites->tanker_tip():StagePoint{45,86};
            const float tx=float(basket.x-tip.x*scale),ty=float(basket.y-tip.y*scale);
            tanker_origin={tx,ty};tanker_scale=scale;
            if(sprites)sprites->refueling_tanker(tx,ty,scale);
            else{line(tx-226*scale,ty,tx+226*scale,ty,white);line(tx+45*scale,ty+6*scale,basket.x,basket.y,white);}
            probe_error_px=std::hypot(basket.x-visible_contact.x,basket.y-visible_contact.y);
            if(assistance){
                const auto aid=CustomStageGuidance::tanker(s.x,s.height,s.range,s.definition.start_distance,s.coupled,s.aligned());
                auto diamond=[&](float x,float y,float radius,SDL_Color c){const float weight=aid.locked?3:1;stroke(x,y-radius,x+radius,y,weight,c);stroke(x+radius,y,x,y+radius,weight,c);stroke(x,y+radius,x-radius,y,weight,c);stroke(x-radius,y,x,y-radius,weight,c);};
                const float cx=tx,cy=ty+12*scale,radius=84;
                diamond(cx,cy,radius,green);
                diamond(cx+float(aid.offset.x),cy+float(aid.offset.y),float(aid.radius),aid.locked?green:white);
            }
        }
        if(scene)scene->draw_before(200,camera);
        if(sprites && s.definition.player_shadow && !s.refuel()){
            const auto shadow=CustomStageShadow::at_height(s.height,s.bank);
            const double near=std::max(s.surface_start(),-camera.behind+2),far=s.surface_start()+s.definition.length;
            if(far>near){const auto a=camera.project(-s.definition.width/2,0,near),b=camera.project(s.definition.width/2,0,near),c=camera.project(s.definition.width/2,0,far),d=camera.project(-s.definition.width/2,0,far);
                const auto ground=camera.project(s.x+shadow.offset.x,0,shadow.offset.y),plane=camera.project(s.x,s.height,0);
                shadow_drawn=sprites->player_shadow(ground.x,ground.y+plane_y+12-plane.y,float(158*shadow.scale),shadow.opacity,shadow.softness,{{{a.x,a.y},{b.x,b.y},{c.x,c.y},{d.x,d.y}}});}
        }
        if(assistance && !s.refuel() && !s.takeoff() && s.phase==S::Phase::approach){
            const auto aid=CustomStageGuidance::landing(s.x,s.height,s.range,s.lateral,s.vertical,s.speed,s.definition.slope,camera.horizon,camera.focal);
            const auto c=green;
            const float x=aid.cross.x,y=aid.cross.y;
            const float cx=CustomStageGuidance::ils_reference.x,cy=CustomStageGuidance::ils_reference.y;
            // Each deviation bar slides on just one axis around the fixed datum.
            const float localizer_weight=aid.localizer_centered()?3:1,glide_weight=aid.glideslope_centered()?3:1;
            stroke(x,cy-105,x,cy+105,localizer_weight,c);
            stroke(x-10,cy-105,x+10,cy-105,localizer_weight,c);stroke(x-10,cy+105,x+10,cy+105,localizer_weight,c);
            stroke(cx-112,y,cx+112,y,glide_weight,c);
            stroke(cx-112,y-10,cx-112,y+10,glide_weight,c);stroke(cx+112,y-10,cx+112,y+10,glide_weight,c);
            ring(cx,cy,4,c);line(cx-12,cy,cx-5,cy,c);line(cx+5,cy,cx+12,cy,c);
            line(cx,cy-12,cx,cy-5,c);line(cx,cy+5,cx,cy+12,c);
            const float vx=aid.velocity.x,vy=aid.velocity.y;
            ring(vx,vy,7,white);line(vx-20,vy,vx-7,vy,white);line(vx+7,vy,vx+20,vy,white);line(vx,vy-15,vx,vy-7,white);

        }
        if(scene)scene->draw_before(300,camera);
        if(sprites && !s.refuel())sprites->ground_service(camera,s.definition.environment==S::D::Environment::jungle,s.carrier(),s.phase==S::Phase::service,s.phase_time,s.speed,s.ticks);
        if(sprites)sprites->fighter(480,plane_y,158,float(s.bank),s.burner(),s.ticks,s.refuel(),s.gear_down(),float(s.pitch),!s.refuel() && s.height<=0);
        else{line(410,plane_y,550,plane_y,white);line(480,plane_y-35,480,plane_y+15,white);}
        if(s.phase==S::Phase::rollout && s.phase_time<2.1 && sprites)smoke_puffs=sprites->brake_smoke(480,plane_y,s.phase_time);
        if(s.refuel() && s.coupled && sprites)sprites->refueling_hose(tanker_origin.x,tanker_origin.y,tanker_scale);
        if(scene)scene->draw_before(1000,camera);
        if(show_hud && s.definition.hud){
            const auto* native= sprites?sprites->hud():nullptr;
            if(native){auto* hud=sprites->hud();
                // Original SCORE/HIT surrounds, speed artwork, STAGE label and
                // missile symbols retain native pens; only layout/data are custom.
                hud->region(2,1,7,3,60,86);hud->region(20,2,4,1,674,98);
                hud->region(1,25,15,1,96,438);hud->region(17,25,4,1,600,448);
                // Keep the original ruler intact. A single continuous fill
                // avoids repeating native pointer fragments across the meter.
                box(96,455,180,5,{10,23,29,180});box(96,455,float(180*std::clamp(s.speed/150.,0.,1.)),5,white);
                for(unsigned i=0;i<10;++i)if(s.missiles>i*10){hud->tile(hud->entry(25,25),752+i*14,438);hud->tile(hud->entry(25,26),752+i*14,450);}
                hud_tiles=hud->tiles_drawn;
            }else{
                text(60,98,"SCORE",amber,1.25);text(674,98,"HIT",amber,1.25);
                text(96,438,"SPEED",white,1.25);text(600,448,"STAGE",white,1.25);
                for(unsigned i=0;i<10;++i)if(s.missiles>i*10)box(754+i*14,440,4,18,white);
            }
            text(154,98,"0000000",white,1.5);text(734,98,"0",white,1.5);
            text(660,448,std::to_string(stage_number),white,1.5);
            centered(90,s.definition.name,green,1.05);
            auto metric=[&](float x,float y,const char* label,double value,const char* unit){char buffer[80];SDL_snprintf(buffer,sizeof(buffer),"%s%s%.0f%s",label,*label?" ":"",value,unit);text(x,y,buffer,white,1);};
            metric(96,154,s.takeoff()?"RUN":"DIST",s.takeoff()?s.ground_run():s.range,"M");
            metric(768,154,s.refuel()?(s.height<0?"BELOW":"ABOVE"):"ALT",std::abs(s.height),"M");
            metric(96,418,"",s.speed," MPS");
            if(!s.refuel())metric(768,170,s.vertical<0?"DESCENT":"CLIMB",std::abs(s.vertical)," MPS");
            text(752,418,"MISSILES "+std::to_string(s.missiles),white,1);
            if(s.phase!=S::Phase::briefing){
                centered(122,"DOWN NOSE UP   SPACE BOOST   SHIFT SLOW",white,.95);
                centered(135,"H AID   M AUDIO   P PAUSE   R RETRY   TAB STAGE   ESC EXIT",muted,.85);
                if(paused)centered(150,"PAUSED",amber,1.5);
                else if(s.finished())centered(150,s.phase==S::Phase::success?"STAGE COMPLETE - ENTER TO RETRY":"APPROACH FAILED - ENTER TO RETRY",s.phase==S::Phase::success?green:amber,1);
                else if(assistance && !s.refuel() && !s.takeoff())centered(150,"ILS - CENTER BOTH BARS UNTIL BOLD",green,.9);
            }
            centered(426,s.message,s.phase==S::Phase::failed?amber:green,.9);
            if(s.refuel() || s.servicing()){const double progress=s.refuel()?s.rearm_progress:s.service_progress();
                box(340,451,210,6,{10,23,29,165});box(340,451,float(progress*210),6,green);}
            if(s.phase==S::Phase::briefing){box(198,178,564,176,{9,24,29,232});box(198,178,564,176,muted,false);
                centered(201,s.definition.name,white,1.25);
                centered(240,s.takeoff()?"FULL THROTTLE - BUILD SPEED - PULL BACK":s.refuel()?"MATCH 140 MPS - DOCK AT 12M":"FOLLOW THE ILS CROSS - PULL BACK TO FLARE",white,1);
                centered(264,s.refuel()?"HOLD CONTACT TO RELOAD MISSILES":"REARM MISSILES - FULL THROTTLE TO DEPART",muted,1);
                centered(289,"DOWN NOSE UP   SPACE BOOST   SHIFT SLOW",white,.9);
                centered(314,"ENTER START   TAB NEXT STAGE   ESC EXIT",green,1);
            }
        }
        SDL_SetRenderClipRect(r,nullptr);
        if(scene)scene->draw_before(std::numeric_limits<double>::infinity(),camera);
    }
};
}

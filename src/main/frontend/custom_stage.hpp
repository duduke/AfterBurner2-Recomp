#pragma once
#include "engine/game_controls.hpp"
#include "frontend/custom_stage_guidance.hpp"
#include "frontend/custom_stage_layers.hpp"
#include "frontend/custom_stage_atmosphere.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace Frontend {
// Authored stage rules and metric camera. These never drive the arcade board.
struct CustomStageDefinition {
    enum class Objective {landing,takeoff,refuel};
    enum class Environment {carrier,runway,jungle,sea};
    Objective objective=Objective::landing;
    Environment environment=Environment::runway;
    std::string name="RUNWAY / LANDING";
    bool seamless_takeoff=true,rom_environment=true,player_shadow=true,clouds=true,hud=true;CustomStageOcean ocean;unsigned tanker_variant=0;
    std::filesystem::path asset_directory;std::vector<CustomStageLayer> layers;
    double length=2200,width=45,start_distance=1400,start_height=88,speed=80,slope=.06,tanker_span=50;
    static CustomStageDefinition preset(unsigned i){
        CustomStageDefinition d;
        if(i==0){d.objective=Objective::refuel;d.environment=Environment::sea;d.name="TANKER / RENDEZVOUS";d.start_distance=220;d.start_height=-5;d.speed=140;}
        if(i==1 || i==5){d.environment=Environment::carrier;d.name="CARRIER";d.length=300;d.width=70;d.start_distance=1200;d.start_height=76;}
        if(i==3 || i==6){d.environment=Environment::jungle;d.name="JUNGLE RUNWAY";}
        if(i==2 || i==4)d.name="AUTUMN RUNWAY";
        if(i>=4){d.objective=Objective::takeoff;d.start_distance=0;d.start_height=0;d.speed=0;d.name+=" / TAKEOFF";}
        else if(i!=0)d.name+=" / LANDING";
        return d;
    }
    static CustomStageDefinition load(const std::string& path){
        const auto root=StageJson::load(path);
        root.keys({"$schema","version","name","objective","environment","length","width","start_distance","start_height","speed","glideslope","tanker_span","seamless_takeoff","presentation","layers"});
        if(auto schema=root.find("$schema"))schema->text();
        const auto version=root.find("version");if(!version || version->numeric()!=1)throw std::runtime_error("Stage JSON requires version 1");
        CustomStageDefinition d;d.asset_directory=std::filesystem::absolute(path).parent_path();
        if(auto v=root.find("name")){d.name=v->text();if(d.name.empty() || d.name.find('\0')!=std::string::npos)throw std::runtime_error("Invalid stage name");}
        if(auto v=root.find("objective")){auto s=v->text();if(s=="landing")d.objective=Objective::landing;else if(s=="takeoff")d.objective=Objective::takeoff;else if(s=="refuel")d.objective=Objective::refuel;else throw std::runtime_error("Unknown stage objective");}
        if(auto v=root.find("environment")){auto s=v->text();if(s=="carrier")d.environment=Environment::carrier;else if(s=="runway")d.environment=Environment::runway;else if(s=="jungle")d.environment=Environment::jungle;else if(s=="sea")d.environment=Environment::sea;else throw std::runtime_error("Unknown stage environment");}
        if(auto v=root.find("seamless_takeoff"))d.seamless_takeoff=v->flag();
        if(auto v=root.find("presentation")){v->keys({"rom_environment","ocean","player_shadow","tanker_variant","clouds","hud"});
            if(auto r=v->find("tanker_variant")){if(r->kind==StageJson::Kind::string){if(r->text()!="random")throw std::runtime_error("Unknown tanker variant selection");}
                else{const double n=r->numeric();if(n<1 || n>64 || std::floor(n)!=n)throw std::runtime_error("Invalid native tanker stage number");d.tanker_variant=unsigned(n);}}if(auto r=v->find("hud"))d.hud=r->flag();if(auto r=v->find("clouds"))d.clouds=r->flag();if(auto r=v->find("player_shadow"))d.player_shadow=r->flag();if(auto r=v->find("ocean"))d.ocean.load(*r);if(auto r=v->find("rom_environment"))d.rom_environment=r->flag();}
        if(auto v=root.find("layers"))d.layers=load_stage_layers(*v);
        for(auto [key,dest]:{std::pair{"length",&d.length},{"width",&d.width},{"start_distance",&d.start_distance},{"start_height",&d.start_height},{"speed",&d.speed},{"glideslope",&d.slope},{"tanker_span",&d.tanker_span}})if(auto v=root.find(key))*dest=v->numeric();
        if(d.length<250 || d.length>10000 || d.width<20 || d.width>200 || d.start_distance<0 || d.start_distance>10000 ||
           d.start_height< -50 || d.start_height>2000 || d.speed<0 || d.speed>250 || d.slope<.01 || d.slope>.2 || d.tanker_span<20 || d.tanker_span>100)
            throw std::runtime_error("Stage dimensions outside supported authoring bounds");
        if(d.objective!=Objective::refuel && d.environment==Environment::sea)throw std::runtime_error("Landing/takeoff needs runway or carrier");
        return d;
    }
};
struct StageCamera {
    double x{},height{},horizon=198,focal=480,behind=55;
    struct Point{float x,y;bool visible;};
    Point project(double wx,double wy,double z)const{
        const double depth=z+behind;
        if(depth<2)return {0,0,false};
        return {float(480+(wx-x)*focal/depth),float(horizon+(height+20-wy)*focal/depth),true};
    }
    double span(double meters,double z)const{return meters*focal/std::max(2.,z+behind);}
};
struct CustomStage {
    using D=CustomStageDefinition;using O=D::Objective;
    enum class Phase{briefing,approach,contact,success,failed,rollout,ready,accelerating,climb,service,taxi};
    static constexpr double dt=1./60;
    D definition;Phase phase=Phase::briefing;
    double x{},height{},range{},speed{},pitch{},bank{},vertical{},lateral{},elapsed{},fuel{},rearm_progress{},power{};
    bool departing{},coupled{},throttle_armed{};double phase_time{},surface_offset{},departure_origin{};unsigned missiles=50;
    unsigned ticks{};const char* message="ENTER / START TO BEGIN";
    explicit CustomStage(D d):definition(std::move(d)){reset();}
    void reset(){phase=Phase::briefing;x=definition.objective==O::takeoff?0:8;height=definition.start_height;range=definition.start_distance;speed=definition.speed;
        pitch=bank=vertical=lateral=elapsed=power=rearm_progress=0;fuel=1;departing=coupled=throttle_armed=false;phase_time=0;surface_offset=definition.objective==O::takeoff?0:aim_offset();departure_origin=range;missiles=definition.objective==O::takeoff?100:50;ticks=0;message="ENTER / START TO BEGIN";}
    void start(){if(phase==Phase::briefing){phase=definition.objective==O::takeoff?Phase::ready:Phase::approach;message=takeoff()?"FULL THROTTLE TO START TAKEOFF":"FLY THE STAGE";}}
    bool finished()const{return phase==Phase::success || phase==Phase::failed;}
    bool carrier()const{return definition.environment==D::Environment::carrier;}
    bool refuel()const{return definition.objective==O::refuel;}
    bool takeoff()const{return definition.objective==O::takeoff || departing;}
    bool servicing()const{return phase==Phase::service || phase==Phase::taxi;}
    double surface_start()const{return range-surface_offset;}
    double ground_run()const{return departure_origin-range;}
    double contact_distance()const{return CustomStageGuidance::contact_distance(x,height,range);}
    bool gear_down()const{return !refuel() && (height<15 || servicing() || phase==Phase::ready);}
    double service_progress()const{return std::clamp((phase_time-2)/6.,0.,1.);}
    double aim_offset()const{return carrier()?55:180;}
    double target_height()const{return refuel()?0:std::max(0.,range)*definition.slope+2;}
    bool aligned()const{return contact_distance()<=CustomStageGuidance::capture_radius && std::abs(bank)<.16 && std::abs(pitch)<.16 && std::abs(speed-140)<1;}
    bool burner()const{return takeoff() && power>=.95 && phase!=Phase::ready && phase!=Phase::briefing && phase!=Phase::failed;}
    void end(bool good,const char* why){phase=good?Phase::success:Phase::failed;message=why;}
    void tick(GameControls input){
        if(phase==Phase::briefing || finished())return;
        const auto clean=[](float v){return std::isfinite(v)?std::clamp(double(v),-1.,1.):0.;};
        power=clean(input.throttle);const double py=clean(input.stick_y),bx=clean(input.stick_x);
        pitch+=(py-pitch)*std::min(1.,dt*5);bank+=(bx-bank)*std::min(1.,dt*5);elapsed+=dt;++ticks;phase_time+=dt;
        if(phase==Phase::service){power=0;pitch=bank=vertical=lateral=0;
            if(phase_time>=2){missiles=std::min(100u,50+unsigned(service_progress()*50));}
            message=phase_time<2?"REARMING CREW APPROACHING":phase_time<8?"REARMING MISSILES":"REARM COMPLETE / CREW CLEARING";
            if(phase_time>=10){missiles=100;phase_time=0;departing=true;departure_origin=range;
                phase=carrier()?Phase::taxi:Phase::ready;message=carrier()?"DECK CREW / TOWING TO CATAPULT":"REARM COMPLETE / FULL THROTTLE TO TAKE OFF";}
            return;}
        if(phase==Phase::taxi){power=0;pitch=bank=vertical=lateral=0;
            range=std::min(surface_offset,range+35*dt);
            if(range>=surface_offset){phase=Phase::ready;phase_time=0;departure_origin=range;message="CATAPULT READY / FULL THROTTLE TO TAKE OFF";}return;}
        lateral=bank*(refuel()?13:20);x+=lateral*dt;
        if(phase==Phase::rollout){speed=std::max(0.,speed-(carrier()?36:16)*dt);range-=speed*dt;vertical=0;
            if(std::abs(x)>definition.width*.45)end(false,"LEFT THE LANDING SURFACE");
            else if(range<-(definition.length-aim_offset()))end(false,"ROLLOUT OVERRUN");
            else if(speed==0){if(definition.seamless_takeoff){phase=Phase::service;phase_time=0;message="LANDED / REARM MISSILES";}else end(true,"LANDING COMPLETE / AIRCRAFT STOPPED");}return;}
        if(takeoff()){
            if(phase==Phase::ready){speed=0;pitch=bank=0;message=carrier()?"CATAPULT READY / FULL THROTTLE TO TAKE OFF":"REARM COMPLETE / FULL THROTTLE TO TAKE OFF";if(!throttle_armed && power>=.25)message="RELEASE THROTTLE / THEN FULL POWER TO TAKE OFF";if(power<.25)throttle_armed=true;if(throttle_armed && power>=.95){phase=Phase::accelerating;message="FULL POWER / BUILD SPEED";}return;}
            speed=std::clamp(speed+((carrier()?37:14)*std::max(0.,power)-2*std::max(0.,-power))*dt,0.,150.);
            range-=speed*dt;
            if(speed>68 && pitch>.12){vertical=std::max(0.,pitch*22);height+=vertical*dt;phase=Phase::climb;}
            else if(height>0){vertical=-3;height=std::max(0.,height+vertical*dt);}
            message=speed<70?"FULL POWER / BUILD SPEED":"PULL BACK TO ROTATE / CLIMB";
            if(std::abs(x)>definition.width*.45 && height<4)end(false,"LEFT THE DEPARTURE SURFACE");
            else if(definition.length+surface_start()<30 && height<5)end(false,"OVERRUN / ROTATE EARLIER");
            else if(height>40)end(true,"TAKEOFF COMPLETE / POSITIVE CLIMB");
            return;
        }
        const double target_speed=refuel()?140+power*25:definition.speed+power*25;
        speed+=(target_speed-speed)*dt*1.5;
        if(refuel()){
            if(!coupled){vertical=pitch*10;height+=vertical*dt;range-=(speed-140)*dt;
                if(aligned())coupled=true;}
            if(coupled){x=height=bank=pitch=vertical=lateral=power=0;range=CustomStageGuidance::probe_forward;speed=140;phase=Phase::contact;rearm_progress=std::min(1.,rearm_progress+dt/4);missiles=std::min(100u,50+unsigned(rearm_progress*50));}
            else{phase=Phase::approach;}
            message=coupled?"DIAMONDS LOCKED / RELOADING MISSILES":contact_distance()<=CustomStageGuidance::capture_radius?"MATCH SPEED / LEVEL WINGS TO DOCK":"MATCH THE DIAMONDS / DOCK THE PROBE";
            if(missiles>=100)end(true,"REARM COMPLETE / PROBE CONNECTED");
            else if(range<3)end(false,"TOO CLOSE TO TANKER");
        }else{
            vertical=-speed*definition.slope+pitch*9;
            if(height<10 && pitch>0)vertical=std::max(vertical,-2.2);
            height+=vertical*dt;range-=speed*dt;
            message=height<12?"FLARE / WINGS LEVEL":"FOLLOW THE CROSS / FLIGHT PATH TO AIM POINT";
            if(height<=0){height=0;
                const bool window=range<25 && range>-(definition.length-aim_offset()-30);
                if(window && std::abs(x)<(carrier()?14:definition.width*.4) && vertical>=-5 && std::abs(bank)<.55 && speed>=55 && speed<=115){phase=Phase::rollout;phase_time=0;message="TOUCHDOWN / BRAKING";}
                else end(false,window?"HARD OR MISALIGNED TOUCHDOWN":"MISSED THE LANDING SURFACE");}
            else if(range<-(definition.length-aim_offset()))end(false,"MISSED APPROACH / GO AROUND");
        }
        if(elapsed>100)end(false,"STAGE TIME LIMIT");
    }
    GameControls demo()const{
        GameControls c;c.stick_x=float(std::clamp(-x*.18-bank*.65,-1.,1.));
        if(servicing())return c;
        if(takeoff()){c.throttle=phase==Phase::ready && !throttle_armed?0:1;c.stick_y=speed>72?.65f:0;}
        else if(refuel()){c.stick_y=float(std::clamp(-height*.3-pitch*.7,-1.,1.));c.throttle=float(std::clamp((range-CustomStageGuidance::probe_forward)*.045-(speed-140)*.09,-1.,1.));}
        else{const double sink=height<4?1.7:std::clamp((height-target_height())*.75+speed*definition.slope,1.,10.);
            c.stick_y=float(std::clamp((speed*definition.slope-sink)/9.,-1.,1.));}
        return c;
    }
};
}

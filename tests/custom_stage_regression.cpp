#include "frontend/custom_stage.hpp"
#include "frontend/custom_stage_tire_sound.hpp"
#include "frontend/custom_stage_shadow.hpp"
#include "frontend/custom_stage_variants.hpp"
#include "frontend/custom_runway_art.hpp"
#include "frontend/custom_stage_scenery.hpp"
#include <cassert>
#include <iostream>
using namespace Frontend;
int main(){
    for(unsigned i=0;i<7;++i){CustomStage s(CustomStageDefinition::preset(i));s.start();
        for(int f=0;f<6000 && !s.finished();++f)s.tick(s.demo());
        std::cout<<i<<" phase="<<int(s.phase)<<" "<<s.message<<" range="<<s.range<<" h="<<s.height<<'\n';assert(s.phase==CustomStage::Phase::success);}
    for(unsigned kind:{1u,2u,3u}){
        CustomStage cycle(CustomStageDefinition::preset(kind));cycle.start();bool service=false,taxi=false,depart=false;
        const double initial_fuel=cycle.fuel;double previous=cycle.surface_start();
        for(unsigned f=0;f<6000 && cycle.phase!=CustomStage::Phase::ready;++f){cycle.tick(cycle.demo());assert(cycle.fuel==initial_fuel);service|=cycle.phase==CustomStage::Phase::service;taxi|=cycle.phase==CustomStage::Phase::taxi;
            assert(std::abs(cycle.surface_start()-previous)<3);previous=cycle.surface_start();}
        assert(service && cycle.phase==CustomStage::Phase::ready && cycle.fuel==1 && cycle.missiles==100);assert(taxi==(kind==1));
        const double parked=cycle.surface_start();
        GameControls held;held.throttle=1;for(unsigned f=0;f<60;++f)cycle.tick(held);
        assert(cycle.phase==CustomStage::Phase::ready && !cycle.burner());
        for(unsigned f=0;f<120;++f)cycle.tick({});assert(cycle.surface_start()==parked && cycle.speed==0 && !cycle.burner());
        for(unsigned f=0;f<1000 && !cycle.finished();++f){cycle.tick(cycle.demo());depart|=cycle.phase==CustomStage::Phase::climb;}
        assert(depart && cycle.phase==CustomStage::Phase::success);
    }
    CustomStage dock(CustomStageDefinition::preset(0));dock.start();dock.x=dock.height=0;dock.range=19;
    for(unsigned f=0;f<120;++f)dock.tick({});assert(!dock.coupled && dock.missiles==50 && dock.fuel==1);
    dock.range=CustomStageGuidance::probe_forward;
    for(unsigned f=0;f<300 && !dock.finished();++f)dock.tick({});
    assert(dock.coupled && dock.missiles==100 && dock.fuel==1 && dock.contact_distance()==0);
    CustomStage locked(CustomStageDefinition::preset(0));locked.start();locked.x=locked.height=0;locked.range=CustomStageGuidance::probe_forward;
    locked.tick({});assert(locked.coupled && locked.phase==CustomStage::Phase::contact);
    GameControls disturbance;disturbance.stick_x=disturbance.stick_y=disturbance.throttle=1;
    for(unsigned i=0;i<300 && !locked.finished();++i){const auto before=locked.missiles;locked.tick(disturbance);
        assert(locked.coupled && locked.x==0 && locked.height==0 && locked.bank==0 && locked.pitch==0 && locked.speed==140);
        assert(locked.contact_distance()==0 && locked.missiles>=before);}
    assert(locked.finished() && locked.missiles==100 && locked.fuel==1);
    const auto latched_aid=CustomStageGuidance::tanker(10,5,19,220,true);
    assert(latched_aid.locked && latched_aid.offset.x==0 && latched_aid.offset.y==0 && latched_aid.radius==84);
    const auto acquiring_aid=CustomStageGuidance::tanker(0,0,12,220,false,true);
    assert(acquiring_aid.locked && acquiring_aid.radius==84 && acquiring_aid.offset.x==0 && acquiring_aid.offset.y==0);
    const auto near_aid=CustomStageGuidance::tanker(0,0,12.01,220,false);
    assert(!near_aid.locked && near_aid.radius<=83 && near_aid.radius>82);locked.reset();assert(!locked.coupled);
    for(auto [pitch,pose]:{std::pair{-1.,0u},{-.5,1u},{0.,2u},{.5,3u},{1.,4u}})assert(CustomStageGuidance::pitch_pose(pitch)==pose);
    CustomStageTireSound tire;assert(tire.sample(80,true)==0);tire.start();assert(tire.sample(80,true)==0);
    double chirp_energy=0,late_energy=0;for(unsigned i=0;i<48000;++i){double v=tire.sample(80,false);assert(std::abs(v)<5000);if(i<10000)chirp_energy+=v*v;else if(i>38000)late_energy+=v*v;}
    assert(chirp_energy>1.e9 && late_energy<chirp_energy*.001);
    tire.reset();assert(tire.sample(80,true)==0);
    const auto contact_shadow=CustomStageShadow::at_height(0),low_shadow=CustomStageShadow::at_height(10),high_shadow=CustomStageShadow::at_height(60);
    assert(contact_shadow.offset.x==0 && contact_shadow.offset.y==0);
    assert(contact_shadow.scale>low_shadow.scale && low_shadow.scale>high_shadow.scale);
    assert(contact_shadow.opacity>low_shadow.opacity && low_shadow.opacity>high_shadow.opacity);
    assert(contact_shadow.softness<low_shadow.softness && low_shadow.softness<high_shadow.softness);
    const std::array<StagePoint,4> receiver{{{-1,-1},{1,-1},{1,1},{-1,1}}};
    const auto clipped=CustomStageShadow::clip({{{-2,-1},{0,0}},{{2,-1},{1,0}},{{2,1},{1,1}},{{-2,1},{0,1}}},receiver);
    assert(clipped.size()==4);for(auto v:clipped){assert(std::abs(v.point.x)<=1 && std::abs(v.point.y)<=1);assert(v.uv.x>=.25 && v.uv.x<=.75);}
    assert(CustomStageShadow::clip({{{2,2},{0,0}},{{3,2},{1,0}},{{3,3},{1,1}}},receiver).empty());
    CustomStageOcean ocean;assert(ocean.sky_at(1)==ocean.sea_at(0));assert(ocean.sea_at(1)==ocean.water);
    assert(ocean.sky_at(.5)==ocean.sky && ocean.sea_at(.2)==ocean.water);
    const auto level=CustomStageGuidance::landing(0,62,1000,0,-4.8,80,.06,198,480);
    const auto reference=CustomStageGuidance::ils_reference;
    assert(level.localizer_centered() && level.glideslope_centered());
    assert(std::abs(level.cross.x-reference.x)<1.e-8 && std::abs(level.cross.y-reference.y)<1.e-8);
    assert(std::abs(level.velocity.x-480)<1.e-8 && std::abs(level.velocity.y-(198+480*.06))<1.e-8);
    const auto lateral_only=CustomStageGuidance::landing(20,62,1000,0,-4.8,80,.06,198,480);
    const auto high=CustomStageGuidance::landing(0,82,1000,0,-4.8,80,.06,198,480);
    assert(!lateral_only.localizer_centered() && lateral_only.glideslope_centered());
    assert(high.localizer_centered() && !high.glideslope_centered());
    assert(lateral_only.cross.x<reference.x && lateral_only.cross.y==level.cross.y);
    assert(high.cross.x==level.cross.x && high.cross.y>reference.y);
    const auto pitched=CustomStageGuidance::landing(20,82,1000,0,-4.8,80,.06,220,480);
    const auto high_right=CustomStageGuidance::landing(20,82,1000,0,-4.8,80,.06,198,480);
    assert(high_right.cross.x<level.cross.x && high_right.cross.y>level.cross.y);
    assert(pitched.cross.x==high_right.cross.x && pitched.cross.y==high_right.cross.y);
    assert(pitched.velocity.y!=high_right.velocity.y);
    const auto low_left=CustomStageGuidance::landing(-20,42,1000,0,-4.8,80,.06,198,480);
    assert(low_left.cross.x>reference.x && low_left.cross.y<reference.y);
    const auto extreme=CustomStageGuidance::landing(1000,1000,50,0,-4.8,80,.06,198,480);
    assert(extreme.cross.x==reference.x-100 && extreme.cross.y==reference.y+90);
    assert(CustomStageGuidance::closure(150,220)<CustomStageGuidance::closure(20,220));
    for(double bank:{-25.,0.,25.})for(bool flip:{false,true})for(StagePoint root: {StagePoint{-5,-12},StagePoint{6,-8}}){
        const StagePoint engine{480,385};const auto centre=anchored_stage_image(engine,root,1.3,bank,flip);
        if(flip)root.x=-root.x;const auto delta=rotate_stage_point({root.x*1.3,root.y*1.3},bank);
        assert(std::hypot(centre.x+delta.x-engine.x,centre.y+delta.y-engine.y)<1.e-8);
    }
    StageCamera c;assert(c.span(50,1000)<c.span(50,100));assert(!c.project(0,0,-100).visible);
    assert(c.project(10,0,100).x>c.project(0,0,100).x);assert(c.project(0,10,100).y<c.project(0,0,100).y);
    CustomStage t(CustomStageDefinition::preset(4));t.start();
    for(int f=0;f<300;++f)t.tick({});assert(t.speed==0 && !t.burner() && t.phase==CustomStage::Phase::ready);
    GameControls full;full.throttle=1;t.tick(full);assert(t.burner());assert(std::string(t.message).find("FULL THROTTLE")==std::string::npos);
    auto landing_only=CustomStageDefinition::preset(1);landing_only.seamless_takeoff=false;CustomStage land(landing_only);land.start();land.height=.001;land.range=0;land.x=0;land.pitch=.5;
    GameControls flare;flare.stick_y=.5;land.tick(flare);assert(land.phase==CustomStage::Phase::rollout);
    for(int f=0;f<600;++f)land.tick({});assert(land.phase==CustomStage::Phase::success && land.speed==0 && !land.takeoff());
    CustomStage bad(CustomStageDefinition::preset(2));bad.start();bad.x=100;bad.height=.001;bad.tick({});assert(bad.phase==CustomStage::Phase::failed);
    CustomStage partial(CustomStageDefinition::preset(4));partial.start();GameControls half;half.throttle=.9f;
    for(int f=0;f<60;++f)partial.tick(half);assert(partial.phase==CustomStage::Phase::ready && !partial.burner());
    CustomStage malformed(CustomStageDefinition::preset(0));malformed.start();GameControls nan;nan.throttle=nan.stick_x=nan.stick_y=std::nanf("");malformed.tick(nan);assert(std::isfinite(malformed.height) && std::isfinite(malformed.x));
    const std::vector<unsigned> variants{3,9,11,16,19,21};CustomStageVariantPicker first(123),second(123);
    unsigned previous=0;for(unsigned cycle=0;cycle<5;++cycle){std::vector<unsigned> seen;
        for(unsigned i=0;i<variants.size();++i){auto next=first.pick(variants);assert(next==second.pick(variants));assert(next!=previous);previous=next;seen.push_back(next);}
        std::sort(seen.begin(),seen.end());assert(seen==variants);}
    assert(first.pick(variants,9)==9);bool bad_variant=false;try{first.pick(variants,5);}catch(const std::runtime_error&){bad_variant=true;}assert(bad_variant);
    const char* files[]={"refuel","carrier-landing","runway-landing","jungle-landing","runway-takeoff","carrier-takeoff","jungle-takeoff"};
    for(auto file:files)CustomStageDefinition::load(std::string("assets/custom_stages/")+file+".json");
    const auto unicode=StageJson::parse(R"({"name":"Flight \u2708 \ud83d\ude80","enabled":true,"values":[null,-1.2e+2]})");
    assert(unicode.find("name")->text()=="Flight ✈ 🚀" && unicode.find("enabled")->flag());
    assert(unicode.find("values")->array[1].numeric()==-120);
    for(const auto& invalid: {"{\"x\":1,\"x\":2}","[1,]","{\"a\":true,}","01","1e999","NaN","true false","\"\\ud800\"","\"\\udc00\""}){
        bool bad=false;try{StageJson::parse(invalid);}catch(const std::runtime_error&){bad=true;}assert(bad);
    }
    const auto many=StageJson::parse(R"([{"name":"rgba","order":900,"elements":[{"type":"rectangle","color":[17,103,219,127],"repeat":[4096,4096]}]},{"order":-20,"elements":[]}])");
    const auto layers=load_stage_layers(many);assert(layers.size()==2 && layers[0].order==-20);
    assert(layers[1].elements[0].columns==4096 && layers[1].elements[0].rows==4096 && layers[1].elements[0].color[3]==127);
    std::vector<uint32_t> source(320*224,0xffbb6600u);
    for(unsigned y=0;y<224;++y)for(unsigned x=90;x<=230;++x)source[y*320+x]=0xff000000u|(y<<16)|(x<<8)|y;
    const auto road=CustomRunwayArt::extract(source);
    assert(road.size()==CustomRunwayArt::width*CustomRunwayArt::height);
    // The original road's rows and stripe colors must survive rescaling.
    for(unsigned y=0;y<CustomRunwayArt::height;++y)for(unsigned x=0;x<CustomRunwayArt::width;++x){
        const auto pixel=road[y*CustomRunwayArt::width+x];assert(((pixel>>16)&255)==y+80);assert((pixel&255)==y+80);assert(((pixel>>8)&255)>=90 && ((pixel>>8)&255)<=230);}
    bool rejected=false;try{CustomRunwayArt::extract(std::vector<uint32_t>(320*224,0xff000000u));}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    StageCamera field_camera;field_camera.height=20;
    const auto field=CustomSceneryField::visible(field_camera,900,45,false,0);
    const auto shifted=CustomSceneryField::visible(field_camera,899,45,false,0);
    assert(field.size()>256 && CustomSceneryField::band_height>=3);
    unsigned left=0,right=0,stable=0;
    for(const auto a:field){assert(std::abs(a.x)>=45./2+14);left+=a.x<0;right+=a.x>0;
        for(const auto b:shifted)if(a.variation==b.variation && a.x==b.x && std::abs((a.z-900)-(b.z-899))<1.e-8){++stable;break;}}
    assert(left>100 && right>100 && stable>256);
    assert(CustomSceneryField::visible(field_camera,0,70,true,-15).size()>256);
    std::cout<<"custom stages: seven objectives, seamless service/departure cycles, physical docking, ILS, anchored flames and authoring passed\n";
}

#include "wide_ground_reference.hpp"
#include "engine/game_session.hpp"
#include "hwvideo/wide_scene.hpp"
#include "wide_native_reference.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
static void require(bool c,const char* message){if(!c)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
    require(roms.load_aburner2(),"ROMs required");uint64_t frames=0,changes=0,retained=0;
    std::filesystem::create_directories("build/oracle/widescreen");
    const unsigned stage_count=NativeStageSkip::stage_count(roms);
    require(stage_count==23,"ordered ROM stage census changed");
    const unsigned requested=argc==3&&std::string_view(argv[1])=="--stage"?std::stoul(argv[2]):0;
    require(argc==1||(argc==3&&requested>=1&&requested<=stage_count),"Usage: wide_stage_visibility [--stage 1..23]");
    for(unsigned stage=requested?requested:1;stage<=(requested?requested:stage_count);++stage){
        std::cout<<"Wide stage "<<stage<<": boot"<<std::endl;
        GameSession reference(roms),captured(roms);captured.capture_wide_scene(true);
        auto step=[&](GameControls c){reference.advance(c);captured.advance(c);auto a=reference.frame(),b=captured.frame();
            require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"wide stage observer changed time");
            require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto x,auto y){return x.r==y.r&&x.g==y.g&&x.b==y.b;}),"wide stage observer changed native RGB");
            require(reference.take_audio()==captured.take_audio(),"wide stage observer changed native PCM");++frames;};
        for(unsigned f=0;f<600;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;step(c);}
        reference.debug_skip_to_stage(stage);captured.debug_skip_to_stage(stage);captured.capture_feedback(true);
        Presentation::WideScene plain(roms),corrected(roms);WideNativeReference legacy(roms);unsigned directions[2]{};
        for(unsigned f=0;f<360;++f){GameControls c;c.stick_x=(f/60)%2?1.f:-1.f;c.stick_y=f<120?1.f:f<240?-1.f:0.f;c.throttle=1;c.cannon=true;c.missile=f%60<4;step(c);
            if(f%120==0)std::cout<<"Wide stage "<<stage<<": render "<<f<<"/360"<<std::endl;
            auto scene=captured.wide_scene();require(scene.ready,"wide stage frontier missing");
            legacy.check(scene.clips,scene.info.number,reference.frame().pixels);
            plain.render(scene.clips,scene.info.number,false);corrected.render(scene.clips,scene.info.number,true);
            auto p=plain.view(),q=corrected.view();require(p.ready&&q.ready,"wide stage row coverage missing");
            require(std::equal(p.sprites.begin(),p.sprites.end(),q.sprites.begin()),"wide stage ground correction changed sprites");
            for(unsigned i=0;i<p.ground_indices.size();++i)if(p.ground_indices[i]!=q.ground_indices[i]){
                require(valid_wide_ground_change(p,q,i),"wide stage correction overwrote terrain");++changes;}
            for(auto& clip:scene.clips)for(auto& d:clip.extended_sprites)retained+=d.kind==2;
            ++directions[c.stick_x>0];
            if(f==210||f==270){std::ofstream image("build/oracle/widescreen/stage-"+std::to_string(stage)+"-"+std::to_string(f)+".ppm",std::ios::binary);image<<"P6\n398 224\n255\n";
                for(unsigned i=0;i<q.background.size();++i){auto color=q.sprites[i]?q.sprites[i]:q.background[i];for(unsigned shift:{16u,8u,0u})image.put(char(color>>shift));}}
            if(f==180)captured.capture_feedback(false);
        }
        require(directions[0]==180&&directions[1]==180,"wide stage direction windows missing");
        captured.set_paused(true);const auto paused=captured.info();
        require(!captured.advance({})&&captured.info().number==paused.number&&captured.info().time_ns==paused.time_ns,"wide stage pause advanced native board");
        captured.set_paused(false);
        for(unsigned n=0;n<3;++n){captured.capture_wide_scene(false);step({});require(!captured.wide_scene().ready,"wide mode disable retained clips");
            captured.capture_wide_scene(true);plain.reset();corrected.reset();step({});auto scene=captured.wide_scene();require(scene.ready,"wide mode re-enable lost capture");
            corrected.render(scene.clips,scene.info.number,true);require(corrected.view().ready,"wide mode re-enable lost rows");}
        reference.restart();captured.restart();plain.reset();corrected.reset();
        require(!captured.wide_scene().ready,"wide cold restart retained clips");
        for(unsigned n=0;n<12;++n){step({});auto scene=captured.wide_scene();require(scene.ready,"wide cold restart missing capture");corrected.render(scene.clips,scene.info.number,true);require(corrected.view().ready,"wide cold restart missing rows");}
        std::cout<<"Wide directed stage "<<stage<<": 360 input-window frames; native RGB/PCM/time unchanged; both direction windows 180/180; ground preserves terrain"<<std::endl;
    }
    std::cout<<"Wide additional stages: "<<frames<<" native RGB/PCM/time frames, "<<changes<<" eligible sky-fill corrections, "<<retained<<" retained scenery descriptor observations\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

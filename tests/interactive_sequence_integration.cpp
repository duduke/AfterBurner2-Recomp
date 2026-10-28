#include "engine/interactive_sequence_player.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <map>
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main()try{
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    unsigned comparisons=0,presentations=0;
    for(auto surface:{InteractiveSequencePlayer::Surface::carrier,InteractiveSequencePlayer::Surface::runway,InteractiveSequencePlayer::Surface::jungle}){
        InteractiveSequencePlayer player(roms,surface);
        XBoardGame reference(roms);
        auto tick=[&](GameControls c={}){auto p=arcade_inputs(c);XBoardGame::Host::Input in;in.buttons=p.buttons;in.analog=p.analog;reference.tick(in);reference.take_audio();};
        if(surface!=InteractiveSequencePlayer::Surface::carrier){
            for(unsigned f=0;f<600;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;tick(c);}
            NativeStageSkip::enter(reference.board(),roms,surface==InteractiveSequencePlayer::Surface::runway?5:13,[&]{tick();});
        }
        auto compare=[&]{
            require(reference.board().frames==player.native_frames(),"Native frame identity differs");
            const auto& a=reference.pixels();const auto b=player.native_pixels();
            require(a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](auto x,auto y){return x.r==y.r&&x.g==y.g&&x.b==y.b;}),"Native continuation RGB differs from direct board");++comparisons;
        };
        while(reference.board().frames<player.native_frames()){
            GameControls c;auto f=reference.board().frames;
            if(surface==InteractiveSequencePlayer::Surface::carrier){c.coin=f>=360&&f<372;c.start=f>=420&&f<432;}tick(c);
        }
        // Material extraction must leave native presentation and board time
        // untouched, and must exclude the HUD/aircraft from its output.
        const auto frame_before=reference.board().frames;
        const auto pixels_before=reference.pixels();
        const auto material=reference.terrain_material();
        require(material.size()==320*224 && material==reference.terrain_material(),"Material export is not stable");
        require(reference.board().frames==frame_before,"Material export advanced the board");
        require(std::equal(pixels_before.begin(),pixels_before.end(),reference.pixels().begin(),[](auto a,auto b){return a.r==b.r&&a.g==b.g&&a.b==b.b;}),"Material export changed native RGB");
        unsigned detached=0;for(unsigned i=0;i<material.size();++i){auto c=pixels_before[i];detached+=material[i]!=(0xff000000u|unsigned(c.r)<<16|unsigned(c.g)<<8|c.b);}
        require(detached>100,"Material export retained every native actor and HUD pixel");
        compare();
        require(!player.approach().empty(),"ROM approach scenery/aircraft was not captured");
        const auto& last=player.approach().back();unsigned background_equal=0,actor_pixels=0;
        for(unsigned i=0;i<320*224;++i){const auto c=reference.pixels()[i];const uint32_t rgb=0xff000000u|uint32_t(c.r)<<16|uint32_t(c.g)<<8|c.b;
            background_equal+=last.scenery[i]==rgb;actor_pixels+=last.aircraft[i]!=0;}
        require(background_equal>320*224*.8 && actor_pixels>0,"Scenery detachment erased the scene or lost the native aircraft");
        std::map<uint64_t,std::vector<RGB888>> history;
        history.emplace(reference.board().frames,reference.pixels());
        auto presentation=[&]{
            const auto found=history.find(player.presentation_frame());require(found!=history.end(),"Presentation skipped an unrecorded native frame");
            auto pixels=player.pixels();const auto& expected=found->second;
            require(std::equal(expected.begin(),expected.end(),pixels.begin(),[](auto a,auto b){return a.r==b.r&&a.g==b.g&&a.b==b.b;}),"Buffered presentation changed native RGB");
            while(history.begin()->first<player.presentation_frame())history.erase(history.begin());
            if(player.phase()!=InteractiveSequencePlayer::Phase::takeoff && player.phase()!=InteractiveSequencePlayer::Phase::complete && player.first_burner_frame())
                require(player.presentation_frame()<player.first_burner_frame(),"Afterburner became visible before full throttle");
            ++presentations;
        };
        presentation();std::set<InteractiveSequencePlayer::Phase> phases;bool held=false;
        for(unsigned f=0;f<2400 && player.phase()!=InteractiveSequencePlayer::Phase::complete;++f){
            phases.insert(player.phase());GameControls input;
            if(player.phase()==InteractiveSequencePlayer::Phase::ready){
                require(player.missile_count()==player.missile_maximum(),"Ready before missiles finish reloading");
                if(surface!=InteractiveSequencePlayer::Surface::carrier)require(player.first_burner_frame() && player.buffered_frames()>0,"No automatic burner frames were held for full throttle");
                const auto before=player.native_frames();
                GameControls half;half.throttle=.5f;for(unsigned i=0;i<120;++i)player.advance(half);
                require(player.phase()==InteractiveSequencePlayer::Phase::ready && player.native_frames()==before,"Native departure did not wait for full throttle");compare();presentation();held=true;input.throttle=1;
            }
            const auto before=player.native_frames();player.advance(input);
            if(player.native_frames()!=before){GameControls physical;if(player.phase()==InteractiveSequencePlayer::Phase::takeoff || player.phase()==InteractiveSequencePlayer::Phase::complete)physical.throttle=1;tick(physical);compare();history.emplace(reference.board().frames,reference.pixels());}
            presentation();
        }
        require(held && player.phase()==InteractiveSequencePlayer::Phase::complete,"Native landing/takeoff lifecycle incomplete");
        require(phases.count(InteractiveSequencePlayer::Phase::rollout)&&phases.count(InteractiveSequencePlayer::Phase::rearming)&&phases.count(InteractiveSequencePlayer::Phase::ready)&&phases.count(InteractiveSequencePlayer::Phase::takeoff),"Native lifecycle phase missing");
        std::cout<<"Native landing surface "<<int(surface)<<": rollout, reload, held full-throttle prompt and completed departure passed\n";
    }
    std::cout<<"Native continuation RGB: "<<comparisons<<" of "<<comparisons<<" identical, 0 skipped (direct-board comparison; not independent MAME)\n";
    std::cout<<"Native buffered presentation: "<<presentations<<" of "<<presentations<<" identical, 0 skipped; no burner before full throttle\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

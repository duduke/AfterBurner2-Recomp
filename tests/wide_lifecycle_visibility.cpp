#include "wide_ground_reference.hpp"
// Real physical-input lifecycle replays; no stage skips or supplied execution.
#include "engine/xboard_game.hpp"
#include "hwvideo/wide_scene.hpp"
#include "hwvideo/wide_visibility.hpp"
#include "wide_native_reference.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
static void require(bool v,const std::string& message){if(!v)throw std::runtime_error(message);}
using Host=XBoardGame::Host;
static uint16_t peek(const Host& h,uint32_t a){auto i=MainBus::memory_offset(a);require(i!=MainBus::UNMAPPED,"Unmapped observation");return uint16_t(h.memory.bytes.at(i)<<8|h.memory.bytes.at(i+1));}
int main(int argc,char** argv)try{
    require(argc==2||argc==3,"Usage: wide_lifecycle_visibility ending|neutral|continue|weapons [--bad-reference]");
    const std::string name=argv[1];require(name=="ending"||name=="neutral"||name=="continue"||name=="weapons","Unknown lifecycle recording");
    const bool bad=argc==3;require(!bad||std::string_view(argv[2])=="--bad-reference","Unknown negative control");
    const std::string base="build/oracle/natural_"+name;
    // Use MAME's observed pins, not the earlier requested controls: its input
    // update latency is retained in these independent capture companions.
    const auto pins_path=name=="ending"?base+"_sequences.inputs":base+"_sequences.mame.csv.inputs";
    std::ifstream input(pins_path),reference(base+"_sequences.mame.csv");
    std::string magic,line;unsigned frames,a,b,c;require(bool(input>>magic>>frames>>a>>b>>c)&&magic=="AB2PHYS1"&&a<256&&b<256&&c<256,"Invalid recording");
    std::getline(reference,line);if(!line.empty()&&line.back()=='\r')line.pop_back();require(line=="frame,time_ns,mode,stage_index,lives,death_state,score,credits_bcd,carrier_state,refuel_state","Invalid MAME reference columns");
    require(roms.load_aburner2(),"ROMs required");XBoardGame game(roms);auto& h=game.board();h.switches={uint8_t(a),uint8_t(b),uint8_t(c)};
    Presentation::WideVisibility visibility(roms);
    auto plain_owner=std::make_unique<Presentation::WideScene>(roms),corrected_owner=std::make_unique<Presentation::WideScene>(roms);
    auto& plain=*plain_owner;auto& corrected=*corrected_owner;
    WideNativeReference legacy(roms);
    std::vector<Presentation::SceneClip> clips;std::optional<Presentation::SceneClip> pending;
    h.phase=[&](const auto& p){visibility.observe(p,h);};
    h.rendering_started=[&]{pending.emplace();auto& q=*pending;const auto& m=h.memory;auto bytes=std::span(m.bytes);
        Presentation::SceneClip::words(q.tiles,bytes.subspan(MainBus::TILE,0x10000));Presentation::SceneClip::words(q.text,bytes.subspan(MainBus::TEXT,0x1000));Presentation::SceneClip::words(q.palette,bytes.subspan(MainBus::PALETTE,0x4000));
        q.road=m.road_buffer;q.sprites=m.sprite_buffer;q.road_control=m.road_control;q.registers=game.video_registers();q.extended_sprites=visibility.descriptors();
        q.mode=peek(h,0x29c09c);q.throttle=peek(h,0x29c0d2);q.roll=peek(h,0x29c09e);};
    h.rendered=[&](uint64_t ns,unsigned top,unsigned bottom,bool display){require(bool(pending),"Missing lifecycle snapshot");pending->ns=ns;pending->top=top;pending->bottom=bottom;pending->display=display;clips.push_back(std::move(*pending));pending.reset();};
    std::set<unsigned> modes,stages,carrier,refuel,death,horizon_modes;uint64_t corrections=0,overflow=0,max_descriptors=0;
    for(unsigned f=0;f<frames;++f){unsigned index,value;Host::Input pins;
        require(bool(input>>index>>value)&&index==f&&value<256,"Missing physical frame");pins.buttons=value;
        for(auto& axis:pins.analog){require(bool(input>>value)&&value<256,"Invalid physical axis");axis=value;}
        clips.clear();game.tick(pins);game.take_audio();
        std::array<uint64_t,10> observed{h.frames,h.time(),peek(h,0x29c09c),peek(h,0x29c060),peek(h,0xffff88e0),peek(h,0xffff9124),
            (uint32_t(peek(h,0xffff8884))<<16)|peek(h,0xffff8886),h.memory.bytes.at(MainBus::memory_offset(0xffff8052)),peek(h,0xffff90b0),peek(h,0xffff90e0)};
        require(bool(std::getline(reference,line)),"Missing MAME observation");std::replace(line.begin(),line.end(),',',' ');std::istringstream fields(line);
        for(unsigned col=0;col<observed.size();++col){uint64_t expected;require(bool(fields>>expected),"Truncated MAME row");if(bad&&f==0&&col==2)++expected;
            require(observed[col]==expected,"Wide lifecycle observation differs from MAME at frame "+std::to_string(f)+" column "+std::to_string(col));}
        require(!(fields>>line),"Extra MAME column");
        legacy.check(clips,h.frames,game.pixels());
        modes.insert(observed[2]);death.insert(observed[5]);if(observed[2]==16){stages.insert(observed[3]);refuel.insert(observed[9]);}if(observed[2]>=15&&observed[2]<=18)carrier.insert(observed[8]);
        plain.render(clips,h.frames,false);corrected.render(clips,h.frames,true);auto p=plain.view(),q=corrected.view();
        require(p.ready&&q.ready,"Wide lifecycle missing rendered rows");
        if(name=="neutral"&&f==500){const auto i=180*398+10;
            require(q.ground_indices[i]>=0x1700&&q.ground_indices[i]<0x1708,"carrier sea side-band background still clipped");
            save_wide_ground_fixture(q,"build/oracle/widescreen/edge-carrier.bin");}
        if(name=="neutral"&&f==960){require(q.ground_indices[20*398+380]>=0x1720&&q.ground_indices[20*398+380]<0x1730,"departure blue side column remains");
            save_wide_ground_fixture(q,"build/oracle/widescreen/edge-departure.bin");}
        require(std::equal(p.sprites.begin(),p.sprites.end(),q.sprites.begin())&&std::equal(p.protection.begin(),p.protection.end(),q.protection.begin()),"Wide lifecycle ground correction changed protected layers");
        for(unsigned i=0;i<p.ground_indices.size();++i)if(p.ground_indices[i]!=q.ground_indices[i]){require(valid_wide_ground_change(p,q,i),"Wide lifecycle correction overwrote terrain");++corrections;}
        for(auto& clip:clips){max_descriptors=std::max<uint64_t>(max_descriptors,clip.extended_sprites.size());for(auto& d:clip.extended_sprites){overflow+=d.kind==4||d.kind==5;if(d.kind==3)horizon_modes.insert(clip.mode);}}
        if(f%3000==0)std::cout<<"Wide lifecycle "<<name<<": "<<f+1<<'/'<<frames<<" mode="<<observed[2]<<" stage="<<observed[3]<<std::endl;
    }
    input>>std::ws;require(input.peek()==EOF,"Trailing physical input frames");require(!std::getline(reference,line),"Trailing MAME observation frames");
    require(horizon_modes.contains(1)&&horizon_modes.contains(15),"Attract/takeoff published horizon missing from wider edges");
    if(name=="ending"){for(unsigned s=0;s<23;++s)require(stages.contains(s),"Missing natural stage");for(unsigned s=0;s<13;++s)require(carrier.contains(s),"Missing natural carrier state");for(unsigned s=0;s<8;++s)require(refuel.contains(s),"Missing natural refuel state");for(unsigned m:{0u,1u,16u,17u,18u,19u,20u,21u,22u,25u,26u})require(modes.contains(m),"Missing ending/ranking/attract mode");}
    std::cout<<"Wide lifecycle "<<name<<" PASS: "<<frames<<'/'<<frames<<" MAME ten-column observations, zero skips; full-width rows and terrain/protection preserved; modes=";
    for(auto m:modes)std::cout<<m<<',';std::cout<<" stages="<<stages.size()<<" carrier="<<carrier.size()<<" refuel="<<refuel.size()<<" death_states="<<death.size()<<" corrections="<<corrections<<" overflow observations="<<overflow<<" max descriptors="<<max_descriptors<<" horizon_modes=";for(auto m:horizon_modes)std::cout<<m<<',';std::cout<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

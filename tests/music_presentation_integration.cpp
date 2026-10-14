#include "frontend/native_music.hpp"
#include "engine/game_session.hpp"
#include <iostream>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
GameControls controls(unsigned f) {
    GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;
    if(f>=480){c.stick_x=f%120<60?.5f:-.5f;c.cannon=true;c.missile=f%90<12;c.throttle=f%180<90?1:-1;}
    return c;
}
int main()try {
    require(roms.load_aburner2(),"ROMs required");
    GameSession reference(roms),session(roms);session.capture_music(true);session.capture_cockpit(true);
    Frontend::NativeMusic music("", "build/oracle/music-fixtures");
    std::array<uint64_t,3> spatial_energy{};std::array<uint64_t,2> spatial_frames{};
    uint64_t music_energy=0,effects_energy=0,samples=0;std::array<unsigned,6> starts{};unsigned stops=0,fades=0;
    for(unsigned f=0;f<1800;++f) {
        auto c=controls(f);require(reference.advance(c)&&session.advance(c),"native advance failed");
        require(reference.info().time_ns==session.info().time_ns,"audio tap changed board time");
        const auto a=reference.frame().pixels,b=session.frame().pixels;
        for(size_t i=0;i<a.size();++i)require(a[i].r==b[i].r && a[i].g==b[i].g && a[i].b==b[i].b,"audio tap changed gameplay RGB");
        auto raw=session.take_audio();require(raw==reference.take_audio(),"audio tap changed native PCM");auto chunks=session.take_music();
        for(const auto& chunk:chunks) {
            for(const auto& block:chunk.spatial){const auto chip=block.chip;require(block.music.size()==block.groups[0].size(),"raw music tap duration mismatch");require(block.first==spatial_frames[chip],"noncontiguous chip tap");for(unsigned g=0;g<3;++g){require(block.groups[g].size()==block.groups[0].size(),"spatial group lengths");for(auto v:block.groups[g])spatial_energy[g]+=std::abs(v.left)+std::abs(v.right);}spatial_frames[chip]+=block.groups[0].size();}
            require(chunk.music.size()==chunk.effects.size(),"stem lengths differ");
            for(auto v:chunk.music)for(auto ch:v)music_energy+=uint64_t(std::abs(ch)*32768);
            for(auto v:chunk.effects)for(auto ch:v)effects_energy+=uint64_t(std::abs(ch)*32768);
            for(auto e:chunk.events){if(e.kind==MusicRouting::Kind::start)++starts[e.track];else if(e.kind==MusicRouting::Kind::stop)++stops;else ++fades;}
            samples+=chunk.music.size();
        }
        require(music.mix(raw,std::move(chunks))==raw,"all-original unity frontend mix changed native PCM");
        if(f==530){auto before=session.info();session.set_paused(true);require(!session.advance(c)&&session.take_music().empty()&&session.take_audio().empty(),"paused music tap advanced");require(session.info().time_ns==before.time_ns,"paused audio clock moved");session.set_paused(false);}
    }
    require(music_energy>1000000 && effects_energy>1000000 && starts[0] && starts[1],"music/effects/catalog coverage missing");
    std::cout<<"Spatial energy "<<spatial_energy[0]<<','<<spatial_energy[1]<<','<<spatial_energy[2]<<" frames "<<spatial_frames[0]<<','<<spatial_frames[1]<<'\n';
    require(spatial_energy[1]>1000000 && spatial_energy[2]>1000000 && spatial_frames[0]>spatial_frames[1],"front/rear chip-rate coverage missing");
    session.capture_cockpit(false);
    for(unsigned f=0;f<10;++f){auto c=controls(1799);reference.advance(c);session.advance(c);auto raw=session.take_audio();require(raw==reference.take_audio(),"disabled cockpit changed native PCM");auto chunks=session.take_music();for(const auto& chunk:chunks)for(const auto& block:chunk.spatial)require(block.groups[0].empty(),"disabled cockpit retained raw buffers");require(music.mix(raw,std::move(chunks))==raw,"disabled cockpit changed frontend PCM");}
    session.capture_cockpit(true);reference.advance(controls(1799));session.advance(controls(1799));auto resumed=session.take_audio();require(resumed==reference.take_audio(),"live cockpit resume changed PCM");auto resumed_chunks=session.take_music();uint64_t rear=0;for(const auto& chunk:resumed_chunks)for(const auto& block:chunk.spatial)for(auto v:block.groups[2])rear+=std::abs(v.left)+std::abs(v.right);require(rear>0,"live cockpit lost current engine ownership");require(music.mix(resumed,std::move(resumed_chunks))==resumed,"live resume changed frontend PCM");
    reference.restart();session.restart();music.restarted();
    for(unsigned f=0;f<20;++f){reference.advance({});session.advance({});auto raw=session.take_audio();require(raw==reference.take_audio() && music.mix(raw,session.take_music())==raw,"restart tap/history differs from fresh board");}
    std::cout<<"Music presentation: 1800 native RGB/PCM frames identical; "<<samples<<" stem frames, music energy="<<music_energy<<" effects energy="<<effects_energy<<"; starts=";
    for(auto n:starts)std::cout<<n<<',';std::cout<<" spatial unknown/front/rear="<<spatial_energy[0]<<','<<spatial_energy[1]<<','<<spatial_energy[2]<<" stops="<<stops<<" fade updates="<<fades<<"; pause/restart/live capture toggle passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

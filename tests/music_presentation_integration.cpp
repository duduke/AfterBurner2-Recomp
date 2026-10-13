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
    GameSession reference(roms),session(roms);session.capture_music(true);
    Frontend::NativeMusic music("", "build/oracle/music-fixtures");
    uint64_t music_energy=0,effects_energy=0,samples=0;std::array<unsigned,6> starts{};unsigned stops=0,fades=0;
    for(unsigned f=0;f<781;++f) {
        auto c=controls(f);require(reference.advance(c)&&session.advance(c),"native advance failed");
        require(reference.info().time_ns==session.info().time_ns,"audio tap changed board time");
        const auto a=reference.frame().pixels,b=session.frame().pixels;
        for(size_t i=0;i<a.size();++i)require(a[i].r==b[i].r && a[i].g==b[i].g && a[i].b==b[i].b,"audio tap changed gameplay RGB");
        auto raw=session.take_audio();require(raw==reference.take_audio(),"audio tap changed native PCM");auto chunks=session.take_music();
        for(const auto& chunk:chunks) {
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
    reference.restart();session.restart();music.restarted();
    for(unsigned f=0;f<20;++f){reference.advance({});session.advance({});auto raw=session.take_audio();require(raw==reference.take_audio() && music.mix(raw,session.take_music())==raw,"restart tap/history differs from fresh board");}
    std::cout<<"Music presentation: 781 native RGB/PCM frames identical; "<<samples<<" stem frames, music energy="<<music_energy<<" effects energy="<<effects_energy<<"; starts=";
    for(auto n:starts)std::cout<<n<<',';std::cout<<" stops="<<stops<<" fade updates="<<fades<<"; pause/restart passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

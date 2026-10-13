#include "engine/game_session.hpp"
#include <iostream>
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main()try {
    require(roms.load_aburner2(),"ROMs required");GameSession original(roms),observed(roms);observed.capture_feedback(true);
    std::array<unsigned,3> count{};unsigned boost=0;uint64_t sequence=0;
    for(unsigned f=0;f<4200;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;
        if(f>=600){c.stick_x=0;c.throttle=1;c.cannon=true;c.missile=f%90<12;}
        observed.capture_feedback(true); // Idempotent frontend preference polling.
        original.advance(c);observed.advance(c);auto a=original.frame(),b=observed.frame();
        require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"feedback changed time");
        require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"feedback changed RGB");
        require(original.take_audio()==observed.take_audio(),"feedback changed PCM");
        auto cues=observed.take_feedback();require(cues.number==b.info.number&&cues.ns==b.info.time_ns,"cue frame timestamp mismatch");
        for(auto e:cues.events){require(e.sequence>sequence&&e.ns<=cues.ns,"cue ordering");sequence=e.sequence;++count[unsigned(e.cue)];}
        boost+=cues.boost;require(observed.take_feedback().events.empty(),"events consumed twice");
    }
    std::cout<<"counts="<<count[0]<<","<<count[1]<<","<<count[2]<<" boost="<<boost<<"\n";
    require(count[0]&&count[1]&&count[2]&&boost,"native cue coverage missing");
    observed.set_paused(true);require(!observed.advance({})&&observed.take_feedback().events.empty(),"paused board emitted cues");
    observed.restart();original.restart();for(unsigned f=0;f<8;++f){original.advance({});observed.advance({});require(original.take_audio()==observed.take_audio(),"restart PCM changed");require(observed.take_feedback().events.empty(),"restart replayed cues");}
    std::cout<<"Feedback: 4200 native RGB/PCM/time frames identical; cannon/missile/impact="<<count[0]<<'/'<<count[1]<<'/'<<count[2]<<", boost frames="<<boost<<"; pause, consumption and restart passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

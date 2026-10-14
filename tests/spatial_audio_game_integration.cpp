#include "frontend/native_music.hpp"
#include "engine/game_session.hpp"
#include <iostream>
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try{
#ifdef AB2_OPENAL
    require(roms.load_aburner2(),"ROMs required");
    const std::string config="build/oracle/spatial-audio/game.output";
    Frontend::OutputSettings settings;settings.profile=Frontend::AudioProfile::headphones;
    require(Frontend::save_output_settings(config,settings),"output settings");
    Frontend::AudioOutput output(config,true);
    for(unsigned i=0;i<400&&!output.status().ready;++i)SDL_Delay(5);
    require(output.status().openal&&output.status().hrtf,"HRTF loopback unavailable");
    GameSession game(roms);game.capture_music(true);game.capture_cockpit(true);
    Frontend::NativeMusic music("","build/oracle/music-fixtures");music.output=&output;
    uint64_t frames=0;double energy=0;unsigned clips=0;std::deque<unsigned> pending;
    auto render=[&](unsigned count){auto values=output.render_loopback(count);require(values.size()==count*2,"game loopback frontier");for(float v:values){require(std::isfinite(v),"nonfinite game output");energy+=v*v;if(std::abs(v)>1.001f)++clips;}frames+=count;};
    for(unsigned f=0;f<1800;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;
        if(f>=480){c.stick_x=f%120<60?.5f:-.5f;c.cannon=true;c.missile=f%90<12;c.throttle=f%180<90?1:-1;}
        require(game.advance(c),"game advance");auto pcm=game.take_audio();const auto count=pcm.size()/2;
        require(music.mix(pcm,game.take_music())==pcm,"OpenAL submission changed original unity PCM");
        // Keep two frame chunks ahead, like normal device playback. Rendering
        // exactly to each buffer's end otherwise creates artificial starvation.
        pending.push_back(unsigned(count));if(pending.size()>=3){render(pending.front());pending.pop_front();}
    }
    while(!pending.empty()){render(pending.front());pending.pop_front();}
    require(frames>1000000&&energy>1&&clips==0,"game audio silent/clipping");
    game.set_paused(true);output.pause(true);const auto time=game.info().time_ns;require(!game.advance({})&&time==game.info().time_ns,"paused audio advanced game");
    output.clear();SDL_Delay(10);output.render_loopback(4096);
    game.restart();music.restarted();output.pause(false);
    for(unsigned f=0;f<20;++f){game.advance({});auto pcm=game.take_audio();require(music.mix(pcm,game.take_music())==pcm,"restart changed native PCM");require(output.render_loopback(unsigned(pcm.size()/2)).size()==pcm.size(),"restart playback frontier");}
    music.output=nullptr;
    std::cout<<"Spatial game playback: 1800 frames, "<<frames<<" stereo output frames, unity native PCM preserved; energy="<<energy<<", clipped="<<clips<<"; pause/clear/restart passed\n";
#else
    std::cout<<"Spatial game integration requires optional OpenAL Soft\n";
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "frontend/native_music.hpp"
#include "engine/game_session.hpp"
#include <fstream>
#include <iostream>
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(int argc,char** argv)try{
#ifdef AB2_OPENAL
    std::string recording_path;bool bad_reference=false;
    for(int i=1;i<argc;++i){const std::string_view option=argv[i];
        if(option=="--recording"&&i+1<argc)recording_path=argv[++i];
        else if(option=="--bad-reference")bad_reference=true;
        else throw std::runtime_error("Usage: spatial_audio_game_integration [--recording physical-inputs] [--bad-reference]");}
    require(roms.load_aburner2(),"ROMs required");
    std::ifstream recording;unsigned frame_count=1800;std::array<uint8_t,3> switches{255,255,221};
    if(!recording_path.empty()){recording.open(recording_path);std::string magic;unsigned a,b,c;
        require(bool(recording>>magic>>frame_count>>a>>b>>c)&&magic=="AB2PHYS1"&&frame_count&&a<256&&b<256&&c<256,"Invalid spatial physical recording");
        switches={uint8_t(a),uint8_t(b),uint8_t(c)};}
    const std::string config="build/oracle/spatial-audio/game.output";
    Frontend::OutputSettings settings;settings.profile=Frontend::AudioProfile::headphones;
    require(Frontend::save_output_settings(config,settings),"output settings");
    Frontend::AudioOutput output(config,true);
    for(unsigned i=0;i<400&&!output.status().ready;++i)SDL_Delay(5);
    require(output.status().openal&&output.status().hrtf,"HRTF loopback unavailable");
    GameSession game(roms),reference(roms);game.set_dip_switches(switches);reference.set_dip_switches(switches);game.capture_music(true);game.capture_cockpit(true);
    Frontend::NativeMusic music("","build/oracle/music-fixtures");music.output=&output;
    uint64_t frames=0;double energy=0,native_energy=0;unsigned clips=0,stops=0,fades=0;std::array<unsigned,6> starts{};std::deque<unsigned> pending;
    auto render=[&](unsigned count){auto values=output.render_loopback(count);require(values.size()==count*2,"game loopback frontier");for(float v:values){require(std::isfinite(v),"nonfinite game output");energy+=v*v;if(std::abs(v)>1.001f)++clips;}frames+=count;};
    for(unsigned f=0;f<frame_count;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;
        if(f>=480){c.stick_x=f%120<60?.5f:-.5f;c.cannon=true;c.missile=f%90<12;c.throttle=f%180<90?1:-1;}
        if(recording.is_open()){unsigned index,buttons,value;ArcadeInputs pins;
            require(bool(recording>>index>>buttons)&&index==f&&buttons<256,"Invalid spatial recorded frame");pins.buttons=buttons;
            for(auto& axis:pins.analog){require(bool(recording>>value)&&value<256,"Invalid spatial recorded axis");axis=value;}
            auto axis=[](unsigned v,unsigned low,unsigned center,unsigned high){require(v>=low&&v<=high,"Recorded control outside frontend range");return (int(v)-int(center))/float(v<center?center-low:high-center);};
            c.stick_x=axis(pins.analog[0],0x20,0x80,0xe0);c.stick_y=axis(pins.analog[1],0x40,0x80,0xc0);c.throttle=axis(pins.analog[2],0,0x80,0xff);
            c.cannon=!(buttons&0x10);c.missile=!(buttons&0x20);c.coin=!(buttons&0x40);c.start=!(buttons&8);
            const auto roundtrip=arcade_inputs(c);require(roundtrip.buttons==pins.buttons&&roundtrip.analog==pins.analog,"Recording cannot preserve physical pins through frontend");}
        require(game.advance(c)&&reference.advance(c),"game advance");auto pcm=game.take_audio();const auto count=pcm.size()/2;
        auto expected=reference.take_audio();if(bad_reference&&!expected.empty())expected[0]^=1;
        require(pcm==expected&&game.info().time_ns==reference.info().time_ns,"Spatial observer changed native PCM/time");
        const auto a=game.frame().pixels,b=reference.frame().pixels;
        require(std::equal(a.begin(),a.end(),b.begin(),[](auto x,auto y){return x.r==y.r&&x.g==y.g&&x.b==y.b;}),"Spatial observer changed native RGB");
        auto chunks=game.take_music();for(const auto& chunk:chunks)for(auto event:chunk.events){if(event.kind==MusicRouting::Kind::start)++starts.at(event.track);else if(event.kind==MusicRouting::Kind::stop)++stops;else ++fades;}
        require(music.mix(pcm,std::move(chunks))==pcm,"OpenAL submission changed original unity PCM");
        for(auto v:pcm){const double sample=double(v)/32768;native_energy+=sample*sample;}
        // Keep two frame chunks ahead, like normal device playback. Rendering
        // exactly to each buffer's end otherwise creates artificial starvation.
        pending.push_back(unsigned(count));if(pending.size()>=3){render(pending.front());pending.pop_front();}
        if(recording.is_open()&&f%3000==0)std::cout<<"Spatial physical replay: "<<f+1<<'/'<<frame_count<<std::endl;
    }
    if(recording.is_open()){recording>>std::ws;require(recording.peek()==EOF,"Trailing spatial physical recording frames");}
    const auto steady=output.status();require(steady.underruns==0&&steady.recoveries==0,"Continuous spatial playback underrun/recovery");
    while(!pending.empty()){render(pending.front());pending.pop_front();}
    require(frames>1000000&&energy>1&&clips==0,"game audio silent/clipping");
    game.set_paused(true);output.pause(true);const auto time=game.info().time_ns;require(!game.advance({})&&time==game.info().time_ns,"paused audio advanced game");
    output.clear();SDL_Delay(10);output.render_loopback(4096);
    game.restart();music.restarted();output.pause(false);
    for(unsigned f=0;f<20;++f){game.advance({});auto pcm=game.take_audio();require(music.mix(pcm,game.take_music())==pcm,"restart changed native PCM");require(output.render_loopback(unsigned(pcm.size()/2)).size()==pcm.size(),"restart playback frontier");}
    music.output=nullptr;
    std::cout<<"Spatial game playback: "<<frame_count<<" native RGB/PCM/time frames identical, "<<frames<<" stereo output frames; energy="<<energy<<", native_energy="<<native_energy<<", clipped="<<clips<<"; starts=";
    for(auto n:starts)std::cout<<n<<',';std::cout<<" stops="<<stops<<" fades="<<fades<<" steady_underruns="<<steady.underruns<<" peak_buffer_ms="<<steady.peak_buffered*1000<<" peak_pending_ms="<<steady.peak_pending*1000<<"; pause/clear/restart passed\n";
#else
    std::cout<<"Spatial game integration requires optional OpenAL Soft\n";
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

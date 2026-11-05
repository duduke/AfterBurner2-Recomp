#include "frontend/audio_output.hpp"
#include <iostream>
#include <filesystem>
#include <future>
#include <cstdlib>
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void wait_ready(Frontend::AudioOutput& output){for(unsigned i=0;i<400&&!output.status().ready;++i)SDL_Delay(5);require(output.status().openal&&output.status().ready,"OpenAL loopback setup failed");}
template<class Predicate>void wait_for(Predicate predicate,const char* why){for(unsigned i=0;i<400;++i){if(predicate())return;SDL_Delay(5);}require(false,why);}
void configure(Frontend::AudioOutput& output,const Frontend::OutputSettings& settings){const auto previous=output.status().configuration;output.configure(settings);wait_for([&]{return output.status().configuration>previous;},"live reconfiguration timed out");wait_ready(output);}
Frontend::AudioPacket tone(unsigned speaker,int rate=48000){
    Frontend::AudioPacket p;auto& stream=p.streams[3+speaker];stream.rate=rate;stream.channels=1;stream.pcm.resize(rate/8);
    for(unsigned i=0;i<stream.pcm.size();++i)stream.pcm[i]=int16_t(std::sin(i*6.28318530718*800/rate)*6000);
    return p;
}
double energy(const std::vector<float>& values,unsigned channel,unsigned channels=2){double sum=0;for(size_t i=channel;i<values.size();i+=channels)sum+=values[i]*values[i];return sum;}
void object_directions(Frontend::AudioOutput& output){
    auto packet=[](float x,float y,float z){Frontend::AudioPacket p;const auto weights=ObjectAudio::weights(x,y,z);
        for(unsigned d=0;d<ObjectAudio::directions;++d){auto& s=p.streams[13+d];s.channels=1;s.pcm.resize(4800);
            for(unsigned n=0;n<s.pcm.size();++n)s.pcm[n]=int16_t(std::sin(n*.13)*3000*weights[d]);}return p;};
    output.clear();output.submit(packet(-1,0,0));output.submit(packet(1,0,0));
    auto first=output.render_loopback(4800),second=output.render_loopback(4800);
    require(energy(first,0)>energy(first,1)*2,"queued object left position changed before playback");
    require(energy(second,1)>energy(second,0)*2,"queued object right position was lost");
    output.clear();output.submit(packet(0,0,-1));auto front=output.render_loopback(6000);
    output.clear();output.submit(packet(0,0,1));auto rear=output.render_loopback(6000);
    require(front!=rear,"object front/rear HRTF indistinguishable");
    output.clear();output.submit(packet(0,1,-1));auto raised=output.render_loopback(6000);
    require(front!=raised,"object elevation HRTF indistinguishable");
    for(unsigned d=0;d<ObjectAudio::directions;++d){const auto p=ObjectAudio::direction(d);const auto w=ObjectAudio::weights(p[0],p[1],p[2]);float power=0;for(auto v:w)power+=v*v;require(std::abs(power-1)<1e-5,"object direction power discontinuity");}
    std::cout<<"Object HRTF: queued left/right movement, front/rear, elevation and 24-direction power passed\n";
}
Frontend::AudioPacket continuous_packet(unsigned blocks_per_second){
    Frontend::AudioPacket packet;packet.original.resize(96000/blocks_per_second);
    for(unsigned source=0;source<packet.streams.size();++source){
        auto& stream=packet.streams[source];
        const bool pcm=source==2||(source>=7&&source<=10)||source==12||source>=13+ObjectAudio::directions;
        stream.rate=source==0?48000:pcm?31250:62500;stream.channels=(source>=3&&source<=10)||source>=13?1:2;
        stream.pcm.resize(stream.rate/blocks_per_second*stream.channels);
        for(size_t i=0;i<stream.pcm.size();++i)stream.pcm[i]=int16_t(std::sin(double(i/stream.channels)*6.28318530718*(110+source*70)/stream.rate)*300);
    }
    return packet;
}
void synchronized_reprime(Frontend::AudioOutput& output,bool staggered=false){
    output.clear();const auto before=output.status();const auto packet=continuous_packet(50);
    auto initial=packet;
    if(staggered)initial.streams[0].pcm.resize(initial.streams[0].pcm.size()*2);
    output.submit(std::move(initial));output.render_loopback(staggered?1440:1800);
    if(staggered){
        // Twelve chip-rate streams stop first; the 48 kHz source survives one
        // more render quantum. Prove the first reap completed before draining
        // the survivor, so a same-poll counter cannot accidentally pass.
        wait_for([&]{return output.status().underruns==before.underruns+Frontend::audio_streams-1;},"staggered first source cohort did not drain");
        require(output.status().reprimes==before.reprimes,"partial source exhaustion stopped a live source");output.render_loopback(960);
    }
#ifdef _WIN32
    constexpr unsigned quiet_blocks=3; // 60 ms must remain silent; 80 ms resumes.
#else
    constexpr unsigned quiet_blocks=2; // Retained 50 ms policy on other hosts.
#endif
    for(unsigned block=0;block<quiet_blocks;++block){output.submit(packet);const auto quiet=output.render_loopback(960);require(energy(quiet,0)+energy(quiet,1)<1e-12,"exhausted streams restarted before synchronized re-prime");}
    output.submit(packet);const auto resumed=output.render_loopback(960);require(energy(resumed,0)+energy(resumed,1)>.1,"synchronized re-prime failed to resume");
    const auto recovered=output.status();require(recovered.underruns==before.underruns+Frontend::audio_streams&&recovered.reprimes==before.reprimes+1,"global starvation telemetry incorrect");
    for(unsigned block=0;block<50;++block){output.submit(packet);output.render_loopback(960);}
    require(output.status().underruns==recovered.underruns,"synchronized recovery repeated its underrun");output.clear();
}
void sustained(Frontend::AudioOutput& output){
    // Repeated exact 100 ms blocks exercise all 13 production sources at their
    // native rates. Two queued blocks avoid deliberately draining each block;
    // loopback advances sample time without claiming a real-time hardware run.
    const auto packet=continuous_packet(10);
    unsigned seconds=60;if(const char* requested=std::getenv("AB2_SPATIAL_SUSTAIN_SECONDS"))seconds=unsigned(std::clamp(std::atoi(requested),1,3600));
    output.clear();output.submit(packet);output.submit(packet);auto rendered=output.render_loopback(4800);
    const auto before=output.status();double minimum=1e100,maximum=0,peak_buffer=0;
    for(unsigned block=1;block<seconds*10;++block){
        output.submit(packet);rendered=output.render_loopback(4800);require(rendered.size()==9600,"sustained render extent");
        const double power=energy(rendered,0)+energy(rendered,1);minimum=std::min(minimum,power);maximum=std::max(maximum,power);
        for(float sample:rendered)require(std::isfinite(sample)&&std::abs(sample)<1,"sustained output clipped/invalid");
        peak_buffer=std::max(peak_buffer,output.status().buffered);require(peak_buffer<.3,"sustained queue latency grew");
    }
    const auto after=output.status();require(after.underruns==before.underruns,"sustained source underrun");require(minimum>1&&maximum/minimum<1.05,"sustained signal disappeared or drifted");
    std::cout<<"Spatial sustained loopback: "<<seconds<<" sample-seconds, "<<seconds*48000ull<<" frames, 61 sources, no new underruns or clipping, steady-block energy="<<minimum<<".."<<maximum<<", peak software queue="<<peak_buffer*1000<<" ms\n";
    output.clear();
}
int main()try{
#ifdef AB2_OPENAL
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");require(SDL_Init(SDL_INIT_AUDIO),SDL_GetError());
    const std::string folder="build/oracle/spatial-audio";std::filesystem::create_directories(folder);const std::string config=folder+"/loopback.output";
    Frontend::OutputSettings settings;settings.profile=Frontend::AudioProfile::headphones;require(Frontend::save_output_settings(config,settings),"save profile");
    require(Frontend::load_output_settings(config)==settings,"profile round trip");
    const auto effect=Frontend::load_openal_wav("build/oracle/music-fixtures/B tone.WAV",false);
    const auto music=Frontend::load_openal_wav("build/oracle/music-fixtures/B tone.WAV",true);
    require(effect.channels==1&&music.channels==2&&effect.rate==music.rate&&music.samples.size()==2*effect.samples.size(),"WAV mono/stereo conversion");
    bool rejected=false;try{Frontend::load_openal_wav(folder+"/missing.wav",false);}catch(...){rejected=true;}require(rejected,"missing WAV accepted");
    Frontend::AudioOutput output(config,true);wait_ready(output);require(output.render_loopback(0).empty(),"empty audio frontier");require(output.status().hrtf,"HRTF was not actually enabled");require(!output.status().resamplers.empty(),"resampler enumeration missing");
    std::array<std::vector<float>,4> renders;
    for(unsigned speaker=0;speaker<4;++speaker){output.clear();output.submit(tone(speaker));renders[speaker]=output.render_loopback(7000);require(renders[speaker].size()==14000,"loopback extent");for(float v:renders[speaker])require(std::isfinite(v)&&std::abs(v)<1,"invalid or clipping output");}
    require(energy(renders[0],0)>energy(renders[0],1)*1.1,"front left localization reversed");
    require(energy(renders[1],1)>energy(renders[1],0)*1.1,"front right localization reversed");
    require(energy(renders[2],0)>energy(renders[2],1)*1.1,"rear left localization reversed");
    require(energy(renders[3],1)>energy(renders[3],0)*1.1,"rear right localization reversed");
    require(renders[0]!=renders[2]&&renders[1]!=renders[3],"front/rear HRTF indistinguishable");
    output.clear();Frontend::AudioPacket bgm;bgm.streams[0].pcm.resize(12000);for(unsigned i=0;i<6000;++i)bgm.streams[0].pcm[i*2]=int16_t(std::sin(i*.1)*4000);output.submit(std::move(bgm));auto dry=output.render_loopback(7000);
    require(energy(dry,0)>1&&energy(dry,1)<1e-12,"stereo BGM was spatialized/remixed into opposite ear");
    output.clear();output.submit(tone(0,62500));auto raw=output.render_loopback(7000);require(energy(raw,0)>1,"chip-rate OpenAL resampling silent");
    // Exhaustion must not leave AL_STOPPED buffers silently discarded when the
    // same source receives a later packet. The counter sees the stopped state
    // before rewind, so profiling can distinguish a restart from clean flow.
    const auto stopped=output.status();output.submit(tone(0));auto restarted=output.render_loopback(7000);require(energy(restarted,0)>1,"drained source did not restart");require(output.status().underruns>stopped.underruns,"source exhaustion was not counted");require(output.status().reprimes==stopped.reprimes,"single-source exhaustion re-primed every stream");
    output.clear();output.pause(true);SDL_Delay(20);auto quiet=output.render_loopback(256);require(energy(quiet,0)<1e-12&&energy(quiet,1)<1e-12,"pause/clear left stale audio");output.pause(false);
    output.clear();output.pause(true);SDL_Delay(20);output.submit(tone(1));
    auto paused=std::async(std::launch::async,[&]{return output.render_loopback(256);});
    if(paused.wait_for(std::chrono::seconds(2))!=std::future_status::ready){std::cerr<<"paused render with pending PCM deadlocked\n";std::exit(1);}
    quiet=paused.get();require(energy(quiet,0)<1e-12&&energy(quiet,1)<1e-12,"pending PCM leaked while paused");require(output.status().queued_packets==1,"pause consumed pending PCM");output.pause(false);
    auto resumed=output.render_loopback(7000);require(energy(resumed,1)>1,"pause/resume lost pending PCM");
    // A full producer queue must remain bounded and release a blocked submitter
    // on clear, while all device/context work stays with the playback owner.
    output.clear();output.pause(true);SDL_Delay(20);for(unsigned i=0;i<6;++i)output.submit(tone(i%4));
    auto blocked=std::async(std::launch::async,[&]{output.submit(tone(0));});
    require(blocked.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout,"producer queue failed to apply backpressure");require(output.status().queued_packets==6&&output.status().pending==.75,"bounded pending queue accounting");
    output.clear();if(blocked.wait_for(std::chrono::seconds(2))!=std::future_status::ready){std::cerr<<"clear did not release blocked producer\n";std::exit(1);}blocked.get();output.clear();output.pause(false);
    // Reconfiguration is live; same object, no emulated board restart.
    settings.profile=Frontend::AudioProfile::speakers51;configure(output,settings);require(output.status().mode==ALC_SURROUND_5_1_SOFT&&!output.status().hrtf,"5.1 mode/HRTF policy incorrect");
    output.submit(tone(2));auto surround=output.render_loopback(7000);require(surround.size()==42000,"5.1 channel count");require(energy(surround,4,6)+energy(surround,5,6)>energy(surround,0,6)+energy(surround,1,6),"rear speaker did not reach surround channels");
    settings.profile=Frontend::AudioProfile::speakers21;configure(output,settings);require(output.status().mode==ALC_STEREO_BASIC_SOFT&&!output.status().hrtf,"2.1 stereo/bass management policy");
    settings.profile=Frontend::AudioProfile::headphones;settings.resampler=output.status().resamplers.back();configure(output,settings);require(output.status().hrtf,"HRTF/resampler retry failed");
    object_directions(output);synchronized_reprime(output);
    synchronized_reprime(output,true);
    sustained(output);
    // Malformed backend PCM is a failure injection, not a game producer. It
    // must recover to usable dry SDL with truthful flags, and permit retry.
    const auto previous=output.status();auto invalid=tone(0);invalid.streams[3].rate=0;output.submit(std::move(invalid));wait_for([&]{return output.status().recoveries>previous.recoveries;},"OpenAL playback failure did not recover");
    const auto fallback=output.status();require(fallback.ready&&!fallback.openal&&!fallback.hrtf&&fallback.mode==0&&fallback.text.starts_with("SDL FALLBACK:"),"playback failure reported stale OpenAL/HRTF state");require(output.settings==settings,"recovery overwrote requested profile");
    configure(output,settings);output.submit(tone(1));require(energy(output.render_loopback(7000),1)>1,"OpenAL retry after SDL recovery silent");
    require(output.status().peak_queued_packets==6&&output.status().peak_pending>=.75,"lifetime queue telemetry lost across reconfiguration");
    std::cout<<"Spatial audio: WAV conversion, HRTF/directions, dry BGM, chip-rate interpolation, underrun restart, queued pause/resume, six-packet backpressure/clear, live profiles/resampler and SDL failure recovery/retry passed\n";
#else
    require(!Frontend::sdl_recovery_ready(1000,true,true),"SDL starved tail bypassed recovery reserve");
    require(Frontend::sdl_recovery_ready(1000,true,false),"SDL finite short tail cannot drain");
    require(!Frontend::sdl_recovery_ready(3840,false,false),"SDL playback started before recovery buffer filled");
    require(Frontend::sdl_recovery_ready(15360,false,false),"SDL primed output did not start");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    // Keep the physical dummy device at a different rate, so empty-input
    // telemetry cannot accidentally pass by avoiding the SDL resampler.
    require(SDL_InitSubSystem(SDL_INIT_AUDIO),"SDL dummy audio initialization failed");
    SDL_AudioSpec device_spec{SDL_AUDIO_S16,2,44100};
    struct DeviceOwner{SDL_AudioDeviceID id;~DeviceOwner(){SDL_CloseAudioDevice(id);SDL_QuitSubSystem(SDL_INIT_AUDIO);}} physical{SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&device_spec)};
    require(physical.id&&SDL_GetAudioDeviceFormat(physical.id,&device_spec,nullptr)&&device_spec.freq==44100,"SDL resampling fixture device rate differs");
    const std::string folder="build/oracle/spatial-audio";std::filesystem::create_directories(folder);
    const std::string config=folder+"/sdl-only.output";Frontend::OutputSettings settings;settings.profile=Frontend::AudioProfile::headphones;
    require(Frontend::save_output_settings(config,settings),"save SDL fallback profile");Frontend::AudioOutput output(config);
    wait_for([&]{return output.status().ready;},"SDL-only output unavailable");
    const auto fallback=output.status();require(!fallback.openal&&!fallback.hrtf&&fallback.text=="OPENAL NOT BUILT - SDL FALLBACK","SDL-only output claimed OpenAL/HRTF");
    output.pause(true);SDL_Delay(20);Frontend::AudioPacket packet;packet.original.resize(4800,500);output.submit(std::move(packet));
    wait_for([&]{return output.status().queued_packets==1;},"SDL-only pause lost pending PCM");
    output.pause(false);wait_for([&]{return output.status().packets==1;},"SDL-only playback did not accept native PCM");
    const auto before=output.status().configuration;settings.profile=Frontend::AudioProfile::original;output.configure(settings);
    wait_for([&]{return output.status().configuration>before;},"SDL-only profile switch timed out");
    require(output.status().ready&&output.status().text=="ORIGINAL SDL STEREO"&&output.status().packets==1,"SDL-only original profile/reconfiguration status");
    const auto initial=output.status();
    auto sdl_packet=[&]{Frontend::AudioPacket p;p.original.resize(1920,700);output.submit(std::move(p));}; // 20 ms
    for(unsigned i=0;i<3;++i)sdl_packet();
    wait_for([&]{return output.status().packets==initial.packets+3;},"SDL priming packets not accepted");
    require(output.status().sdl_priming&&output.status().buffered>=.059,"SDL playback started before recovery buffer filled");
    sdl_packet();wait_for([&]{return !output.status().sdl_priming;},"SDL primed output did not start");
    require(output.status().underruns==initial.underruns,"SDL startup reported an underrun");
    wait_for([&]{return output.status().underruns==initial.underruns+1;},"SDL software starvation was not counted");
    // The playback owner publishes the counter before its end-of-iteration
    // buffer/status snapshot. Observe that snapshot instead of racing it.
    wait_for([&]{return output.status().sdl_priming;},"SDL starvation did not re-prime");
    auto starved=output.status();require(starved.sdl_priming&&starved.reprimes==initial.reprimes+1,"SDL starvation did not re-prime");
    SDL_Delay(35);require(output.status().underruns==starved.underruns,"SDL starvation counted repeatedly while empty");
    for(unsigned i=0;i<3;++i)sdl_packet();
    wait_for([&]{return output.status().packets==initial.packets+7;},"SDL recovery packets not accepted");
    require(output.status().sdl_priming&&output.status().buffered>=.059,"SDL recovery resumed on a short packet");
    sdl_packet();wait_for([&]{return !output.status().sdl_priming;},"SDL recovery did not restart");
    output.pause(true);SDL_Delay(25);const auto frozen=output.status();SDL_Delay(30);
    require(output.status().buffered==frozen.buffered&&output.status().underruns==starved.underruns,"SDL paused recovery consumed PCM or counted starvation");
    output.clear();wait_for([&]{return output.status().buffered==0&&output.status().sdl_priming;},"SDL clear retained recovery state");
    output.pause(false);SDL_Delay(20);require(output.status().underruns==starved.underruns,"SDL clear counted intentional silence as starvation");
    sdl_packet();wait_for([&]{return output.status().underruns==starved.underruns+1;},"SDL finite short tail did not play and drain");
    require(output.status().reprimes==starved.reprimes+1,"SDL finite tail recovery not reported");
    std::cout<<"SDL recovery: startup priming, counted starvation, buffered restart, empty stability, pause/clear and finite tail passed\n";
    output.clear();std::cout<<"SDL-only playback: truthful HRTF fallback, queued pause/resume, native PCM and original profile switch passed\n";
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "frontend/audio_output.hpp"
#include <iostream>
#include <filesystem>
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void wait_ready(Frontend::AudioOutput& output){for(unsigned i=0;i<400&&!output.status().ready;++i)SDL_Delay(5);require(output.status().openal&&output.status().ready,"OpenAL loopback setup failed");}
Frontend::AudioPacket tone(unsigned speaker,int rate=48000){
    Frontend::AudioPacket p;auto& stream=p.streams[3+speaker];stream.rate=rate;stream.channels=1;stream.pcm.resize(rate/8);
    for(unsigned i=0;i<stream.pcm.size();++i)stream.pcm[i]=int16_t(std::sin(i*6.28318530718*800/rate)*6000);
    return p;
}
double energy(const std::vector<float>& values,unsigned channel,unsigned channels=2){double sum=0;for(size_t i=channel;i<values.size();i+=channels)sum+=values[i]*values[i];return sum;}
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
    output.clear();output.pause(true);SDL_Delay(20);auto quiet=output.render_loopback(256);require(energy(quiet,0)<1e-12&&energy(quiet,1)<1e-12,"pause/clear left stale audio");output.pause(false);
    // Reconfiguration is live; same object, no emulated board restart.
    settings.profile=Frontend::AudioProfile::speakers51;output.configure(settings);SDL_Delay(80);wait_ready(output);require(output.status().mode==ALC_SURROUND_5_1_SOFT&&!output.status().hrtf,"5.1 mode/HRTF policy incorrect");
    output.submit(tone(2));auto surround=output.render_loopback(7000);require(surround.size()==42000,"5.1 channel count");require(energy(surround,4,6)+energy(surround,5,6)>energy(surround,0,6)+energy(surround,1,6),"rear speaker did not reach surround channels");
    settings.profile=Frontend::AudioProfile::speakers21;output.configure(settings);SDL_Delay(80);wait_ready(output);require(output.status().mode==ALC_STEREO_BASIC_SOFT&&!output.status().hrtf,"2.1 stereo/bass management policy");
    std::cout<<"Spatial audio: mono/stereo WAV, active HRTF, four speaker directions, dry stereo BGM, 62500 Hz interpolation, pause/clear, live 5.1/2.1 profiles passed\n";
#else
    std::cout<<"Spatial audio optional backend not built\n";
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

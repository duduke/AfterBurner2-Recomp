#pragma once
#include <SDL3/SDL.h>
#include "frontend/custom_stage.hpp"
#include "frontend/custom_stage_tire_sound.hpp"
#include "hwaudio/arcade_audio.hpp"
#include <fstream>
#include <memory>

namespace Frontend {
// Authored stage event timing; original music/reload/launch sounds execute the
// unmodified sound ROM. The quiet engine bed and tire friction are procedural.
class CustomStageAudio {
    Roms* rom{};std::unique_ptr<ArcadeAudio> arcade;
    SDL_AudioStream* stream{};std::ofstream wave;uint32_t samples{},noise=1;
    CustomStageTireSound tires;double oscillator{},engine_gain{};bool started{},was_burner{},muted{};
    CustomStage::Phase previous=CustomStage::Phase::briefing;unsigned missiles{},ticks{};
    void word(unsigned value,unsigned bytes){for(unsigned i=0;i<bytes;++i)wave.put(char(value>>(i*8)));}
    void header(){wave.seekp(0);wave.write("RIFF",4);word(36+samples*2,4);wave.write("WAVEfmt ",8);word(16,4);word(1,2);word(2,2);word(48000,4);word(192000,4);word(4,2);word(16,2);wave.write("data",4);word(samples*2,4);}
    void command(uint8_t c){if(arcade)arcade->command(c);}
public:
    uint64_t energy{},rom_energy{};unsigned reload_events{},launch_events{},touchdown_events{};
    CustomStageAudio(Roms* assets,bool playback,const std::string& output,bool mute):rom(assets),muted(mute){
        if(rom){arcade=std::make_unique<ArcadeAudio>(*rom);arcade->render(9600);}
        if(!output.empty()){wave.open(output,std::ios::binary);if(!wave)throw std::runtime_error("Cannot create audio capture");header();}
        if(playback){SDL_AudioSpec spec{SDL_AUDIO_S16,2,48000};stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            if(!stream || !SDL_ResumeAudioStreamDevice(stream))throw std::runtime_error(SDL_GetError());SDL_SetAudioStreamGain(stream,muted?0:1);}
    }
    ~CustomStageAudio(){if(stream)SDL_DestroyAudioStream(stream);if(wave.is_open())header();}
    void mute(){muted=!muted;if(stream)SDL_SetAudioStreamGain(stream,muted?0:1);}
    void clear(){if(stream)SDL_ClearAudioStream(stream);}
    void tick(const CustomStage& s){
        using P=CustomStage::Phase;
        if(s.ticks<ticks || (s.phase==P::briefing && previous!=P::briefing)){
            clear();if(rom){arcade=std::make_unique<ArcadeAudio>(*rom);arcade->render(9600);}started=was_burner=false;engine_gain=0;tires.reset();previous=P::briefing;}
        if(!started && s.phase!=P::briefing){command(0x91);started=true;} // ROM music command, MusicRouting::commands.
        if(s.phase==P::rollout && previous!=P::rollout){tires.start();++touchdown_events;}
        if(s.burner() && !was_burner){command(0xef);++launch_events;} // Main $104D2 launch.
        if((s.phase==P::service || s.phase==P::contact) && s.missiles>missiles){command(0x9b);++reload_events;} // Main $16CF4 reload beep.
        auto pcm=arcade?arcade->render(800):std::vector<int16_t>(1600);
        const double target=s.phase==P::briefing?0:s.servicing() || s.phase==P::ready?.05:std::clamp(s.speed/150.,.1,1.)*(s.burner()?1.:.45);
        for(size_t f=0;f<800;++f){noise=noise*1664525u+1013904223u;const double hiss=(double(noise>>8)/16777215.-.5);
            engine_gain+=(target-engine_gain)*.001;oscillator+=6.283185*(45+s.speed*.35)/48000;
            const double authored=engine_gain*(hiss*850+std::sin(oscillator)*220)+tires.sample(s.speed,s.phase==P::rollout);
            for(unsigned ch=0;ch<2;++ch){auto& v=pcm[f*2+ch];rom_energy+=uint64_t(int64_t(v)*v);v=int16_t(std::clamp(double(v)*.65+authored,-32768.,32767.));energy+=uint64_t(int64_t(v)*v);if(wave.is_open()){word(uint16_t(v),2);++samples;}}}
        if(stream){if(SDL_GetAudioStreamQueued(stream)>48000*4/4)SDL_ClearAudioStream(stream);
            if(!SDL_PutAudioStreamData(stream,pcm.data(),int(pcm.size()*sizeof(int16_t))))throw std::runtime_error(SDL_GetError());}
        previous=s.phase;missiles=s.missiles;ticks=s.ticks;was_burner=s.burner();
    }
};
}

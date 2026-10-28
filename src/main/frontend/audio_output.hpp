#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <span>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <climits>
#include <chrono>
#include <cstdio>
#ifdef AB2_OPENAL
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#endif

namespace Frontend {
enum class AudioProfile { original, headphones, speakers21, speakers51 };
inline constexpr const char* audio_profiles[]{"ORIGINAL SDL","HEADPHONES HRTF","2.1 SPEAKERS","5.1 SPEAKERS"};
inline constexpr std::array<std::array<float,3>,4> cockpit_positions{{{-.5f,0,-.866f},{.5f,0,-.866f},{-.5f,0,.866f},{.5f,0,.866f}}};
// The Deluxe manual labels a rear speaker and lists a roof speaker. Headphones
// present a raised rear image; conventional 5.1 channels stay at ear height.
inline constexpr float headphone_rear_elevation=.5f;
struct OutputSettings {
    AudioProfile profile{};std::string device,resampler;
    bool operator==(const OutputSettings&)const=default;
};
inline OutputSettings load_output_settings(const std::string& path){
    OutputSettings s;std::ifstream in(path);std::string line;
    if(!std::getline(in,line)||line!="AB2OUTPUT1")return s;
    while(std::getline(in,line)){
        for(int i=0;i<4;++i)if(line=="profile="+std::to_string(i))s.profile=AudioProfile(i);
        if(line.starts_with("device="))s.device=line.substr(7);
        if(line.starts_with("resampler="))s.resampler=line.substr(10);
    }return s;
}
inline bool save_output_settings(const std::string& path,const OutputSettings& s){
    if(path.empty()||unsigned(s.profile)>3||s.device.find_first_of("\r\n")!=std::string::npos||s.resampler.find_first_of("\r\n")!=std::string::npos)return false;
    std::ofstream out(path+".tmp");out<<"AB2OUTPUT1\nprofile="<<int(s.profile)<<"\ndevice="<<s.device<<"\nresampler="<<s.resampler<<'\n';out.close();
    std::error_code ec;if(out)std::filesystem::rename(path+".tmp",path,ec);
    if(!out||ec){std::remove((path+".tmp").c_str());return false;}
    return true;
}
// PCM buffers are independent of any device/context and safe to pass to the
// playback owner. 0 = direct stereo music, 1/2 = unclassified stereo effects,
// 3..6 = FM cockpit mono, 7..10 = PCM cockpit mono, 11/12 = direct chip-rate
// stereo music. Chip streams retain rate.
struct AudioPacket {
    struct Stream {int rate=48000,channels=2;std::vector<int16_t> pcm;};
    std::array<Stream,13> streams;
    std::vector<int16_t> original;
};
struct WavPcm {int rate{},channels{};std::vector<int16_t> samples;};
inline WavPcm load_openal_wav(const std::string& path,bool music,int rate=0){
    SDL_AudioSpec from{};Uint8* bytes=nullptr;Uint32 length=0;
    if(!SDL_LoadWAV(path.c_str(),&from,&bytes,&length))throw std::runtime_error(SDL_GetError());
    struct Free{Uint8* p;~Free(){SDL_free(p);}} raw{bytes};
    const int size=SDL_AUDIO_BYTESIZE(from.format)*from.channels;
    if(from.freq<=0||from.channels<=0||size<=0||!length||length%size||length>256u*1024*1024)throw std::runtime_error("invalid WAV PCM size/format");
    SDL_AudioSpec to{SDL_AUDIO_S16,music?2:1,rate?rate:from.freq};
    if(to.freq<=0||double(length)/size/from.freq*to.freq*to.channels*2>256*1024*1024)throw std::runtime_error("converted WAV too large");
    Uint8* converted=nullptr;int n=0;
    if(!SDL_ConvertAudioSamples(&from,bytes,int(length),&to,&converted,&n))throw std::runtime_error(SDL_GetError());
    Free cooked{converted};
    if(n<=0||n%(to.channels*2))throw std::runtime_error("invalid converted WAV");
    WavPcm result{to.freq,to.channels,{}};result.samples.resize(n/2);std::memcpy(result.samples.data(),converted,n);return result;
}

// An idle resampler tail may finish a finite producer, but a tail observed
// before a starvation transition cannot bypass the new recovery reserve.
inline bool sdl_recovery_ready(int bytes,bool tail,bool starved){return bytes>=15360 || (tail&&!starved);}

class AudioOutput {
public:
    // Queue times cover software PCM only, excluding the output device's own
    // mix buffer and hardware latency. Counters survive live reconfiguration.
    struct Status {
        bool openal{},ready{},hrtf{},sdl_priming{};
        uint64_t packets{},underruns{},reprimes{},recoveries{},configuration{};
        double buffered{},peak_buffered{},pending{},peak_pending{};
        size_t queued_packets{},peak_queued_packets{};
        int hrtf_status{},mode{};std::string text;std::vector<std::string> devices,resamplers;
    };
    explicit AudioOutput(const std::string& path,bool loopback=false):settings(load_output_settings(path)),path(path),loopback(loopback){audio_initialized=SDL_InitSubSystem(SDL_INIT_AUDIO);worker=std::thread([this]{run();});configure(settings);}
    ~AudioOutput(){ {std::lock_guard lock(mutex);stopping=true;}wake.notify_all();worker.join();if(audio_initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);}
    AudioOutput(const AudioOutput&)=delete;
    OutputSettings settings;
    Status status()const{std::lock_guard lock(mutex);auto state=report;state.queued_packets=packets.size();for(const auto& packet:packets)state.pending+=packet_seconds(packet);return state;}
    bool drained()const{std::lock_guard lock(mutex);return packets.empty()&&report.buffered<.0001;}
    void configure(OutputSettings s){
        settings=std::move(s);save_output_settings(path,settings);
        {std::lock_guard lock(mutex);desired=settings;reconfigure=true;previewing=false;packets.clear();clear_requested=true;test_requested=false;}wake.notify_all();
    }
    void cycle_profile(int delta){auto s=settings;s.profile=AudioProfile((int(s.profile)+4+delta)%4);configure(s);}
    void cycle_device(int delta){auto list=status().devices;cycle(settings.device,list,delta);configure(settings);}
    void cycle_resampler(int delta){auto list=status().resamplers;cycle(settings.resampler,list,delta);configure(settings);}
    void pause(bool value){ {std::lock_guard lock(mutex);paused=value;}wake.notify_all();}
    void clear(){ {std::lock_guard lock(mutex);previewing=false;packets.clear();clear_requested=true;test_requested=false;}wake.notify_all();}
    void speaker_test(){ {std::lock_guard lock(mutex);previewing=false;packets.clear();clear_requested=true;test_requested=true;}wake.notify_all();}
    void submit(AudioPacket packet,bool preview=false){
        if(packet.original.empty()&&std::all_of(packet.streams.begin(),packet.streams.end(),[](const auto& s){return s.pcm.empty();}))return;
        std::unique_lock lock(mutex);
        wake.wait(lock,[&]{return stopping||!report.ready||packets.size()<6;});
        if(stopping||!report.ready)return;
        packets.emplace_back(std::move(packet));previewing=preview;
        double pending=0;for(const auto& queued:packets)pending+=packet_seconds(queued);
        report.peak_pending=std::max(report.peak_pending,pending);report.peak_queued_packets=std::max(report.peak_queued_packets,packets.size());wake.notify_all();
    }
public:
    // Standard Soft loopback extension: deterministic offline render diagnostic.
    std::vector<float> render_loopback(unsigned frames){
        if(!frames)return {};
        std::unique_lock lock(mutex);if(!loopback||!report.openal||!report.ready)throw std::runtime_error("loopback unavailable");
        render_frames=frames;render_result.clear();render_pending=true;wake.notify_all();wake.wait(lock,[&]{return !render_pending||!report.ready||stopping;});return std::move(render_result);
    }
private:
    std::string path;bool loopback{};unsigned render_frames{};bool render_pending{};std::vector<float> render_result;mutable std::mutex mutex;std::condition_variable wake;
    OutputSettings desired;Status report;std::deque<AudioPacket> packets;
    bool stopping{},reconfigure{},clear_requested{},paused{},previewing{},test_requested{};
    SDL_AudioStream* sdl{};
    bool sdl_priming=true,sdl_playing{},sdl_has_packet{};
    std::atomic<bool> sdl_unsupplied{false};
    static void SDLCALL sdl_demand(void* userdata,SDL_AudioStream*,int additional,int){
        if(additional>0)static_cast<AudioOutput*>(userdata)->sdl_unsupplied.store(true,std::memory_order_relaxed);
    }
    uint64_t sdl_last_packet{};
    void sdl_play(bool play){
        if(play==sdl_playing)return;
        if(!(play?SDL_ResumeAudioStreamDevice(sdl):SDL_PauseAudioStreamDevice(sdl)))throw std::runtime_error(SDL_GetError());
        sdl_playing=play;
    }
    int update_sdl(bool suspended,bool preview,bool testing){
        const int bytes=SDL_GetAudioStreamQueued(sdl);
        if(bytes<0)throw std::runtime_error(SDL_GetError());
        // Flush only a finite/idle producer's resampler tail. This does not
        // inject or discard native PCM, and is never needed during steady play.
        const bool tail=sdl_has_packet&&bytes>0&&SDL_GetTicks()-sdl_last_packet>=100;
        if(tail){if(!SDL_FlushAudioStream(sdl))throw std::runtime_error(SDL_GetError());sdl_last_packet=0;sdl_has_packet=false;}
        // Queue emptiness alone is not an underrun: the device may still
        // have prefetched audio. The callback records only unfulfilled reads.
        const bool unsupplied=sdl_unsupplied.exchange(false,std::memory_order_relaxed);
        const bool starved=!suspended&&!preview&&!testing&&!sdl_priming&&unsupplied;
        if(starved){
            sdl_play(false);sdl_priming=true;sdl_last_packet=0;sdl_has_packet=false;
            {std::lock_guard lock(mutex);++report.underruns;++report.reprimes;}
            SDL_Log("SDL playback request lacked source data; re-priming output");
        }
        // Build an 80 ms buffer for startup/recovery. Keep native samples in
        // order; neither the emulated clocks nor playback sample rate changes.
        // A finite short tail can still play after the producer becomes idle.
        const bool play=!suspended&&(!sdl_priming||preview||testing||sdl_recovery_ready(bytes,tail,starved));
        if(play)sdl_priming=false;
        sdl_play(play);
        return bytes;
    }
    static double packet_seconds(const AudioPacket& packet){
        double seconds=packet.original.size()/96000.0;
        for(const auto& stream:packet.streams)if(stream.rate>0&&stream.channels>0)seconds=std::max(seconds,double(stream.pcm.size())/stream.channels/stream.rate);
        return seconds;
    }
    void publish_open(Status info){
        std::lock_guard lock(mutex);
        info.packets=report.packets;info.underruns=report.underruns;info.reprimes=report.reprimes;info.recoveries=report.recoveries;info.configuration=report.configuration+1;
        info.peak_buffered=report.peak_buffered;info.peak_pending=report.peak_pending;info.peak_queued_packets=report.peak_queued_packets;
        report=std::move(info);
    }
    void record_buffered(double seconds){std::lock_guard lock(mutex);report.buffered=seconds;report.peak_buffered=std::max(report.peak_buffered,seconds);report.sdl_priming=sdl&&sdl_priming;}
#ifdef AB2_OPENAL
    ALCdevice* device{};ALCcontext* context{};std::array<ALuint,13> sources{};
    struct Queued {ALuint buffer;double seconds;};std::array<std::deque<Queued>,13> queued;
    bool started{},al_paused{},repriming{};unsigned starved_sources{};int direct{};bool connected_extension{};
    void check(){const auto err=alGetError();if(err!=AL_NO_ERROR)throw std::runtime_error("OpenAL error "+std::to_string(err));}
    void reap(){
        if(!started)return;
        unsigned underruns=0;
        for(unsigned i=0;i<sources.size();++i){
            ALint done=0;alGetSourcei(sources[i],AL_BUFFERS_PROCESSED,&done);
            while(done--){ALuint b=0;alSourceUnqueueBuffers(sources[i],1,&b);alDeleteBuffers(1,&b);queued[i].pop_front();}
            if(queued[i].empty()){
                ALint state=0;alGetSourcei(sources[i],AL_SOURCE_STATE,&state);
                if(state==AL_STOPPED){++underruns;alSourceRewind(sources[i]);}
            }
        }
        check();
        // A stalled producer can drain every synchronized stream. Restarting
        // each next 10/20 ms packet immediately repeats the underrun; collect
        // a synchronized reserve before resuming. A sparse source finishing while
        // other sources still run, or a single one-shot, restarts immediately.
        // Native chip and 48 kHz streams can stop in neighboring mixer quanta.
        // Retain the cohort across reap polls until new producer PCM arrives.
        starved_sources+=underruns;
        const bool exhausted=starved_sources>1&&std::all_of(queued.begin(),queued.end(),[](const auto& q){return q.empty();});
        if(exhausted){started=false;al_paused=false;repriming=true;}
        if(underruns){
            SDL_Log("OpenAL stream underrun: %u sources; %s",underruns,exhausted?"re-priming synchronized output":"restarting on next packet");
            std::lock_guard lock(mutex);report.underruns+=underruns;if(exhausted)++report.reprimes;
        }
    }
    double latency(){reap();double n=0;for(unsigned i=0;i<sources.size();++i){double sum=0;for(auto q:queued[i])sum+=q.seconds;ALfloat offset=0;alGetSourcef(sources[i],AL_SEC_OFFSET,&offset);n=std::max(n,sum-offset);}return n;}
    void clear_al(){if(!context)return;alSourceStopv(int(sources.size()),sources.data());for(unsigned i=0;i<sources.size();++i){while(!queued[i].empty()){ALuint b=0;alSourceUnqueueBuffers(sources[i],1,&b);alDeleteBuffers(1,&b);queued[i].pop_front();}}alSourceRewindv(int(sources.size()),sources.data());started=al_paused=repriming=false;starved_sources=0;check();}
    void open_al(const OutputSettings& s,Status& info){
        const bool enumerate=alcIsExtensionPresent(nullptr,"ALC_ENUMERATE_ALL_EXT");
        if(const auto* p=alcGetString(nullptr,enumerate?ALC_ALL_DEVICES_SPECIFIER:ALC_DEVICE_SPECIFIER))while(*p){info.devices.emplace_back(p);p+=std::strlen(p)+1;}
        if(loopback){auto open=reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr,"alcLoopbackOpenDeviceSOFT"));if(!open)throw std::runtime_error("OpenAL loopback unavailable");device=open(nullptr);}
        else {
            device=alcOpenDevice(s.device.empty()?nullptr:s.device.c_str());
            if(!device && !s.device.empty()){SDL_Log("OpenAL device '%s' unavailable; trying default device",s.device.c_str());device=alcOpenDevice(nullptr);}
        }
        if(!device)throw std::runtime_error("OpenAL device unavailable");
        std::vector<ALCint> attrs{ALC_FREQUENCY,48000};
        if(loopback){attrs.insert(attrs.end(),{ALC_FORMAT_CHANNELS_SOFT,s.profile==AudioProfile::speakers51?ALC_5POINT1_SOFT:ALC_STEREO_SOFT,ALC_FORMAT_TYPE_SOFT,ALC_FLOAT_SOFT});}
        const bool hrtf_ext=alcIsExtensionPresent(device,"ALC_SOFT_HRTF"),mode_ext=alcIsExtensionPresent(device,"ALC_SOFT_output_mode");
        if(hrtf_ext){attrs.push_back(ALC_HRTF_SOFT);attrs.push_back(s.profile==AudioProfile::headphones?ALC_TRUE:ALC_FALSE);}
        if(mode_ext){attrs.push_back(ALC_OUTPUT_MODE_SOFT);attrs.push_back(s.profile==AudioProfile::speakers51?ALC_SURROUND_5_1_SOFT:s.profile==AudioProfile::headphones?ALC_STEREO_HRTF_SOFT:ALC_STEREO_BASIC_SOFT);}attrs.push_back(0);
        context=alcCreateContext(device,attrs.data());if(!context||!alcMakeContextCurrent(context))throw std::runtime_error("OpenAL context unavailable");
        const auto version=alGetString(AL_VERSION);if(!version||std::string(version).find("ALSOFT")==std::string::npos)throw std::runtime_error("OpenAL Soft required");
        if(hrtf_ext){ALCint active=0;alcGetIntegerv(device,ALC_HRTF_SOFT,1,&active);info.hrtf=active==ALC_TRUE;alcGetIntegerv(device,ALC_HRTF_STATUS_SOFT,1,&info.hrtf_status);}
        if(mode_ext)alcGetIntegerv(device,ALC_OUTPUT_MODE_SOFT,1,&info.mode);
        connected_extension=alcIsExtensionPresent(device,"ALC_EXT_disconnect");
        direct=alIsExtensionPresent("AL_SOFT_direct_channels_remix")?AL_REMIX_UNMATCHED_SOFT:alIsExtensionPresent("AL_SOFT_direct_channels")?AL_TRUE:0;
        if(!direct)throw std::runtime_error("OpenAL direct stereo unsupported");
        ALint selected=-1;
        if(alIsExtensionPresent("AL_SOFT_source_resampler")){
            auto get=reinterpret_cast<LPALGETSTRINGISOFT>(alGetProcAddress("alGetStringiSOFT"));ALint count=0;alGetIntegerv(AL_NUM_RESAMPLERS_SOFT,&count);
            if(get)for(int i=0;i<count;++i){const auto* name=get(AL_RESAMPLER_NAME_SOFT,i);if(name){info.resamplers.emplace_back(name);if(s.resampler==name)selected=i;}}
        }
        alListener3f(AL_POSITION,0,0,0);const ALfloat orientation[]{0,0,-1,0,1,0};alListenerfv(AL_ORIENTATION,orientation);alDistanceModel(AL_NONE);alDopplerFactor(0);
        alGenSources(int(sources.size()),sources.data());
        for(unsigned i=0;i<sources.size();++i){alSourcei(sources[i],AL_SOURCE_RELATIVE,AL_TRUE);alSourcef(sources[i],AL_ROLLOFF_FACTOR,0);
            if(i>=3 && i<11){const unsigned slot=(i-3)%4;const auto& p=cockpit_positions[slot];
                const float elevation=s.profile==AudioProfile::headphones&&slot>=2?headphone_rear_elevation:0.f;
                alSource3f(sources[i],AL_POSITION,p[0],p[1]+elevation,p[2]);}
            else {alSource3f(sources[i],AL_POSITION,0,0,0);if(direct)alSourcei(sources[i],AL_DIRECT_CHANNELS_SOFT,direct);}
            if(selected>=0)alSourcei(sources[i],AL_SOURCE_RESAMPLER_SOFT,selected);
        }check();info.openal=info.ready=true;
        const char* mode=info.mode==ALC_SURROUND_5_1_SOFT?"5.1":info.mode==ALC_STEREO_HRTF_SOFT?"STEREO HRTF":info.mode==ALC_STEREO_BASIC_SOFT?"STEREO":"DEVICE MODE UNKNOWN";
        info.text=std::string("OPENAL ")+mode;
        if(s.profile==AudioProfile::headphones&&!info.hrtf)info.text="HRTF UNAVAILABLE - STEREO";
        if(s.profile==AudioProfile::speakers51&&info.mode!=ALC_SURROUND_5_1_SOFT)info.text="5.1 UNAVAILABLE - "+std::string(mode);
        const auto* dataset=info.hrtf?alcGetString(device,ALC_HRTF_SPECIFIER_SOFT):nullptr;
        SDL_Log("Audio requested=%s actual=%s mode=%x HRTF=%d status=%d dataset=%s resampler=%s version=%s",audio_profiles[int(s.profile)],info.text.c_str(),info.mode,info.hrtf,info.hrtf_status,dataset?dataset:"none",s.resampler.empty()?"default":s.resampler.c_str(),version);
    }
    void queue_al(const AudioPacket& packet){
        reap();
        for(unsigned i=0;i<sources.size();++i){const auto& stream=packet.streams[i];if(stream.pcm.empty())continue;
            if(stream.rate<=0||(stream.channels!=1&&stream.channels!=2)||stream.pcm.size()%stream.channels||stream.pcm.size()>size_t(INT_MAX)/2)throw std::runtime_error("invalid OpenAL packet");
            ALuint b=0;alGenBuffers(1,&b);alBufferData(b,stream.channels==1?AL_FORMAT_MONO16:AL_FORMAT_STEREO16,stream.pcm.data(),int(stream.pcm.size()*2),stream.rate);
            alSourceQueueBuffers(sources[i],1,&b);queued[i].push_back({b,double(stream.pcm.size())/stream.channels/stream.rate});
        }check();
        if(!started){
#ifdef _WIN32
            // Match SDL's 80 ms reserve on this host. The measured Windows
            // frame stalls can exhaust the former 25/50 ms startup/recovery
            // queues even when subsequent frames repay the pacing debt.
            constexpr double windows_openal_reserve=0.080;
            constexpr double startup_reserve=windows_openal_reserve,recovery_reserve=windows_openal_reserve;
#else
            constexpr double startup_reserve=0.025,recovery_reserve=0.050;
#endif
            if((loopback&&!repriming) || latency()>=(repriming?recovery_reserve:startup_reserve)){
                std::array<ALuint,13> active{};unsigned n=0;
                for(unsigned i=0;i<sources.size();++i)if(!queued[i].empty())active[n++]=sources[i];
                if(n){alSourcePlayv(int(n),active.data());started=true;repriming=false;}
            }
        } else {
            std::array<ALuint,13> restart{};unsigned n=0,underruns=0;
            for(unsigned i=0;i<sources.size();++i)if(!queued[i].empty()){
                ALint state=0;alGetSourcei(sources[i],AL_SOURCE_STATE,&state);
                if(state!=AL_PLAYING && state!=AL_PAUSED){restart[n++]=sources[i];if(state==AL_STOPPED)++underruns;}
            }
            if(underruns){SDL_Log("OpenAL stream underrun: restarting %u sources",underruns);std::lock_guard lock(mutex);report.underruns+=underruns;}
            if(n)alSourcePlayv(int(n),restart.data());
        }check();starved_sources=0;
    }
#endif
    bool audio_initialized{};uint64_t test_until{};
    std::thread worker;
    static void cycle(std::string& value,const std::vector<std::string>& list,int delta){int i=0;for(size_t j=0;j<list.size();++j)if(value==list[j])i=int(j)+1;i=(i+int(list.size())+1+delta)%(int(list.size())+1);value=i?list[i-1]:"";}
    void close(){
#ifdef AB2_OPENAL
        if(context){try{clear_al();}catch(...){}alDeleteSources(int(sources.size()),sources.data());alcMakeContextCurrent(nullptr);alcDestroyContext(context);context=nullptr;sources.fill(0);for(auto& q:queued)q.clear();}if(device){alcCloseDevice(device);device=nullptr;}
#endif
        if(sdl){SDL_DestroyAudioStream(sdl);sdl=nullptr;}
        sdl_priming=true;sdl_playing=false;sdl_last_packet=0;sdl_has_packet=false;sdl_unsupplied.store(false,std::memory_order_relaxed);
    }
    void open(const OutputSettings& s){close();Status info;
#ifdef AB2_OPENAL
        if(s.profile!=AudioProfile::original){try{open_al(s,info);publish_open(std::move(info));return;}catch(const std::exception& e){SDL_Log("OpenAL unavailable: %s; using SDL",e.what());auto devs=std::move(info.devices);info=Status{};info.devices=std::move(devs);info.text=std::string("SDL FALLBACK: ")+e.what();close();}}
#endif
        if(!audio_initialized){info.text="AUDIO UNAVAILABLE";}else{
            SDL_AudioSpec spec{SDL_AUDIO_S16,2,48000};SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,"1024");sdl=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,sdl_demand,this);
            info.ready=sdl&&SDL_PauseAudioStreamDevice(sdl);info.sdl_priming=info.ready;if(!info.ready){if(sdl)SDL_DestroyAudioStream(sdl);sdl=nullptr;info.text="SDL AUDIO UNAVAILABLE";}else if(info.text.empty())info.text=s.profile==AudioProfile::original?"ORIGINAL SDL STEREO":"OPENAL NOT BUILT - SDL FALLBACK";
        }publish_open(std::move(info));
    }
    void recover_to_sdl(const std::string& reason){
        OutputSettings fallback;fallback.profile=AudioProfile::original;open(fallback);
        {std::lock_guard lock(mutex);++report.recoveries;report.text=(report.ready?"SDL FALLBACK: ":"AUDIO UNAVAILABLE: ")+reason;render_result.clear();render_pending=false;}wake.notify_all();
    }
    void test(){
        AudioPacket p;const size_t frames=48000*3;
        p.original.resize(frames*2);p.streams[0].pcm.resize(frames*2);for(unsigned i=3;i<11;++i){p.streams[i].channels=1;p.streams[i].pcm.resize(frames);}
        for(size_t n=0;n<frames;++n){const unsigned slot=n/36000;const size_t local=n%36000;if(slot>=4||local>=18000)continue;
            const float envelope=std::min({1.f,float(local)/480,float(18000-local)/480});const int16_t v=int16_t(std::sin(double(n)*6.28318530718*(slot<2?700:250)/48000)*2500*envelope);
            p.streams[3+slot].pcm[n]=v;p.original[2*n+(slot&1)]=v;
        }
#ifdef AB2_OPENAL
        if(context){clear_al();queue_al(p);return;}
#endif
        if(sdl){SDL_ClearAudioStream(sdl);SDL_PutAudioStreamData(sdl,p.original.data(),int(p.original.size()*2));sdl_priming=false;sdl_last_packet=SDL_GetTicks();sdl_has_packet=true;sdl_play(true);}
        SDL_Log("Speaker test: front left, front right, rear left, rear right");
    }
    void run(){
        bool backpressured=false;
        for(;;){try{OutputSettings s;AudioPacket packet;bool reset=false,clear_now=false,test_now=false,is_paused=false,is_preview=false,have=false;
            {std::unique_lock lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(5),[&]{
                    return stopping||reconfigure||clear_requested||test_requested||render_pending||(!backpressured&&!packets.empty()&&(!paused||previewing));
                });
                if(stopping)break;
                reset=std::exchange(reconfigure,false);if(reset)s=desired;clear_now=std::exchange(clear_requested,false);test_now=std::exchange(test_requested,false);is_paused=paused;is_preview=previewing;
            }
            if(reset)open(s);
            if(clear_now){test_until=0;if(sdl){sdl_play(false);SDL_ClearAudioStream(sdl);sdl_priming=true;sdl_last_packet=0;sdl_has_packet=false;sdl_unsupplied.store(false,std::memory_order_relaxed);}
#ifdef AB2_OPENAL
                clear_al();
#endif
            }
            if(test_now){test();test_until=SDL_GetTicks()+3000;}
            const bool testing=SDL_GetTicks()<test_until;
            bool full=false;
#ifdef AB2_OPENAL
            if(context){if(connected_extension){ALCint connected=1;alcGetIntegerv(device,ALC_CONNECTED,1,&connected);if(!connected){SDL_Log("OpenAL device disconnected; reverting to default SDL");recover_to_sdl("OpenAL device disconnected");}}
                if(context){full=!loopback&&latency()>.10;const bool stop=is_paused&&!is_preview&&!testing;
                    if(stop!=al_paused&&started){
                        if(stop)alSourcePausev(int(sources.size()),sources.data());
                        else {std::array<ALuint,13> active{};unsigned n=0;for(unsigned i=0;i<sources.size();++i)if(!queued[i].empty())active[n++]=sources[i];if(n)alSourcePlayv(int(n),active.data());}
                        al_paused=stop;
                    }
                }
            }
#endif
            if(sdl){const auto n=update_sdl(is_paused&&!is_preview&&!testing,is_preview,testing);full=n>48000*4/10;}
            double buffered=0;
#ifdef AB2_OPENAL
            if(context)buffered=latency();
#endif
            if(sdl)buffered=std::max(0,SDL_GetAudioStreamQueued(sdl))/192000.0;
            record_buffered(buffered);backpressured=full;
            if(!full&&(!is_paused||is_preview)){
                {std::lock_guard lock(mutex);if(!packets.empty()){packet=std::move(packets.front());packets.pop_front();report.buffered=std::max(report.buffered,.001);have=true;}}wake.notify_all();
                if(have){
#ifdef AB2_OPENAL
                    if(context)queue_al(packet);
#endif
                    if(sdl&&!packet.original.empty()){
                        if(!SDL_PutAudioStreamData(sdl,packet.original.data(),int(packet.original.size()*2)))throw std::runtime_error(SDL_GetError());
                        sdl_last_packet=SDL_GetTicks();sdl_has_packet=true;
                    }
                    double fresh=0;
#ifdef AB2_OPENAL
                    if(context)fresh=latency();
#endif
                    if(sdl)fresh=std::max(0,SDL_GetAudioStreamQueued(sdl))/192000.0;
                    record_buffered(fresh);{std::lock_guard lock(mutex);++report.packets;}
                }
            }
#ifdef AB2_OPENAL
            if(loopback&&context){unsigned frames=0,channels=2;{std::lock_guard lock(mutex);if(render_pending&&(packets.empty()||(is_paused&&!is_preview))){frames=render_frames;channels=report.mode==ALC_SURROUND_5_1_SOFT?6:2;}}
                if(frames){auto render=reinterpret_cast<LPALCRENDERSAMPLESSOFT>(alcGetProcAddress(device,"alcRenderSamplesSOFT"));if(!render)throw std::runtime_error("loopback render unavailable");std::vector<float> values(size_t(frames)*channels);render(device,values.data(),frames);{std::lock_guard lock(mutex);render_result=std::move(values);render_pending=false;}wake.notify_all();}
            }
#endif
        }catch(const std::exception& e){SDL_Log("Audio playback failed: %s",e.what());
#ifdef AB2_OPENAL
            const bool was_openal=context!=nullptr;
#else
            const bool was_openal=false;
#endif
            close();
            if(was_openal)recover_to_sdl(e.what());
            else {std::lock_guard lock(mutex);report.ready=report.openal=report.hrtf=false;report.mode=report.hrtf_status=0;report.buffered=0;report.text="AUDIO PLAYBACK FAILED";}
            {std::lock_guard lock(mutex);packets.clear();render_result.clear();render_pending=false;}wake.notify_all();}}close();
    }
};
}

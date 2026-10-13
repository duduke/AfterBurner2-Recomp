#pragma once
#include <SDL3/SDL.h>
#include "frontend/music_settings.hpp"
#include "frontend/music_decoder.hpp"
#include "engine/xboard_game.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <map>

namespace Frontend {
// File work is isolated from board execution and device playback. A decoded
// file is immutable, so menu preview and active music can safely share it.
class NativeMusic {
public:
    MusicSettings settings;std::vector<std::string> files;std::string notice;
    explicit NativeMusic(std::string config,std::filesystem::path folder="soundtrack")
        :settings(load_music_settings(config)),config(std::move(config)),folder(std::filesystem::absolute(folder)),worker([this]{decode_jobs();}) {
        refresh();for(const auto& file:settings.files)if(!file.empty())request(file);
    }
    ~NativeMusic(){stop_preview();{std::lock_guard lock(mutex);stopping=true;}wake.notify_one();worker.join();}
    NativeMusic(const NativeMusic&)=delete;
    void refresh() {
        files=soundtrack_files(folder);candidate=-1;
        {std::lock_guard lock(mutex);for(auto it=cache.begin();it!=cache.end();) {
            std::error_code ec;auto stamp=std::filesystem::last_write_time(folder/it->first,ec);
            if(it->second.done && (ec || !it->second.data || stamp!=it->second.stamp))it=cache.erase(it);else ++it;
        }}
        for(const auto& file:settings.files)if(!file.empty())request(file);
        if(files.empty())notice="ADD WAV/FLAC TO SOUNDTRACK";
    }
    void volume(bool effects,int delta){auto& v=effects?settings.effects:settings.music;v=std::clamp(v+delta*5,0,100);save();}
    void track_volume(unsigned track,int delta){settings.track_volume[track]=std::clamp(settings.track_volume[track]+delta*5,0,100);save();}
    void defaults(){settings.music=settings.effects=100;settings.track_volume.fill(100);save();}
    void originals(){settings.files.fill({});pending=candidate=-1;stop_preview();save();}
    std::string selection(unsigned track)const{return settings.files[track].empty()?"ORIGINAL":settings.files[track];}
    void cycle(unsigned track,int delta) {
        const auto& current=candidate==int(track)?pending_file:settings.files[track];
        int i=0;for(unsigned j=0;j<files.size();++j)if(files[j]==current)i=int(j)+1;
        i=(i+int(files.size())+1+delta)%(int(files.size())+1);
        stop_preview();
        if(!i){pending=candidate=-1;settings.files[track].clear();save();return;}
        pending=candidate=int(track);pending_file=files[i-1];request(pending_file);poll();
    }
    void poll() {
        if(pending<0) {
            std::lock_guard lock(mutex);
            if(notice=="LOADING MUSIC FILE" && std::all_of(cache.begin(),cache.end(),[](const auto& item){return item.second.done;}))notice="MUSIC FILES READY";
            return;
        }
        std::lock_guard lock(mutex);const auto it=cache.find(pending_file);if(it==cache.end() || !it->second.done)return;
        if(it->second.data){settings.files[pending]=pending_file;pending=-1;save();}
        else {notice=it->second.error;pending=-1;}
    }
    void preview(unsigned track) {
        stop_preview();const auto file=settings.files[track];if(file.empty()){notice="ORIGINAL PLAYS IN GAME";return;}
        preview_data=lookup(file);if(!preview_data){request(file);notice="FILE NOT READY FOR PREVIEW";return;}
        SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};preview_stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if(!preview_stream || !SDL_ResumeAudioStreamDevice(preview_stream)){stop_preview();notice="PREVIEW AUDIO UNAVAILABLE";return;}
        preview_track=int(track);preview_cursor=0;notice="PREVIEWING - SPACE TO STOP";
    }
    bool previewing()const{return preview_stream!=nullptr;}
    void stop_preview(){if(preview_stream){SDL_DestroyAudioStream(preview_stream);notice="PREVIEW STOPPED";}preview_stream=nullptr;preview_data.reset();}
    void update_preview() {
        if(!preview_stream)return;
        SDL_SetAudioStreamGain(preview_stream,settings.music*settings.track_volume[preview_track]/10000.f);
        const int queued=SDL_GetAudioStreamQueued(preview_stream);if(queued<0){stop_preview();return;}if(queued>48000/10*8)return;
        if(preview_cursor>=preview_data->stereo.size()){if(!queued)stop_preview();return;}
        const auto count=std::min<size_t>(1920,preview_data->stereo.size()-preview_cursor);
        if(!SDL_PutAudioStreamData(preview_stream,preview_data->stereo.data()+preview_cursor,int(count*sizeof(float)))){stop_preview();notice="PREVIEW PLAYBACK FAILED";return;}
        preview_cursor+=count;
    }
    void restarted(){active=-1;active_file.reset();cursor=0;events.clear();playing=replacement=false;fade=1;music_gain=effects_gain=1;stop_preview();}
    std::vector<int16_t> mix(std::vector<int16_t> original,std::vector<XBoardGame::AudioPresentation> chunks) {
        poll();size_t out=0;
        for(auto& chunk:chunks) {
            for(auto e:chunk.events)events.push_back(e);
            for(size_t i=0;i<chunk.music.size();++i) {
                const auto sample=chunk.first+i;
                while(!events.empty() && sample>=event_sample(events.front().ns)) {event(events.front());events.pop_front();}
                const auto track_gain=active>=0?settings.track_volume[active]/100.f:1.f;
                const float target_music=settings.music/100.f*track_gain,target_effects=settings.effects/100.f;
                // 10 ms frontend gain transition, independent of native clocks.
                music_gain+=std::clamp(target_music-music_gain,-1.f/480,1.f/480);
                effects_gain+=std::clamp(target_effects-effects_gain,-1.f/480,1.f/480);
                const bool identity=!replacement && music_gain==1 && effects_gain==1 && target_music==1 && target_effects==1;
                for(unsigned ch=0;ch<2;++ch) {
                    float music=chunk.music[i][ch];
                    if(replacement)music=playing&&active_file?active_file->stereo[cursor+ch]*fade:0;
                    if(out<original.size() && !identity)original[out]=XBoardSpeaker::pcm(std::clamp(music*music_gain+chunk.effects[i][ch]*effects_gain,-1.f,1.f));
                    ++out;
                }
                if(playing && active_file)cursor=(cursor+2)%active_file->stereo.size();
            }
        }
        if(!original.empty() && out!=original.size())throw std::runtime_error("presentation audio frontier mismatch");
        return original;
    }
private:
    struct Entry{std::shared_ptr<const DecodedMusic> data;std::string error;bool done{};uint64_t used{};std::filesystem::file_time_type stamp{};};
    std::string config;std::filesystem::path folder;
    std::mutex mutex;std::condition_variable wake;std::deque<std::string> jobs;std::map<std::string,Entry> cache;
    bool stopping{};uint64_t clock{};std::thread worker;
    int pending=-1,candidate=-1,active=-1;bool playing{},replacement{};std::string pending_file;
    std::shared_ptr<const DecodedMusic> active_file,preview_data;size_t cursor{},preview_cursor{};int preview_track{};
    SDL_AudioStream* preview_stream{};float fade=1,music_gain=1,effects_gain=1;std::deque<MusicRouting::Event> events;
    static uint64_t event_sample(uint64_t ns){return ns/1000000000*48000+ns%1000000000*48000/1000000000;}
    void save(){notice=save_music_settings(config,settings)?"AUDIO SETTINGS SAVED":"AUDIO SETTINGS SAVE FAILED";}
    std::shared_ptr<const DecodedMusic> lookup(const std::string& file) {
        std::lock_guard lock(mutex);auto it=cache.find(file);if(it==cache.end() || !it->second.data)return {};it->second.used=++clock;return it->second.data;
    }
    void request(const std::string& file) {
        if(!music_filename(file) || file.empty())return;
        std::lock_guard lock(mutex);auto [it,added]=cache.try_emplace(file);
        if(added){jobs.push_back(file);wake.notify_one();}
        if(!it->second.done)notice="LOADING MUSIC FILE";
    }
    void decode_jobs() {
        for(;;) {
            std::string file;{std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping || !jobs.empty();});if(stopping)return;file=jobs.front();jobs.pop_front();}
            std::string error;std::shared_ptr<const DecodedMusic> data;
            std::error_code ec;const auto real=std::filesystem::weakly_canonical(folder/file,ec);
            if(ec || real.parent_path()!=std::filesystem::weakly_canonical(folder,ec))error="MUSIC FILE OUTSIDE SOUNDTRACK";
            else data=decode_music(real.string(),error);
            std::lock_guard lock(mutex);constexpr size_t limit=512ull*1024*1024;
            size_t used=data?data->stereo.size()*sizeof(float):0;for(const auto& [name,item]:cache){(void)name;if(item.data)used+=item.data->stereo.size()*sizeof(float);}
            while(used>limit) {
                auto oldest=cache.end();for(auto it=cache.begin();it!=cache.end();++it)if(it->second.data && it->second.data.use_count()==1 && (oldest==cache.end() || it->second.used<oldest->second.used))oldest=it;
                if(oldest==cache.end()){data.reset();error="MUSIC CACHE FULL";break;}
                used-=oldest->second.data->stereo.size()*sizeof(float);cache.erase(oldest);
            }
            auto& item=cache[file];item.data=std::move(data);item.error=error;item.done=true;item.used=++clock;std::error_code stamp_error;item.stamp=std::filesystem::last_write_time(folder/file,stamp_error);
        }
    }
    void event(const MusicRouting::Event& e) {
        if(e.kind==MusicRouting::Kind::start) {
            active=e.track;cursor=0;fade=1;active_file.reset();playing=true;replacement=false;const auto& file=settings.files[active];
            if(!file.empty()){active_file=lookup(file);if(!active_file){request(file);notice="MUSIC NOT READY - USING ORIGINAL";}}replacement=bool(active_file);
        } else if(e.kind==MusicRouting::Kind::stop) {playing=false;}
        else fade=e.gain;
    }
};
}

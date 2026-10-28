#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Frontend {
// Optional bounded diagnostics. Keep all samples in memory during play so
// filesystem writes cannot manufacture the stalls being measured.
class FrameTimings {
public:
    using Phases=std::array<double,9>; // board, scene, filter, shader, present, scene subphases
    explicit FrameTimings(const char* path,unsigned frames):path(path?path:""),
        frequency(double(SDL_GetPerformanceFrequency())),capacity(std::min(frames?frames:36000u,36000u)) {
        if(!this->path.empty())rows.reserve(capacity);
    }
    ~FrameTimings() {
        if(path.empty())return;
        try {
            const std::filesystem::path destination(std::u8string(path.begin(),path.end()));
            std::ofstream out(destination);
            out<<"frame,paused,total_ms,work_ms,pacing_ms,gap_ms,board_ms,scene_ms,filter_ms,shader_ms,present_ms,scene_setup_ms,ground_ms,scene_sprite_ms,flatten_ms,thread_cycles,processor_begin,processor_end\n"<<std::fixed<<std::setprecision(6);
            for(const auto& row:rows) {
                out<<row.frame<<','<<row.paused<<','<<row.total<<','<<row.work<<','<<row.pacing<<','<<row.gap;
                for(auto value:row.phases)out<<','<<value;
                out<<','<<row.cycles<<','<<row.processor_begin<<','<<row.processor_end<<'\n';
            }
            if(!out)SDL_Log("Frame timing trace could not be saved: %s",path.c_str());
        }catch(const std::exception& error){SDL_Log("Frame timing trace: %s",error.what());}
    }
    bool enabled()const{return !path.empty()&&rows.size()<capacity;}
    void begin(Phases phases) {
        if(!enabled())return;
        before=phases;start=SDL_GetPerformanceCounter();cycles=thread_cycles();processor=processor_number();
    }
    void finish(uint64_t frame,bool paused,uint64_t work_end,uint64_t paced_end,Phases phases) {
        if(!enabled())return;
        const auto prior_end=last_end?last_end:start;last_end=paced_end;
        for(unsigned i=0;i<phases.size();++i){phases[i]-=before[i];if(phases[i]<0)return;}
        rows.push_back({frame,paused,(paced_end-prior_end)*1000.0/frequency,(work_end-start)*1000.0/frequency,
            (paced_end-work_end)*1000.0/frequency,(start-prior_end)*1000.0/frequency,phases,thread_cycles()-cycles,processor,processor_number()});
    }
private:
    struct Row {uint64_t frame;bool paused;double total,work,pacing,gap;Phases phases;uint64_t cycles;unsigned processor_begin,processor_end;};
    std::string path;double frequency;unsigned capacity;std::vector<Row> rows;
    Phases before{};uint64_t start{},cycles{},last_end{};unsigned processor{};
    static uint64_t thread_cycles(){
#ifdef _WIN32
        ULONG64 result{};QueryThreadCycleTime(GetCurrentThread(),&result);return result;
#else
        return 0;
#endif
    }
    static unsigned processor_number(){
#ifdef _WIN32
        return GetCurrentProcessorNumber();
#else
        return unsigned(-1);
#endif
    }
};
}

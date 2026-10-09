#pragma once
#include "hwaudio/xboard_audio_deadlines.hpp"
#include "hwaudio/xboard_sound_cpu.hpp"
#include "hwaudio/xboard_speaker.hpp"
#include "hwmath/compare_timer.hpp"
#include <deque>

// Sound owner for the joint board. Commands and RESET come only from the
// main bus, and execution is granted by the board's common arbiter.
class XBoardSoundBoard {
public:
    XBoardSoundCpu cpu;XBoardPcm pcm;XBoardAudioDeadlines audio;XBoardSpeaker speaker;
    std::function<void(const XBoardSoundCpu::Event&,uint64_t)> observer;
    std::function<void(uint64_t,const std::vector<std::array<float,2>>&)> output;
    XBoardSoundBoard(std::span<const uint8_t> program,std::span<const uint8_t> samples)
        :cpu(program.data(),program.size()),pcm(samples),audio(pcm){
        cpu.pcm_read=[&](uint8_t a){return audio.pcm_read(a,now());};
        cpu.pcm_write=[&](uint8_t a,uint8_t d){audio.pcm_write(a,d,now());};
        cpu.ym_read=[&](uint8_t a){return audio.ym_read(a,now());};
        cpu.ym_write=[&](uint8_t a,uint8_t d){audio.ym_write(a,d,now());};
        cpu.latch_read=[&]{const auto v=latch->sound_read();cpu.abort_grant();return v;};
        cpu.trace=[&](const auto& e){if(observer)observer(e,now());};
        audio.irq_line=[&](bool a){cpu.irq_line(a);};
        audio.synchronize=[&]{if(running)cpu.abort_grant();};
        audio.stream=[&](unsigned d,uint64_t,uint64_t first,const auto& out){speaker.append(d,first,out);};
    }
    void bind_latch(CompareTimer5250& value){
        latch=&value;latch->nmi_line=[&](bool a){nmi.emplace_back(running?now():provider_ns,a);if(running)cpu.abort_grant();};
    }
    uint64_t now()const{return grant_ns+(cpu.clocks()-grant_clock-1)*250;}
    void external_time(uint64_t ns){provider_ns=ns;}
    void queue_reset(uint64_t ns,bool a){reset.emplace_back(ns,a);}
    uint64_t deadline()const{
        auto ns=audio.deadline();if(!nmi.empty())ns=std::min(ns,nmi.front().first);if(!reset.empty())ns=std::min(ns,reset.front().first);return ns;
    }
    void advance(uint64_t ns){
        while(!reset.empty()&&reset.front().first<=ns){cpu.reset_line(reset.front().second);reset.pop_front();}
        while(!nmi.empty()&&nmi.front().first<=ns){cpu.nmi_line(nmi.front().second);nmi.pop_front();}
        audio.advance(ns);
    }
    unsigned run(uint64_t,uint64_t begin,unsigned budget){
        grant_ns=begin;grant_clock=cpu.clocks();running=true;const auto ran=cpu.run(budget);running=false;return ran;
    }
    void update_speaker(uint64_t ns){audio.update(ns);auto out=speaker.update(ns);if(output)output(ns,out);}
private:
    CompareTimer5250* latch{};bool running{};uint64_t grant_ns{},grant_clock{},provider_ns{};
    std::deque<std::pair<uint64_t,bool>> reset,nmi;
};

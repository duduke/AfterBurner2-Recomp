#pragma once
#include "hwaudio/xboard_audio_deadlines.hpp"
#include "hwaudio/xboard_sound_cpu.hpp"
#include "hwaudio/xboard_speaker.hpp"
#include "hwmath/compare_timer.hpp"
#include <deque>
#include <memory>
#include "hwaudio/music_routing.hpp"

// Sound owner for the joint board. Commands and RESET come only from the
// main bus, and execution is granted by the board's common arbiter.
class XBoardSoundBoard {
public:
    XBoardSoundCpu cpu;XBoardPcm pcm;XBoardAudioDeadlines audio;XBoardSpeaker speaker;
    std::function<void(const XBoardSoundCpu::Event&,uint64_t)> observer;
    std::function<void(uint64_t,const std::vector<std::array<float,2>>&)> output;
    std::function<void(uint64_t,const std::vector<XBoardSpeaker::Stereo>&,const std::vector<XBoardSpeaker::Stereo>&,const std::vector<MusicRouting::Event>&)> presentation_output;
    std::unique_ptr<CockpitRouting> cockpit;
    std::array<CockpitRouting::Block,2> spatial_blocks;
    void prepare_cockpit(std::span<const uint8_t> sound,std::span<const uint8_t> main){if(!cockpit)cockpit=std::make_unique<CockpitRouting>(sound,main);}
    void enable_cockpit(std::span<const uint8_t> sound,std::span<const uint8_t> main){
        prepare_cockpit(sound,main);audio.cockpit=cockpit.get();
        audio.spatial=[&](const auto& b){auto& out=spatial_blocks[b.chip];out.chip=b.chip;if(out.groups[0].empty())out.first=b.first;
            for(unsigned g=0;g<3;++g)out.groups[g].insert(out.groups[g].end(),b.groups[g].begin(),b.groups[g].end());out.music.insert(out.music.end(),b.music.begin(),b.music.end());};
    }
    void disable_cockpit(){audio.spatial={};audio.cockpit=nullptr;spatial_blocks={};}
    void disable_music_presentation(){disable_cockpit();audio.stems={};audio.music_mask={};presentation_output={};routing.reset();music_speaker={};effects_speaker={};}
    const MusicRouting* music_metadata()const{return routing.get();}
    void enable_music_presentation(std::span<const uint8_t> rom) {
        routing=std::make_unique<MusicRouting>(rom);
        audio.music_mask=[&]{return routing->fm_music;};
        audio.stems=[&](unsigned d,uint64_t,uint64_t first,const auto& m,const auto& f){music_speaker.append(d,first,m);effects_speaker.append(d,first,f);};
    }
    XBoardSoundBoard(std::span<const uint8_t> program,std::span<const uint8_t> samples)
        :cpu(program.data(),program.size()),pcm(samples),audio(pcm){
        cpu.pcm_read=[&](uint8_t a){return audio.pcm_read(a,now());};
        cpu.pcm_write=[&](uint8_t a,uint8_t d){audio.pcm_write(a,d,now());};
        cpu.ym_read=[&](uint8_t a){return audio.ym_read(a,now());};
        cpu.ym_write=[&](uint8_t a,uint8_t d){audio.ym_write(a,d,now());};
        cpu.latch_read=[&]{const auto v=latch->sound_read();cpu.abort_grant();return v;};
        cpu.trace=[&](const auto& e){if(cockpit && CockpitRouting::interesting(e))cockpit->observe(e,cpu.registers());if(routing && MusicRouting::interesting(e))routing->observe(e,now(),cpu.registers(),cpu.ram);if(observer)observer(e,now());};
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
    void update_speaker(uint64_t ns){audio.update(ns);auto out=speaker.update(ns);if(output)output(ns,out);
        if(routing){auto m=music_speaker.update(ns),f=effects_speaker.update(ns);if(presentation_output)presentation_output(ns,m,f,routing->events);routing->events.clear();}
    }
private:
    std::unique_ptr<MusicRouting> routing;XBoardSpeaker music_speaker,effects_speaker;
    CompareTimer5250* latch{};bool running{};uint64_t grant_ns{},grant_clock{},provider_ns{};
    std::deque<std::pair<uint64_t,bool>> reset,nmi;
};

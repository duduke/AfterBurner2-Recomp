#pragma once
#include "hwaudio/xboard_audio_deadlines.hpp"
#include "hwaudio/xboard_sound_cpu.hpp"
#include "hwaudio/xboard_speaker.hpp"
#include "hwmath/compare_timer.hpp"

// Standalone sound-board host. Game commands are an explicitly external
// transport FIFO, delivered through the real latch and acknowledgement.
// Chip timing, CPU machine cycles and speaker updates have independent clocks.
class XBoardAudioRuntime {
public:
    XBoardAudioRuntime(std::span<const uint8_t> program,std::span<const uint8_t> samples)
        :cpu(program.data(),program.size()),pcm(samples),audio(pcm){
        cpu.pcm_read=[&](uint8_t a){return audio.pcm_read(a,bus_time());};
        cpu.pcm_write=[&](uint8_t a,uint8_t d){audio.pcm_write(a,d,bus_time());};
        cpu.ym_read=[&](uint8_t a){return audio.ym_read(a,bus_time());};
        cpu.ym_write=[&](uint8_t a,uint8_t d){audio.ym_write(a,d,bus_time());};
        cpu.latch_read=[&](){auto value=latch.sound_read();cpu.abort_grant();return value;};
        latch.nmi_line=[&](bool asserted){nmi.emplace_back(running?bus_time():now,asserted);if(running)cpu.abort_grant();};
        audio.irq_line=[&](bool asserted){cpu.irq_line(asserted);};
        audio.synchronize=[&](){if(running)cpu.abort_grant();};
        audio.stream=[&](unsigned d,uint64_t,uint64_t first,const auto& out){speaker.append(d,first,out);};
        // Playback latency keeps fixed 20ms speaker batches independent of
        // SDL request sizes without executing future commands prematurely.
        output.resize(960*2,0);
    }
    void command(uint8_t value){commands.push_back(value);}
    std::vector<int16_t> render(unsigned frames){
        requested+=frames;
        const uint64_t target=(requested/48000)*1000000000+(requested%48000)*1000000000/48000;
        advance(target);
        if(output.size()<size_t(frames)*2)throw std::runtime_error("speaker playback underflow");
        std::vector<int16_t> out;out.reserve(size_t(frames)*2);
        for(size_t i=0;i<size_t(frames)*2;++i){out.push_back(output.front());output.pop_front();}return out;
    }
    uint64_t time()const{return now;}
    XBoardSoundCpu::Coverage coverage()const{return cpu.coverage();}
private:
    XBoardSoundCpu cpu;XBoardPcm pcm;XBoardAudioDeadlines audio;XBoardSpeaker speaker;CompareTimer5250 latch;
    std::deque<uint8_t> commands;std::deque<std::pair<uint64_t,bool>> nmi;std::deque<int16_t> output;
    uint64_t requested{},now{},local{},grant_clock{},grant_ns{},flush=20000000;bool running{};
    uint64_t bus_time()const{return grant_ns+(cpu.clocks()-grant_clock-1)*250;}
    void deliver(){
        while(!nmi.empty()&&nmi.front().first<=now){cpu.nmi_line(nmi.front().second);nmi.pop_front();}
        audio.advance(now);
        if(!latch.state().nmi&&nmi.empty()&&!commands.empty()){
            latch.deliver_sound(commands.front());commands.pop_front();
            while(!nmi.empty()&&nmi.front().first<=now){cpu.nmi_line(nmi.front().second);nmi.pop_front();}
        }
    }
    void advance(uint64_t target){
        while(now<target){
            deliver();
            auto head=std::min(target,std::min(flush,audio.deadline()));
            if(!nmi.empty())head=std::min(head,nmi.front().first);
            if(head<now)head=now;
            if(local<head){auto budget=unsigned(std::min<uint64_t>((head-local)/250,80000));
                if(budget){grant_ns=local;grant_clock=cpu.clocks();running=true;auto ran=cpu.run(budget);running=false;
                    local+=uint64_t(ran)*250;
                    // A synchronization abort may introduce an earlier head.
                    head=std::min(head,audio.deadline());if(!nmi.empty())head=std::min(head,nmi.front().first);
                    if(local<head&&ran<budget)head=local;
                }}
            now=head;deliver();
            if(now==flush){audio.update(now);auto mixed=speaker.update(now);
                for(auto v:mixed)for(auto ch:v)output.push_back(XBoardSpeaker::pcm(ch));flush+=20000000;}
        }
    }
};

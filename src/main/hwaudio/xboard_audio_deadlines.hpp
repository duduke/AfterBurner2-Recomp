#pragma once
#include "hwaudio/xboard_pcm.hpp"
#include "hwaudio/presentation_ym.hpp"
#include "hwaudio/cockpit_audio.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <limits>
#include <vector>

// MAME ymfm_mame.h + sound.cpp stream ownership. Stream updates include the
// current sample (floor(time * rate)+1), except at time zero. Timer callbacks
// use their posted expiry even when scheduler delivery is late.
class XBoardAudioDeadlines : private ymfm::ymfm_interface {
public:
    static constexpr uint64_t NEVER=std::numeric_limits<uint64_t>::max();
    using Samples=std::vector<XBoardPcm::Sample>;
    std::function<void(unsigned,uint64_t,uint64_t,const Samples&)> stream;
    std::function<void(unsigned,uint64_t,uint64_t,const Samples&,const Samples&)> stems;
    std::function<uint32_t()> music_mask;
    CockpitRouting* cockpit{};
    std::function<void(const CockpitRouting::Block&)> spatial;
    std::function<void(bool)> irq_line;
    std::function<void()> synchronize;
    std::function<void(bool,unsigned,uint64_t,uint64_t)> timer_callback;
    std::array<uint64_t,2> timer_callbacks{};
    uint64_t mode_callbacks{},late_modes{};
    explicit XBoardAudioDeadlines(XBoardPcm& pcm):pcm(pcm),chip(*this){chip.reset();}
    uint64_t deadline() const {
        auto next=std::min(timers[0].at,timers[1].at);
        if(!modes.empty())next=std::min(next,modes.front().at);
        return next;
    }
    void advance(uint64_t ns){
        while(deadline()<=ns){const auto at=deadline();
            unsigned which=0;
            if(timers[1].at<timers[0].at||(timers[1].at==timers[0].at&&timers[1].order<timers[0].order))which=1;
            current=at;
            if(!modes.empty()&&modes.front().at==at&&(timers[which].at!=at||modes.front().order<timers[which].order)){
                if(timer_callback)timer_callback(true,0,at,ns);
                auto m=modes.front();modes.pop_front();m_engine->engine_mode_write(m.data);
                ++mode_callbacks;if(ns>at)++late_modes;
            }else {if(timer_callback)timer_callback(false,which,at,ns);timers[which].at=NEVER;++timer_callbacks[which];m_engine->engine_timer_expired(which);}
        }
        current=ns;
    }
    uint8_t ym_read(unsigned a,uint64_t ns){current=ns;update_fm(ns);return chip.read(a);}
    void ym_write(unsigned a,uint8_t d,uint64_t ns){current=ns;update_fm(ns);chip.write(a,d);}
    uint8_t pcm_read(unsigned a,uint64_t ns){update_pcm(ns);return pcm.read(a);}
    void pcm_write(unsigned a,uint8_t d,uint64_t ns){update_pcm(ns);pcm.write(a,d);}
    void update(uint64_t ns){current=ns;update_fm(ns);update_pcm(ns);}
    uint64_t fm_samples() const{return fm_count;}
    uint64_t pcm_samples() const{return pcm_count;}
private:
    struct Timer {uint64_t at=NEVER,order=NEVER;};
    struct Mode {uint64_t at,order;uint8_t data;};
    XBoardPcm& pcm;PresentationYm2151 chip;
    std::array<Timer,2> timers;std::deque<Mode> modes;
    uint64_t current{},busy_end{},serial{},fm_count{},pcm_count{};
    void update_fm(uint64_t ns){
        if(!ns)return;
        const auto target=ns/16000+1;
        if(target<=fm_count)return;
        CockpitRouting::Block block{0,fm_count,{},{}};
        Samples out,music,effects;out.reserve(target-fm_count);
        if(stems){music.reserve(target-fm_count);effects.reserve(target-fm_count);}
        for(auto n=fm_count;n<target;++n){ymfm::ym2151::output_data sample;chip.generate(&sample);out.push_back({sample.data[0],sample.data[1]});
            if(spatial&&cockpit){const auto masks=cockpit->masks(music_mask?music_mask():0);for(unsigned g=0;g<3;++g){const auto v=chip.channels(masks[g]);block.groups[g].push_back({v.data[0],v.data[1]});}}
            if(stems){const auto mask=music_mask?music_mask():0;const auto m=mask==0xff?sample:chip.channels(mask),f=mask==0?sample:chip.channels(0xff&~mask);music.push_back({m.data[0],m.data[1]});effects.push_back({f.data[0],f.data[1]});}}
        if(spatial&&cockpit){block.music=music;spatial(block);}
        const auto first=fm_count;fm_count=target;if(stems)stems(0,ns,first,music,effects);if(stream)stream(0,ns,first,out);
    }
    void update_pcm(uint64_t ns){
        if(!ns)return;
        const auto target=ns/32000+1;
        if(target<=pcm_count)return;
        CockpitRouting::Block block{1,pcm_count,{},{}};
        Samples out,music,effects;out.reserve(target-pcm_count);
        if(stems){music.reserve(target-pcm_count);effects.reserve(target-pcm_count);}
        for(auto n=pcm_count;n<target;++n) {
            std::array<XBoardPcm::Sample,16> voices;
            out.push_back(pcm.tick((stems||spatial)?&voices:nullptr));
            if(spatial&&cockpit){std::array<XBoardPcm::Sample,3> groups{};for(unsigned c=8;c<16;++c){auto& v=groups[cockpit->pcm[c]];v.left+=voices[c].left;v.right+=voices[c].right;}for(unsigned g=0;g<3;++g)block.groups[g].push_back(groups[g]);}
            if(stems){XBoardPcm::Sample m{},f{};for(unsigned c=0;c<16;++c){auto& v=c<8?m:f;v.left+=voices[c].left;v.right+=voices[c].right;}music.push_back(m);effects.push_back(f);}
        }
        if(spatial&&cockpit){block.music=music;spatial(block);}
        const auto first=pcm_count;pcm_count=target;if(stems)stems(1,ns,first,music,effects);if(stream)stream(1,ns,first,out);
    }
    void ymfm_sync_mode_write(uint8_t data) override {modes.push_back({current,++serial,data});if(synchronize)synchronize();}
    void ymfm_set_timer(uint32_t n,int32_t clocks) override {timers[n]={clocks<0?NEVER:current+uint64_t(clocks)*250,++serial};}
    void ymfm_set_busy_end(uint32_t clocks) override {busy_end=current+uint64_t(clocks)*250;}
    bool ymfm_is_busy() override {return current<busy_end;}
    void ymfm_update_irq(bool asserted) override {if(irq_line)irq_line(asserted);}
};

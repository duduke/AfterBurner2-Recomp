#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "hwmath/xboard_timeline.hpp"
#include "engine/feedback_sequences.hpp"

namespace Frontend {
// Playback exposes software queue time; unavailable audio keeps tactile cues
// immediate. Hardware/transport latency is not inferred from these values.
inline unsigned refill_audio_delay_ms(double buffered,double pending,bool ready){
    const double seconds=buffered+pending;
    return ready&&std::isfinite(seconds)?unsigned(std::clamp(seconds,0.,1.)*1000.+.5):0;
}
// Layered enhancement tuning, not aircraft physics. ROM observations select the
// phases and impulses; these bounded envelopes shape the controller's feel.
class SequenceMixer {
public:
    void clear(){motion={};until.fill(0);refills.clear();phase_started=0;}
    void restart(){clear();last_sequence=0;}
    template<class Frame> void consume(const Frame& f,uint64_t now,unsigned refill_delay_ms=0){
        if(f.motion.motion!=motion.motion)phase_started=now;
        motion=f.motion;
        refills.erase(std::remove_if(refills.begin(),refills.end(),[&](auto start){return now>=start+duration[3];}),refills.end());
        for(const auto& e:f.sequence_events){
            if(e.sequence<=last_sequence)continue;
            last_sequence=e.sequence;
            if(e.ns>f.ns||f.ns-e.ns>100000000)continue;
            if(e.cue==Feedback::SequenceCue::refill){
                // The current board frame's audio follows the already queued
                // packets. Place its event within that frame, not at its end.
                const uint64_t age=f.ns-e.ns;
                const uint64_t offset=age<XBoardTimeline::FRAME_NS?(XBoardTimeline::FRAME_NS-age)/1000000:0;
                if(refills.size()<32)refills.push_back(now+refill_delay_ms+(refill_delay_ms?offset:0));
            }else until[unsigned(e.cue)]=now+duration[unsigned(e.cue)];
        }
    }
    template<class Settings> std::array<float,4> sample(uint64_t now,const Settings& s) const {
        using M=Feedback::Motion;
        std::array<float,4> v{};
        const float elapsed=float(now-phase_started),speed=std::clamp(motion.speed/768.f,0.f,1.f);
        // Unequal low/high pulses give deck texture instead of a uniform buzz.
        auto triangle=[&](unsigned period){const float p=float((now-phase_started)%period)/period;return p<.5f?2*p:2*(1-p);};
        auto body=[&](float low,float high,int level){v[0]=low*Settings::gain(level);v[1]=high*Settings::gain(level);};
        switch(motion.motion){
        case M::approach:body(.09f+.035f*triangle(230),.025f,s.landing);break;
        case M::rollout:body(.18f+.24f*speed+.12f*triangle(125),.10f+.22f*speed*triangle(73),s.landing);break;
        case M::parked:body(.10f,.035f+.025f*triangle(310),s.takeoff);break;
        case M::spool:{const float ramp=std::clamp(motion.timer/20.f,0.f,1.f);body(.18f+.38f*ramp,.07f+.15f*ramp*triangle(90),s.takeoff);break;}
        case M::roll:body(.38f+.42f*speed,.15f+.30f*speed*triangle(65),s.takeoff);break;
        case M::climb:{const float fade=std::max(0.f,1.f-elapsed/1800.f);body((.12f+.38f*speed)*fade,.12f*fade*triangle(180),s.takeoff);break;}
        default:break;
        }
        for(unsigned i=0;i<3;++i){
            if(until[i]<=now)continue;
            const float age=float(duration[i]-(until[i]-now));
            const int level=i==0?s.landing:i==1?s.takeoff:s.refueling;
            std::array<float,4> pulse{};
            if(i==0){ // Wheel impact, suspension rebound, then a short settling tail.
                const float kick=age<55?1.f-age/90.f:age<105?.18f:age<165?.48f*(165-age)/60.f:.08f*(280-age)/115.f;
                pulse={1.15f*kick,.85f*kick,.45f*kick,.45f*kick};
            }else if(i==1){const float kick=std::max(0.f,1.f-age/230.f);pulse={.90f*kick,.65f*kick,.65f*kick,.65f*kick};}
            else if(i==2){const float kick=age<55?1.f-age/80.f:age<85?.12f:.34f*std::max(0.f,(170-age)/85.f);pulse={.85f*kick,.60f*kick,.45f*kick,.45f*kick};}
            for(unsigned c=0;c<4;++c)v[c]=std::max(v[c],pulse[c]*Settings::gain(level));
        }
        for(auto start:refills)if(now>=start&&now<start+duration[3]){
            const float age=float(now-start),kick=1.f-age/34.f;
            const std::array<float,4> pulse{.42f*kick,.75f*kick,.38f*kick,.38f*kick};
            for(unsigned c=0;c<4;++c)v[c]=std::max(v[c],pulse[c]*Settings::gain(s.refueling));
        }
        return v;
    }
private:
    inline static constexpr std::array<unsigned,4> duration{280,230,170,34};
    std::vector<uint64_t> refills;Feedback::SequenceState motion;std::array<uint64_t,4> until{};uint64_t phase_started{},last_sequence{};
};
}

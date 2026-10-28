#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace Frontend {
// Authored aircraft-tire spin-up chirp followed by speed-dependent friction.
// Separate from the ROM sequencer: no unverified sound-command substitution.
class CustomStageTireSound {
    double age=10,phase{},filtered{};uint32_t noise=1;
public:
    void start(){age=phase=filtered=0;noise=1;}
    void reset(){age=10;phase=filtered=0;}
    double sample(double speed,bool braking){
        if(age>=10)return 0;
        noise=noise*1664525u+1013904223u;const double hiss=double(noise>>8)/16777215.-.5;
        const double motion=std::clamp(speed/85.,0.,1.);
        const double frequency=850+600*std::exp(-age*5)+90*motion;
        phase=std::remainder(phase+6.283185307179586*frequency/48000,6.283185307179586);
        filtered+=(hiss-filtered)*.17;
        const double attack=std::min(1.,age/.008),envelope=std::exp(-age*5.2);
        const double rubber=(std::sin(phase)+.25*std::sin(phase*2))*(1+.10*std::sin(age*57));
        const double chirp=attack*envelope*(rubber*2300+(hiss-filtered)*900);
        const double friction=braking?attack*motion*(filtered*1200+(hiss-filtered)*320):0;
        age+=1./48000;return chirp+friction;
    }
};
}

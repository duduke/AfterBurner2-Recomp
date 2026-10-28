#include "frontend/frame_pacing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
    constexpr double tick=.016768;
    unsigned comparisons=0;
    for(unsigned scenario=0;scenario<4;++scenario){
        Frontend::FramePacing pacing(tick);double now=0;
        for(unsigned frame=0;frame<1000;++frame){
            double work=.012;
            if(frame%160==50)work=scenario==0?.080:scenario==1?.150:.060;
            if(scenario>=2&&frame%160==51)work=.050;
            const double target=(frame+1)*tick;
            now+=work;pacing.elapsed(work);
            const double expected=std::max(0.0,target-now);
            require(std::abs(pacing.delay()-expected)<1e-10,"frame pacing discarded ordinary deadline debt");
            const double wait=pacing.delay()+(scenario==3&&frame%37==0&&frame<980?.003:0);
            now+=wait;pacing.waited(wait);pacing.presented();++comparisons;
        }
        require(std::abs(now-1000*tick)<1e-10,"frame pacing permanently slowed simulation/audio supply");
    }
    Frontend::FramePacing pacing(tick);pacing.elapsed(10);
    unsigned unpaced=0;
    while(pacing.delay()==0&&unpaced<1000){pacing.presented();pacing.elapsed(0);++unpaced;}
    require(unpaced<=16&&unpaced>=15,"frame pacing suspension catch-up is unbounded");
    pacing.elapsed(.1);pacing.reset();
    require(pacing.delay()==tick,"frame pacing reset retained debt");
    std::cout<<"Frame pacing: "<<comparisons<<" absolute-deadline comparisons, repeated 60/80/150 ms stalls, wait overshoot, bounded suspension and reset passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

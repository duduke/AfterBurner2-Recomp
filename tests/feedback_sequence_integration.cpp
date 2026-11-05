#include "engine/game_session.hpp"
#include <fstream>
#include <iostream>
#include <string>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try{
    require(roms.load_aburner2(),"ROMs required");
    std::ifstream input("build/oracle/feedback_sequences.txt.inputs"),oracle("build/oracle/feedback_sequences.txt");
    std::string magic;unsigned frames,a,b,c;
    require(bool(input>>magic>>frames>>a>>b>>c)&&magic=="AB2PHYS1"&&frames>=17000&&a<256&&b<256&&c<256,"physical input header");
    require(bool(oracle>>magic)&&magic=="AB2SEQH1","sequence oracle header");
    GameSession original(roms),observed(roms);const std::array<uint8_t,3> dips{uint8_t(a),uint8_t(b),uint8_t(c)};
    original.set_dip_switches(dips);observed.set_dip_switches(dips);observed.capture_feedback(true);
    Feedback::SequenceCapture reference;std::array<unsigned,4> count{};std::vector<unsigned> pictures;
    struct Phase{unsigned cpu{},space{};bool write=true;uint64_t ns{};uint32_t pc{},address{},data{},mask{};};
    for(unsigned frame=0;frame<17000;++frame){
        unsigned n,pins;std::array<unsigned,8> axes{};
        require(bool(input>>n>>pins)&&n==frame,"physical input gap");for(auto& value:axes)require(bool(input>>value)&&value<256,"physical ADC");
        auto axis=[](unsigned v,unsigned lo,unsigned hi){return (float(v)-128.f)/float(v<128?128-lo:hi-128);};
        GameControls controls{axis(axes[0],32,224),axis(axes[1],64,192),axis(axes[2],0,255),!(pins&16),!(pins&32),!(pins&64),!(pins&8)};
        auto roundtrip=arcade_inputs(controls);require(roundtrip.buttons==pins,"button roundtrip");for(unsigned i=0;i<8;++i)require(roundtrip.analog[i]==axes[i],"ADC roundtrip");
        unsigned mode=0,carrier=0,speed=0,timer=0,refueling=0,handler=0,state=0,landed=0,base_speed=0;uint64_t ns=0;char tag;
        while(oracle>>tag){
            if(tag=='W'){Phase p;unsigned m,r,base,old;require(bool(oracle>>p.ns>>p.pc>>p.address>>p.data>>p.mask>>m>>r>>base>>old),"oracle phase");reference.observe(p,m,r);}
            else {require(tag=='F'&&bool(oracle>>n>>ns>>mode>>carrier>>speed>>timer>>refueling>>handler>>state>>landed>>base_speed)&&n==frame+1,"oracle frame");break;}
        }
        require(tag=='F',"oracle ended early");
        original.advance(controls);observed.advance(controls);
        auto x=original.frame(),y=observed.frame();require(x.info.number==y.info.number&&x.info.time_ns==y.info.time_ns,"sequence capture changed time");
        require(std::equal(x.pixels.begin(),x.pixels.end(),y.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"sequence capture changed RGB");
        require(original.take_audio()==observed.take_audio(),"sequence capture changed PCM");
        const auto f=observed.take_feedback();const auto expected=reference.take();
        if(f.sequence_events.size()!=expected.size()){
            std::cerr<<"frame "<<frame+1<<" native/MAME event count="<<f.sequence_events.size()<<'/'<<expected.size()<<" ns="<<f.ns<<'/'<<ns<<'\n';
            for(const auto& event:f.sequence_events)std::cerr<<"native cue "<<unsigned(event.cue)<<" at "<<event.ns<<'\n';
            for(const auto& event:expected)std::cerr<<"MAME cue "<<unsigned(event.cue)<<" at "<<event.ns<<'\n';
            throw std::runtime_error("native sequence event count differs from MAME frame");
        }
        for(unsigned i=0;i<expected.size();++i){require(f.sequence_events[i].cue==expected[i].cue,"native sequence cue differs from MAME");++count[unsigned(expected[i].cue)];
            if(expected[i].cue==Feedback::SequenceCue::touchdown){pictures.push_back(frame+2);pictures.push_back(frame+30);pictures.push_back(frame+90);}
        }
        if(std::find(pictures.begin(),pictures.end(),frame)!=pictures.end()){
            std::ofstream picture("build/haptics/landing-"+std::to_string(frame)+".ppm",std::ios::binary);
            picture<<"P6\n"<<320<<" 224\n255\n";
            for(const auto& pixel:y.pixels){picture.put(char(pixel.r));picture.put(char(pixel.g));picture.put(char(pixel.b));}
        }
        const auto motion=Feedback::SequenceCapture::state(mode,carrier,speed,timer,refueling,handler,state,landed,base_speed);
        if(!(f.motion.motion==motion.motion&&f.motion.speed==motion.speed&&f.motion.timer==motion.timer&&f.motion.refueling==motion.refueling)){
            std::cerr<<"frame "<<frame+1<<" ns native/MAME="<<f.ns<<'/'<<ns<<" motion/speed/timer/refueling native="<<unsigned(f.motion.motion)<<'/'<<f.motion.speed<<'/'<<f.motion.timer<<'/'<<f.motion.refueling
                <<" MAME="<<unsigned(motion.motion)<<'/'<<motion.speed<<'/'<<motion.timer<<'/'<<motion.refueling<<" raw mode/carrier="<<mode<<'/'<<carrier<<'\n';
            throw std::runtime_error("native sequence snapshot differs from MAME");
        }
        if(frame%4000==3999)std::cout<<"Sequence native checkpoint "<<frame+1<<" frames\n"<<std::flush;
    }
    for(auto n:count)require(n>0,"native sequence coverage missing");
    std::cout<<"Sequence native: 17000 of 17000 MAME event/state frames identical, 0 skipped; capture-on/off RGB/PCM/time identical; touchdown/launch/coupling/refill=";
    for(auto n:count)std::cout<<n<<'/';std::cout<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

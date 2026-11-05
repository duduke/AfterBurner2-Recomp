#include "engine/feedback_sequences.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Phase {unsigned cpu{},space{};bool write=true;uint32_t pc{},address{},data{},mask{};uint64_t ns{};};
int main(int argc,char** argv)try{
    std::ifstream in(argc>1?argv[1]:"build/oracle/feedback_sequences.txt");std::string magic;
    require(bool(in>>magic)&&magic=="AB2SEQH1","missing sequence fixture");
    Feedback::SequenceCapture capture;
    std::vector<std::pair<Feedback::SequenceCue,uint64_t>> expected;
    std::array<unsigned,4> counts{};std::array<unsigned,7> motions{};uint64_t sequence=0;unsigned frames=0,base_frames=0,refuel_frames=0;
    char tag;
    while(in>>tag){
        if(tag=='W'){
            Phase p;unsigned mode,refueling,base,old;
            require(bool(in>>p.ns>>p.pc>>p.address>>p.data>>p.mask>>mode>>refueling>>base>>old),"truncated phase");
            capture.observe(p,mode,refueling);
            const auto a=p.address&0x3fffff;
            // Independent references use committed state/flag edges and actual
            // latch data, without relying on the production observer's PCs.
            auto add=[&](Feedback::SequenceCue cue){expected.emplace_back(cue,p.ns);};
            if(mode==16&&base&&a==base+60&&(p.mask&0xff00)&&(p.data&0x100)&&!(old&0x100))add(Feedback::SequenceCue::touchdown);
            if(mode==16&&base&&a==base+40&&p.mask==65535&&old==8&&p.data==12)add(Feedback::SequenceCue::launch);
            if(mode==15&&a==0x3f90b0&&p.mask==65535&&old==2&&p.data==3)add(Feedback::SequenceCue::launch);
            if(mode==16&&refueling&&a==0x3f9102&&(p.mask&0xff00)&&(p.data&0x100)&&!(old&0x100))add(Feedback::SequenceCue::coupling);
            if(mode==16&&refueling&&(a&~1u)==0xe8016&&(p.mask&255)&&(p.data&255)==0x9b)add(Feedback::SequenceCue::refill);
        }else if(tag=='F'){
            unsigned n,mode,carrier,speed,timer,refueling,handler,state,landed,base_speed;uint64_t ns;
            require(bool(in>>n>>ns>>mode>>carrier>>speed>>timer>>refueling>>handler>>state>>landed>>base_speed),"truncated frame");
            require(n==++frames,"frame gap");
            const auto motion=Feedback::SequenceCapture::state(mode,carrier,speed,timer,refueling,handler,state,landed,base_speed);
            using M=Feedback::Motion;
            M reference=M::none;
            if(mode==15){if(carrier==2)reference=M::spool;else if(carrier==3||carrier==4)reference=M::roll;else if(carrier>=5&&carrier<11)reference=M::climb;}
            if(mode==16&&handler==0x111be)reference=M::approach;
            if(mode==16&&handler==0x1122e){
                ++base_frames;
                switch(state){case 0:reference=M::approach;break;case 4:reference=landed?M::rollout:M::approach;break;
                    case 8:reference=M::parked;break;case 12:reference=base_speed>=320?M::climb:M::roll;break;case 16:reference=M::climb;break;}
            }
            require(motion.motion==reference,"sequence motion differs from MAME controller observations");
            require(motion.refueling==(mode==16&&refueling),"refueling state mismatch");refuel_frames+=motion.refueling;
            ++motions[unsigned(motion.motion)];
            const auto events=capture.take();require(events.size()==expected.size(),"sequence event count differs");
            for(unsigned i=0;i<events.size();++i){const auto& e=events[i];require(e.sequence>sequence&&e.ns<=ns,"sequence event ordering");sequence=e.sequence;
                require(e.cue==expected[i].first&&e.ns==expected[i].second,"sequence event/timestamp differs");++counts[unsigned(e.cue)];}
            expected.clear();require(capture.take().empty(),"sequence event consumed twice");
        }else throw std::runtime_error("unknown sequence record");
    }
    require(in.eof()&&expected.empty(),"incomplete sequence fixture");
    for(auto n:counts)require(n>0,"missing contact/launch/coupling/refill witness");
    for(unsigned i=1;i<motions.size();++i)require(motions[i]>0,"missing motion phase witness");
    require(counts[1]>=2&&base_frames&&refuel_frames,"missing airbase/carrier/refueling coverage");
    std::cout<<"Sequence MAME: "<<frames<<" of "<<frames<<" frames identical, 0 skipped; touchdown/launch/coupling/refill=";
    for(auto n:counts)std::cout<<n<<'/';std::cout<<"; base="<<base_frames<<" refueling="<<refuel_frames<<"; all six motion phases observed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

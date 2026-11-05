#include "engine/feedback_sequences.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Phase {unsigned cpu{},space{};bool write=true;uint64_t ns{};uint32_t pc{},address{},data{},mask{};};
int main()try{
    std::ifstream in("build/oracle/feedback_finale.txt");std::string magic;require(bool(in>>magic)&&magic=="AB2FINH1","finale MAME fixture required");
    Feedback::SequenceCapture capture;std::vector<uint64_t> contacts;unsigned frames=0,kicks=0,approach=0,rollout=0,late=0,stopped=0,tas=0;
    char tag;
    while(in>>tag){
        if(tag=='W'){Phase p;unsigned mode,old,handler;require(bool(in>>p.ns>>p.pc>>p.address>>p.data>>p.mask>>mode>>old>>handler),"truncated finale write");capture.observe(p,mode,false);
            // Independent witness: a real $11F16 aircraft's contact flag changes
            // from clear to set, rather than a production PC or cached latch.
            if(handler==0x11f16&&(p.address&63)==24&&(p.mask&0xff00)&&(p.data&0x8000)){
                ++tas;if(!(old&0x8000))contacts.push_back(p.ns);
            }
        }else if(tag=='F'){
            unsigned n,mode,speed,record,handler,flags,stage,depth,y,carrier;uint64_t ns;
            require(bool(in>>n>>ns>>mode>>speed>>record>>handler>>flags>>stage>>depth>>y>>carrier)&&n==++frames,"finale frame gap");
            using M=Feedback::Motion;M expected=M::none;
            if(mode>=16&&mode<=20){if(handler==0x11e3e||(handler==0x11f16&&(flags&0xc000)==0))expected=M::approach;
                else if(handler==0x11f16&&(flags&0xc000)==0x8000)expected=M::rollout;}
            const auto actual=Feedback::SequenceCapture::state(mode,0,speed,0,false,0,0,false,0,handler,flags);
            require(actual.motion==expected,"finale motion differs from MAME flags/handler");
            require(actual.speed==(expected==M::rollout?speed:0),"finale speed differs from MAME");
            if(expected!=M::none)require(record==capture.finale_record,"finale aircraft identity differs from independent pool scan");
            approach+=expected==M::approach;rollout+=expected==M::rollout;late+=expected==M::rollout&&mode!=16;stopped+=handler==0x120aa&&actual.motion==M::none;
            auto events=capture.take();require(events.size()==contacts.size(),"finale contact missing or duplicated");
            for(unsigned i=0;i<events.size();++i){require(events[i].cue==Feedback::SequenceCue::touchdown&&events[i].ns==contacts[i]&&events[i].ns<=ns,"finale contact/timestamp differs from MAME");++kicks;}
            contacts.clear();
        }else throw std::runtime_error("unknown finale fixture row");
    }
    require(in.eof()&&contacts.empty()&&frames==74452&&kicks==1&&tas>1&&approach&&rollout&&late&&stopped,"finale positive/lifetime coverage incomplete");
    std::cout<<"Finale MAME: "<<frames<<" of "<<frames<<" event/motion frames identical, 0 skipped; contact="<<kicks<<" TAS writes="<<tas<<" approach="<<approach<<" rollout="<<rollout<<" post-flight rollout="<<late<<" settled-stop="<<stopped<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "frontend/native_practice.hpp"
#include "frontend/native_haptics.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
Feedback::Motion expected_motion(unsigned mode,unsigned handler,unsigned flags){
    using M=Feedback::Motion;if(mode<16||mode>20)return M::none;
    if(handler==0x11e3e)return M::approach;
    if(handler==0x11f16){if((flags&0xc000)==0)return M::approach;if((flags&0xc000)==0x8000)return M::rollout;}
    return M::none;
}
void natural(){
    std::ifstream pins("build/oracle/feedback_finale.txt.inputs"),gold("build/oracle/feedback_finale.txt");std::string magic;unsigned frames,a,b,c;
    require(bool(pins>>magic>>frames>>a>>b>>c)&&magic=="AB2PHYS1"&&frames==74452,"actual finale pins required");require(bool(gold>>magic)&&magic=="AB2FINH1","finale oracle required");
    GameSession game(roms);game.set_dip_switches({uint8_t(a),uint8_t(b),uint8_t(c)});game.capture_feedback(true);
    unsigned compared=0,contacts=0,late=0;std::vector<uint64_t> expected;
    for(unsigned frame=0;frame<frames;++frame){
        unsigned n,p;std::array<unsigned,8> axes{};require(bool(pins>>n>>p)&&n==frame,"finale physical pin gap");for(auto& v:axes)require(bool(pins>>v)&&v<256,"finale ADC");
        auto axis=[](unsigned v,unsigned lo,unsigned hi){return (float(v)-128.f)/float(v<128?128-lo:hi-128);};
        GameControls input{axis(axes[0],32,224),axis(axes[1],64,192),axis(axes[2],0,255),!(p&16),!(p&32),!(p&64),!(p&8)};
        auto physical=arcade_inputs(input);require(physical.buttons==p,"finale button roundtrip");for(unsigned i=0;i<8;++i)require(physical.analog[i]==axes[i],"finale ADC roundtrip");
        char tag=0;unsigned mode=0,speed=0,record=0,handler=0,flags=0,stage=0,depth=0,y=0,carrier=0;uint64_t ns=0;
        while(gold>>tag){if(tag=='W'){uint64_t at;unsigned pc,addr,data,mask,m,old,h;require(bool(gold>>at>>pc>>addr>>data>>mask>>m>>old>>h),"finale oracle write");
                if(h==0x11f16&&(addr&63)==24&&(mask&0xff00)&&(data&0x8000)&&!(old&0x8000))expected.push_back(at);
            }else{require(tag=='F'&&bool(gold>>n>>ns>>mode>>speed>>record>>handler>>flags>>stage>>depth>>y>>carrier)&&n==frame+1,"finale oracle frame");break;}}
        require(tag=='F',"finale oracle ended early");game.advance(input);game.take_audio();const auto f=game.take_feedback();
        require(f.number==n&&f.ns==ns&&game.run_status().mode==mode,"native finale mode/time differs from MAME at frame "+std::to_string(n));
        if(record){++compared;const auto motion=expected_motion(mode,handler,flags);
            require(f.motion.motion==motion&&f.motion.speed==(motion==Feedback::Motion::rollout?speed:0),"native finale motion differs from MAME at frame "+std::to_string(n));
            require(f.sequence_events.size()==expected.size(),"native finale contact count differs from MAME");
            for(unsigned i=0;i<expected.size();++i){require(f.sequence_events[i].cue==Feedback::SequenceCue::touchdown,"native finale contact kind");
                require(f.sequence_events[i].ns==expected[i],"native finale contact timestamp differs from MAME");++contacts;}
            late+=motion==Feedback::Motion::rollout&&mode!=16;
        }else require(expected.empty(),"MAME contact has no active finale aircraft");
        expected.clear();if(n%10000==0)std::cout<<"Finale native checkpoint "<<n<<" frames\n"<<std::flush;
    }
    require(contacts==1&&late>0&&compared>3000,"native finale MAME coverage incomplete");
    std::cout<<"Finale native: 74452/74452 MAME mode/time frames identical, 0 skipped; "<<compared<<" finale motion/event frames identical; contact="<<contacts<<" post-flight rollout="<<late<<'\n';
}
void practice(){
    for(unsigned stage:{23u,22u}){
        GameSession plain(roms),game(roms);game.capture_feedback(true);Frontend::NativePractice p,q;
        const std::array<uint8_t,3> dips{255,255,221};p.start(plain,stage,dips);q.start(game,stage,dips);
        Frontend::FeedbackMixer mixer;Frontend::FeedbackSettings settings;settings.enabled=true;settings.body=settings.triggers=100;
        settings.cannon=settings.missile=settings.impact=settings.boost=settings.takeoff=settings.refueling=0;settings.landing=3;
        unsigned frames=0,kicks=0,rollout=0,late=0,stopped=0;uint64_t contact=0;
        for(;frames<10000;++frames){GameControls input{};input.throttle=1;input.cannon=input.missile=true;if(stage==22){input.stick_x=(frames/90)%2?1.f:-1.f;input.stick_y=1.f;}plain.advance(input);game.advance(input);
            auto a=plain.frame(),b=game.frame();require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"Practice capture changed time");
            require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto x,auto y){return x.r==y.r&&x.g==y.g&&x.b==y.b;}),"Practice capture changed RGB");require(plain.take_audio()==game.take_audio(),"Practice capture changed PCM");
            auto f=game.take_feedback();auto now=f.ns/1000000;mixer.consume(f,now,settings);const auto motors=mixer.sample(now,settings);
            for(const auto& e:f.sequence_events)if(e.cue==Feedback::SequenceCue::touchdown){++kicks;contact=f.number;require(motors[0]>50000,"Practice finale touchdown did not reach motors");}
            if(f.motion.motion==Feedback::Motion::rollout){++rollout;late+=game.run_status().mode!=16;require(motors[0]>0,"Practice finale rollout lost motor output");}
            if(rollout&&f.motion.motion==Feedback::Motion::none){++stopped;require(motors==std::array<Uint16,4>{},"Practice finale settled aircraft kept rumbling");}
            if(contact&&(f.number==contact+2||f.number==contact+20)){
                std::ofstream pic("build/haptics/finale-practice-"+std::to_string(stage)+"-"+std::to_string(f.number)+".ppm",std::ios::binary);pic<<"P6\n320 224\n255\n";for(auto v:b.pixels){pic.put(char(v.r));pic.put(char(v.g));pic.put(char(v.b));}}
            if(game.run_status().mode>=21)break;
        }
        if(!(frames<10000&&kicks==1&&rollout>0&&late>0&&stopped>0))std::cerr<<"Practice coverage stage="<<stage<<" frames="<<frames<<" mode="<<game.run_status().mode<<" kicks/rollout/late/stop="<<kicks<<"/"<<rollout<<"/"<<late<<"/"<<stopped<<"\n";
        require(frames<10000&&kicks==1&&rollout>0&&late>0&&stopped>0,"finale Practice coverage incomplete");
        std::cout<<"Finale Practice "<<stage<<(stage==22?" -> 23":"")<<": "<<frames+1<<" paired RGB/PCM/time frames identical, 0 skipped; contact="<<kicks<<" rollout="<<rollout<<" post-flight rollout="<<late<<" settled="<<stopped<<'\n'<<std::flush;
    }
}
int main(int argc,char** argv)try{require(roms.load_aburner2(),"ROMs required");require(argc==2,"Use natural or practice");if(std::string(argv[1])=="natural")natural();else{require(std::string(argv[1])=="practice","unknown finale mode");practice();}}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

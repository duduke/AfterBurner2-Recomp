#include "frontend/interactive_stages.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using Stage=Frontend::InteractiveStage;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
// Physical input only: no replacement position/velocity or expected outputs
// from the model. This controller demonstrates reachable success, not realism.
GameControls pilot(const Stage& s){
    GameControls c;
    if(s.continuing()){if(s.phase==Stage::Phase::ready||s.phase==Stage::Phase::takeoff)c.throttle=1;return c;}
    c.stick_x=std::clamp(float((s.target_x()-s.x)*.22-s.bank*.8),-1.f,1.f);
    if(s.type==Stage::Kind::refuel){
        c.stick_y=std::clamp(float((s.target_height()-s.height)*.3-s.pitch*.8),-1.f,1.f);
        c.throttle=std::clamp(float((s.range-6)*.06-(s.speed-140)*.04),-1.f,1.f);
    }else{
        const double descent=s.height<5.5?1.5:std::clamp((s.height-s.target_height())*.9+5.4,1.,10.);
        c.stick_y=std::clamp(float((6-descent)/7),-1.f,1.f);
    }
    return c;
}
int main()try{
    GameControls back;back.stick_y=-1;back.stick_x=.4f;back.throttle=.7f;back.cannon=true;
    auto mapped=Frontend::interactive_stage_controls(back);
    require(mapped.stick_y==1 && mapped.stick_x==back.stick_x && mapped.throttle==back.throttle && mapped.cannon,
            "Pull back did not reverse only pitch");
    Stage pull,push;pull.start();push.start();GameControls forward;forward.stick_y=1;
    for(int i=0;i<60;++i){pull.tick(Frontend::interactive_stage_controls(back));push.tick(Frontend::interactive_stage_controls(forward));}
    require(pull.height> -8 && pull.pitch>0 && push.height< -8 && push.pitch<0,
            "Back/Down must raise the nose; forward/Up must lower it");
    Stage fuel;
    for(int i=0;i<100;++i)fuel.tick({});
    require(fuel.elapsed==0 && fuel.range==100,"Briefing advanced simulation");
    fuel.start();
    for(int i=0;i<3600 && !fuel.finished();++i)fuel.tick(pilot(fuel));
    require(fuel.phase==Stage::Phase::success && fuel.fuel==1,"Input-only tanker approach cannot complete");
    const auto time=fuel.elapsed;
    fuel.tick({});require(fuel.elapsed==time,"Result continued simulating");

    Stage contact;contact.start();
    for(int i=0;i<3600 && contact.hold<.7 && !contact.finished();++i)contact.tick(pilot(contact));
    require(contact.hold>=.7,"No sustained contact before disconnect test");
    for(int i=0;i<60 && contact.hold>0;++i){auto c=pilot(contact);c.stick_x=1;contact.tick(c);}
    require(contact.hold==0 && contact.fuel==0 && !contact.finished(),"Broken contact retained transfer");
    for(int i=0;i<3600 && !contact.finished();++i)contact.tick(pilot(contact));
    require(contact.phase==Stage::Phase::success,"Cannot reacquire after disconnect");

    Stage overshoot;overshoot.start();GameControls boost;boost.throttle=1;
    for(int i=0;i<900 && !overshoot.finished();++i)overshoot.tick(boost);
    require(overshoot.phase==Stage::Phase::failed && overshoot.range< -3,"Overshoot was accepted");
    Stage timeout;timeout.start();
    for(int i=0;i<3601 && !timeout.finished();++i)timeout.tick({});
    require(timeout.phase==Stage::Phase::failed && timeout.elapsed>=60,"Stationary rendezvous never expires");

    Stage deck(Stage::Kind::landing);deck.start();
    for(int i=0;i<1800 && !deck.finished();++i)deck.tick(pilot(deck));
    require(deck.phase==Stage::Phase::success,"Input-only carrier recovery cannot complete");
    Stage easy(Stage::Kind::landing);easy.start();
    for(int i=0;i<1800 && !easy.finished();++i){
        auto c=pilot(easy);c.stick_y=easy.height<12?1.f:0.f;easy.tick(c);
    }
    require(easy.phase==Stage::Phase::success,"Holding back below height 12 must give a forgiving flare");
    // Directed authored-policy cases pin the wider acceptance window while
    // retaining meaningful failure outside it. These are not ROM fixtures.
    auto touchdown=[](double lateral,double velocity,double roll,double nose,double distance){
        Stage s(Stage::Kind::landing);s.start();s.x=lateral;s.speed=velocity;
        s.bank=roll;s.pitch=nose;s.range=distance;s.height=2.01;
        GameControls c;c.stick_x=roll;c.stick_y=nose;c.throttle=(velocity-90)/35;
        s.tick(c);return s.phase;
    };
    require(touchdown(10,112,.4,.3,20)==Stage::Phase::rollout,"Forgiving offset/speed/bank/early touchdown rejected");
    require(touchdown(0,90,0,.4,-240)==Stage::Phase::rollout,"Extended landing area rejected");
    require(touchdown(14,90,0,.4,-20)==Stage::Phase::failed,"Clear lateral miss accepted");
    require(touchdown(0,124,0,.4,-20)==Stage::Phase::failed,"Excessive landing speed accepted");
    require(touchdown(0,90,.65,.4,-20)==Stage::Phase::failed,"Excessive touchdown bank accepted");
    Stage hard(Stage::Kind::landing);hard.start();
    for(int i=0;i<1800 && !hard.finished();++i){auto c=pilot(hard);c.stick_y=0;hard.tick(c);}
    require(hard.phase==Stage::Phase::failed,"Unflared touchdown accepted");
    Stage bolter(Stage::Kind::landing);bolter.start();GameControls climb;climb.stick_y=1;
    for(int i=0;i<1800 && !bolter.finished();++i)bolter.tick(climb);
    require(bolter.phase==Stage::Phase::bolter,"High approach did not bolter");
    Stage miss(Stage::Kind::landing);miss.start();
    for(int i=0;i<1800 && !miss.finished();++i){auto c=pilot(miss);c.stick_x=1;miss.tick(c);}
    require(miss.phase==Stage::Phase::failed,"Off-deck touchdown accepted");

    // Quantize each controller axis to key presses/releases to prove both
    // challenges are also reachable with digital keyboard-style controls.
    for(auto kind:{Stage::Kind::refuel,Stage::Kind::landing,Stage::Kind::runway,Stage::Kind::jungle}){
        Stage keys(kind);keys.start();double remainder[3]{};
        for(int i=0;i<3600 && !keys.finished();++i){
            auto c=pilot(keys);float* axes[]{&c.stick_x,&c.stick_y,&c.throttle};
            for(int j=0;j<3;++j){const float v=*axes[j];remainder[j]+=std::abs(v);
                *axes[j]=remainder[j]>=1?(v<0?-1.f:1.f):0;
                if(remainder[j]>=1)remainder[j]-=1;}
            keys.tick(c);
        }
        require(keys.phase==Stage::Phase::success,"Digital input approach cannot complete");
    }
    for(auto kind:{Stage::Kind::landing,Stage::Kind::runway,Stage::Kind::jungle}){
        Stage stopped(kind);stopped.start();
        for(unsigned i=0;i<1800 && stopped.phase!=Stage::Phase::ready;++i){auto c=pilot(stopped);c.throttle=0;stopped.tick(c);}
        require(stopped.phase==Stage::Phase::ready && stopped.missiles==stopped.missile_maximum,"Reload did not reach a ready prompt");
        GameControls half;half.throttle=.5f;
        for(unsigned i=0;i<180;++i)stopped.tick(half);
        require(stopped.phase==Stage::Phase::ready,"Takeoff began without full throttle");
        GameControls full;full.throttle=1;stopped.tick(full);
        require(stopped.phase==Stage::Phase::takeoff,"Full throttle did not launch takeoff");
    }

    Stage a,b;a.start();b.start();
    for(int i=0;i<1500;++i){auto c=pilot(a);a.tick(c);b.tick(c);}
    require(a.phase==b.phase && a.range==b.range && a.height==b.height && a.x==b.x,"Identical input replay diverged");
    a.reset(Stage::Kind::landing);
    require(a.phase==Stage::Phase::briefing && a.elapsed==0 && a.hold==0 && a.message.empty() && a.range==900,"Retry leaked earlier challenge state");
    GameControls invalid;invalid.stick_x=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{a.tick(invalid);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected && a.elapsed==0,"Invalid axis mutated state");
    std::cout<<"Interactive stages: analog/digital refuel/recovery, assisted flare/tolerances, disconnect/reacquire, overshoot, timeout, hard/off-deck landing, bolter, replay, reset and invalid axes passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "frontend/native_haptics.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace Frontend;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Motors {std::array<Uint16,4> v{};int body_calls{},trigger_calls{};bool fail_body{};};
bool SDLCALL body(void* u,Uint16 a,Uint16 b){auto& m=*static_cast<Motors*>(u);++m.body_calls;m.v[0]=a;m.v[1]=b;return !m.fail_body;}
bool SDLCALL triggers(void* u,Uint16 a,Uint16 b){auto& m=*static_cast<Motors*>(u);++m.trigger_calls;m.v[2]=a;m.v[3]=b;return true;}
SDL_JoystickID attach(Motors& m,bool pad,bool output=true){SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);d.type=pad?SDL_JOYSTICK_TYPE_GAMEPAD:SDL_JOYSTICK_TYPE_FLIGHT_STICK;d.nbuttons=pad?SDL_GAMEPAD_BUTTON_COUNT:1;d.naxes=pad?SDL_GAMEPAD_AXIS_COUNT:2;d.vendor_id=0x2345;d.product_id=pad?1:output?2:3;d.name="Feedback test device";d.userdata=&m;if(output)d.Rumble=body;if(pad)d.RumbleTriggers=triggers;if(pad){d.axis_mask=(1<<SDL_GAMEPAD_AXIS_COUNT)-1;d.button_mask=(1<<SDL_GAMEPAD_BUTTON_COUNT)-1;}return SDL_AttachVirtualJoystick(&d);}
void pump(NativeInput& i,NativeHaptics& h){SDL_UpdateJoysticks();SDL_Event e;while(SDL_PollEvent(&e)){h.event(e);i.event(e);}h.sync();}
Feedback::Frame frame(uint64_t n,std::initializer_list<Feedback::Event> events={},bool boost=false){return {n,n*16666667,true,boost,events,false,{}, {}};}
int main()try{
    struct Phase {unsigned cpu{},space{};bool write{};uint32_t address{},pc{},data{},mask{};uint64_t ns{};};
    Feedback::Capture capture;
    Phase p{0,0,true,0x3fd012,0x9ebc,0x9f7c,0xffff,10};capture.observe(p,false);require(capture.take(1,20,1,255).events.empty(),"attract launch rumble");
    p.write=false;capture.observe(p,true);p.write=true;p.cpu=1;capture.observe(p,true);p.cpu=0;p.mask=255;capture.observe(p,true);p.mask=0xffff;p.address=0x3fd010;capture.observe(p,true);
    auto rejected=capture.take(2,20,16,252);require(rejected.events.empty()&&!rejected.boost,"read/CPU/lane/high-word or early-boost false cue");p.address=0x3fd012;capture.observe(p,true);
    auto witnessed=capture.take(3,20,16,253);require(witnessed.events.size()==1&&witnessed.events[0].cue==Feedback::Cue::missile&&witnessed.boost,"successful allocation/full-throttle cue missing");
    FeedbackSettings s;s.body=s.triggers=100;FeedbackMixer mixer;
    mixer.consume(frame(1,{{Feedback::Cue::missile,16666667,1},{Feedback::Cue::cannon,16666667,2}}),1000,s);
    auto v=mixer.sample(1000,s);require(v[0]>40000&&v[1]>40000&&v[2]>40000&&v[3]>39000,"overlapping sources cancel a motor");
    s.swap=true;auto swap=mixer.sample(1000,s);require(swap[2]==v[3]&&swap[3]==v[2],"trigger swap");s.swap=false;
    mixer.consume(frame(1,{{Feedback::Cue::cannon,16666667,2}}),1050,s);require(mixer.sample(1050,s)[1]<v[1],"duplicate board frame retriggered shot");
    require(mixer.sample(1100,s)==std::array<Uint16,4>{},"stalled simulation watchdog");
    mixer.clear();mixer.consume(frame(20,{{Feedback::Cue::impact,0,3}}),2000,s);require(mixer.sample(2000,s)==std::array<Uint16,4>{},"stale catchup pulse");
    mixer.consume(frame(21,{},true),2020,s);require(mixer.sample(2020,s)[0]>0,"continuous boost missing");
    mixer.clear();require(mixer.sample(2021,s)==std::array<Uint16,4>{},"stop retained boost");
    s.cannon=false;mixer.consume(frame(22,{{Feedback::Cue::cannon,22*16666667,4}}),2040,s);require(mixer.sample(2040,s)==std::array<Uint16,4>{},"disabled category emitted");
    // A hit remains strong throughout each visible ROM damage variant, then stops.
    for(auto handler:{0xe662u,0xe8d0u,0xe94eu}){
        Feedback::Capture damage_capture;
        for(unsigned state=0;state<7;++state){
            auto f=damage_capture.take(state+1,(state+1)*16666667,16,0,handler,state);
            const bool visible=handler==0xe662 || (handler==0xe8d0&&state<=4) || (handler==0xe94e&&state==0);
            require(f.damage==visible,"damage controller visibility");
            mixer.restart();mixer.consume(f,4000,s);auto hit=mixer.sample(4000,s);
            require(visible?(hit[0]==65535&&hit[1]==65535&&hit[2]>50000):hit==std::array<Uint16,4>{},"sustained impact strength/lifetime");
        }
    }
    mixer.restart();auto falling=frame(1);falling.damage=true;mixer.consume(falling,5000,s);
    for(unsigned n=2;n<180;++n){falling.number=n;mixer.consume(falling,5000+n*17,s);require(mixer.sample(5000+n*17,s)[0]==65535,"long animation faded out");}
    require(mixer.sample(5000+179*17+100,s)==std::array<Uint16,4>{},"damage watchdog");
    s.body=s.triggers=50;require(mixer.sample(5000+179*17,s)[0]>49000,"default hit too weak");s.body=s.triggers=100;
    auto recovered=frame(180);mixer.consume(recovered,8100,s);require(mixer.sample(8100,s)==std::array<Uint16,4>{},"recovered jet retained impact");
    for(bool right:{false,true}){mixer.restart();mixer.consume(frame(1,{{Feedback::Cue::missile,16666667,1,right}}),9000,s);auto side=mixer.sample(9000,s);
        require(right?(side[1]>0&&side[0]==0&&side[3]>0&&side[2]==0):(side[0]>0&&side[1]==0&&side[2]>0&&side[3]==0),"missile side routing");}
    Uint16 previous=0;
    for(int level=0;level<4;++level){s.impact=level;mixer.restart();falling.number=1;mixer.consume(falling,10000,s);auto hit=mixer.sample(10000,s);
        require(level?hit[0]>previous:hit[0]==0,"effect strength levels");previous=hit[0];}s.impact=3;
    {
        Feedback::SequenceCapture capture;Phase event{0,0,true,0x3f90b0,0x104ea,3,0xffff,10};
        capture.observe(event,1,false);require(capture.take().empty(),"attract sequence feedback");
        event.cpu=1;capture.observe(event,15,false);event.cpu=0;event.write=false;capture.observe(event,15,false);
        require(capture.take().empty(),"non-main/read sequence feedback");
        require(Feedback::SequenceCapture::state(1,3,256,0,false,0,0,false,0).motion==Feedback::Motion::none,"attract continuous sequence");
    }
    {
        Feedback::SequenceCapture capture;Phase init{0,0,true,0x3fb912,0x11b18,0x1b30,0xffff,10};
        capture.observe(init,16,false);require(capture.finale_record==0x3fb940,"finale adjacent aircraft identity");
        Phase contact{0,0,true,0x3fb958,0x11f4c,0x8080,0xff00,20};
        capture.observe(contact,1,false);require(capture.take().empty(),"finale attract contact leaked");
        for(unsigned i=0;i<10;++i)capture.observe(contact,16,false);
        auto kicks=capture.take();require(kicks.size()==1&&kicks[0].cue==Feedback::SequenceCue::touchdown,"finale repeated TAS contact");
        require(Feedback::SequenceCapture::state(1,0,126,0,false,0,0,false,0,0x11f16,0x8000).motion==Feedback::Motion::none,"finale attract motion leaked");
        auto f=frame(1);f.flight=false;f.motion=Feedback::SequenceCapture::state(18,0,118,0,false,0,0,false,0,0x11f16,0x8000);
        mixer.restart();mixer.consume(f,10900,s);require(mixer.sample(10900,s)[0]>0,"finale landing stopped outside flight");
        f.number=2;f.motion=Feedback::SequenceCapture::state(18,0,56,0,false,0,0,false,0,0x11f16,0xc000);mixer.consume(f,10917,s);
        require(mixer.sample(10917,s)==std::array<Uint16,4>{},"finale feedback continued after rollout");
        capture.clear();require(!capture.finale_record,"finale identity survived restart");
        capture.observe(contact,16,false);require(capture.take().empty(),"retired finale record produced contact");
    }
    // Sequence layers have distinct phase texture and strong finite contact kicks.
    using Motion=Feedback::Motion;using SC=Feedback::SequenceCue;
    for(auto phase:{Motion::approach,Motion::rollout,Motion::parked,Motion::spool,Motion::roll,Motion::climb}){
        mixer.restart();auto f=frame(1);f.flight=false;f.motion={phase,256,15,false};mixer.consume(f,11000,s);
        require(mixer.sample(11000,s)[0]>0,"non-flight carrier sequence lost rumble");
        if(phase==Motion::spool||phase==Motion::roll||phase==Motion::rollout){auto a=mixer.sample(11000,s),b=mixer.sample(11025,s);require(a!=b,"sequence texture is flat");}
        require(mixer.sample(11100,s)==std::array<Uint16,4>{},"sequence watchdog missing");
    }
    for(auto cue:{SC::touchdown,SC::launch,SC::coupling,SC::refill}){
        mixer.restart();auto f=frame(1);f.sequence_events={{cue,f.ns,1}};mixer.consume(f,12000,s);auto kick=mixer.sample(12000,s);
        require(kick[0]>20000&&kick[1]>35000,"contact/refill kick missing");
        if(cue==SC::coupling)require(kick[0]>50000&&kick[0]>kick[1],"coupling lacks a distinct heavy jolt");
        auto quiet=frame(2);quiet.sequence_events=f.sequence_events;mixer.consume(quiet,12050,s);
        if(cue==SC::refill)require(mixer.sample(12050,s)==std::array<Uint16,4>{},"refill bumps merge into continuous buzz");
        else require(mixer.sample(12050,s)[0]<kick[0],"contact envelope did not decay");
        quiet.number=3;mixer.consume(quiet,12300,s);require(mixer.sample(12300,s)==std::array<Uint16,4>{},"contact envelope outlived animation");
        mixer.clear();require(mixer.sample(12301,s)==std::array<Uint16,4>{},"sequence explicit stop missing");
    }
    for(int effect=0;effect<3;++effect){
        int* levels[]{&s.landing,&s.takeoff,&s.refueling};Uint16 previous=0;
        for(int level=0;level<4;++level){*levels[effect]=level;mixer.restart();auto f=frame(1);
            f.sequence_events={{effect==0?SC::touchdown:effect==1?SC::launch:SC::coupling,f.ns,1}};mixer.consume(f,13000,s);auto kick=mixer.sample(13000,s);
            require(level?kick[1]>previous:kick[1]==0,"sequence per-effect gain/off");previous=kick[1];}
        *levels[effect]=3;
    }
    require(refill_audio_delay_ms(.08,.04,true)==120&&refill_audio_delay_ms(.08,.04,false)==0,"playback queue delay");
    mixer.restart();auto queued=frame(1);queued.sequence_events={{SC::refill,queued.ns,1}};
    mixer.consume(queued,14000,s,100);require(mixer.sample(14000,s)==std::array<Uint16,4>{},"refill led queued audio");
    auto next=frame(2);mixer.consume(next,14080,s,100);
    require(mixer.sample(14115,s)==std::array<Uint16,4>{}&&mixer.sample(14116,s)[1]>40000,"queued refill event/frame offset");
    mixer.clear();require(mixer.sample(14117,s)==std::array<Uint16,4>{},"pause retained delayed refill");
    mixer.restart();queued=frame(1);queued.sequence_events={{SC::refill,queued.ns,1}};mixer.consume(queued,15000,s,100);
    queued=frame(5);queued.sequence_events={{SC::refill,queued.ns,2}};mixer.consume(queued,15067,s,100);
    require(mixer.sample(15116,s)[1]>40000,"later refill replaced a pending bump");
    next=frame(6);mixer.consume(next,15150,s,100);require(mixer.sample(15150,s)==std::array<Uint16,4>{}&&mixer.sample(15183,s)[1]>40000,"delayed refill cadence lost its gap");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x2345/0x0001,0x2345/0x0002,0x2345/0x0003");SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");require(SDL_Init(SDL_INIT_GAMEPAD),SDL_GetError());
    const auto path=(std::filesystem::temp_directory_path()/"ab2-feedback-regression.cfg").string();std::filesystem::remove(path);
    Motors a,b,c;auto pad=attach(a,true),raw=attach(b,false),none=attach(c,false,false);require(pad&&raw&&none,"attach devices");
    {
        NativeInput input("");NativeHaptics h(input,path);pump(input,h);
        require(h.device_id()==pad&&h.body_supported()&&h.triggers_supported(),"capability detection");
        require(!h.settings.enabled,"feedback should start disabled");h.consume(frame(1,{},true),1000);h.update(false,1000);require(a.body_calls==0,"default off emitted output");
        for(unsigned channel=0;channel<4;++channel){h.test(channel,1000);require(a.v[channel]>0,"individual motor test failed");for(unsigned other=0;other<4;++other)if(other!=channel)require(a.v[other]==0,"test leaked another motor");h.stop();require(a.v==std::array<Uint16,4>{},"explicit stop missing");}
        h.test(0,1000);h.update(true,1651);require(a.v==std::array<Uint16,4>{},"finite menu test expiry");
        h.adjust(0,1);h.consume(frame(2,{{Feedback::Cue::cannon,2*16666667,1},{Feedback::Cue::missile,2*16666667,2}}),2000);h.update(false,2000);require(a.v[0]&&a.v[1]&&a.v[2]&&a.v[3],"SDL four channel mix missing");
        h.update(true,2020);require(a.v==std::array<Uint16,4>{},"pause/menu did not stop");
        h.test(1,2100);SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;h.event(e);input.event(e);require(a.v==std::array<Uint16,4>{},"focus loss did not stop");h.test(0,2100);require(a.v==std::array<Uint16,4>{},"unfocused test leak");e.type=SDL_EVENT_WINDOW_FOCUS_GAINED;input.event(e);
        h.test(1,2200);h.restart();require(a.v==std::array<Uint16,4>{},"restart did not stop");
        h.consume(frame(1,{{Feedback::Cue::cannon,16666667,1}}),2300);h.update(false,2300);require(a.v[1]>0,"fresh board sequence rejected after restart");
        h.cycle(1);require(a.v==std::array<Uint16,4>{}&&h.device_id()==raw&&h.body_supported()&&!h.triggers_supported(),"device change or raw capability");h.test(0,2400);require(b.v[0]>0,"raw joystick rumble missing");h.test(2,2400);require(b.v==std::array<Uint16,4>{},"unsupported trigger fallback duplicated body");
        h.cycle(1);require(h.device_id()==none&&!h.body_supported(),"unsupported device reported rumble");h.test(0);require(c.body_calls==0,"unsupported device rumble call");h.cycle(1);
        a.fail_body=true;h.test(0,2500);h.stop();const int failed=a.body_calls;h.consume(frame(2,{{Feedback::Cue::cannon,2*16666667,2}},true),2520);h.update(false,2520);require(a.body_calls==failed&&a.v[3]>0,"failed body channel retried or disabled working triggers");a.fail_body=false;h.test(1,2550);require(a.v[1]>0,"explicit retest did not recover failed channel");
        require(SDL_DetachVirtualJoystick(pad),"detach pad");pump(input,h);require(!h.device_id(),"unplug silently routed to another controller");a.v={};pad=attach(a,true);pump(input,h);require(h.device_id()==pad,"GUID reconnect not restored");h.update(false,2700);require(a.v==std::array<Uint16,4>{},"reconnect replayed pulse");
        h.adjust(0,1);h.adjust(0,1);h.consume(frame(100,{{Feedback::Cue::cannon,100*16666667,1}}),3000);h.update(false,3000);require(a.v[3]>0,"re-enabled capture rejected reset event sequence");
        h.adjust(2,1);h.adjust(3,-1);h.adjust(8,1);
        h.adjust(4,1);require(h.settings.cannon==0,"High to Off");h.adjust(4,1);require(h.settings.cannon==1,"Off to Low");h.adjust(4,1);require(h.settings.cannon==2,"Low to Med");h.adjust(4,1);require(h.settings.cannon==3,"Med to High");h.adjust(4,-1);require(h.settings.cannon==2,"reverse level cycle");h.adjust(15,1);h.adjust(16,-1);h.adjust(17,-1);
    }
    {NativeInput input("");NativeHaptics h(input,path);require(h.settings.enabled&&h.settings.body==60&&h.settings.triggers==40&&h.settings.swap&&h.settings.cannon==2&&h.settings.landing==0&&h.settings.takeoff==2&&h.settings.refueling==2,"preferences did not persist");}
    {std::ofstream f(path);f<<"AB2HAPTICS1\n1 900 -1 1 1 1 1 0 -\n";}
    {NativeInput input("");NativeHaptics h(input,path);require(!h.settings.enabled&&h.settings.body==50,"malformed preferences fallback");}
    {std::ofstream f(path);f<<"AB2HAPTICS1\n1 50 50 1 0 1 1 0 -\n";}
    {NativeInput input("");NativeHaptics h(input,path);require(h.settings.cannon==3&&h.settings.missile==0&&h.settings.impact==3,"legacy cue migration");require(h.settings.landing==3&&h.settings.takeoff==3&&h.settings.refueling==3,"legacy sequence defaults");}
    {std::ofstream f(path);f<<"AB2HAPTICS2\n1 50 50 4 3 3 3 0 -\n";}
    {NativeInput input("");NativeHaptics h(input,path);require(!h.settings.enabled,"invalid effect level accepted");}
    SDL_DetachVirtualJoystick(pad);SDL_DetachVirtualJoystick(raw);SDL_DetachVirtualJoystick(none);SDL_Quit();std::filesystem::remove(path);
    std::cout<<"PASS SDL body/trigger/raw rumble, overlap, gains, stale/duplicate suppression, finite tests, failures, pause/focus/restart, hotplug and persistence\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}

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
Feedback::Frame frame(uint64_t n,std::initializer_list<Feedback::Event> events={},bool boost=false){return {n,n*16666667,true,boost,events};}
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
    }
    {NativeInput input("");NativeHaptics h(input,path);require(h.settings.enabled&&h.settings.body==60&&h.settings.triggers==40&&h.settings.swap,"preferences did not persist");}
    {std::ofstream f(path);f<<"AB2HAPTICS1\n1 900 -1 1 1 1 1 0 -\n";}
    {NativeInput input("");NativeHaptics h(input,path);require(!h.settings.enabled&&h.settings.body==50,"malformed preferences fallback");}
    SDL_DetachVirtualJoystick(pad);SDL_DetachVirtualJoystick(raw);SDL_DetachVirtualJoystick(none);SDL_Quit();std::filesystem::remove(path);
    std::cout<<"PASS SDL body/trigger/raw rumble, overlap, gains, stale/duplicate suppression, finite tests, failures, pause/focus/restart, hotplug and persistence\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}

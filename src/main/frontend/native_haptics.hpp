#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include "engine/feedback_cues.hpp"
#include "frontend/native_input.hpp"

namespace Frontend {
struct FeedbackSettings {
    bool enabled{};int body=50,triggers=50;
    bool cannon=true,missile=true,impact=true,boost=true,swap{};
    std::string device;
};
// Wall-clock envelopes are independent of emulation and speaker playback.
class FeedbackMixer {
public:
    void clear(){until.fill(0);boost=false;flight=false;fresh=0;}
    void restart(){clear();last_frame=last_sequence=0;}
    void consume(const Feedback::Frame& f,uint64_t now,const FeedbackSettings& s) {
        if(!f.number || f.number<=last_frame)return;
        last_frame=f.number;fresh=now;flight=f.flight;boost=f.boost&&s.boost;
        if(!flight){clear();return;}
        for(auto e:f.events){if(e.sequence<=last_sequence)continue;last_sequence=e.sequence;
            if(e.ns>f.ns || f.ns-e.ns>100000000)continue;
            unsigned i=unsigned(e.cue);const bool allow=i==0?s.cannon:i==1?s.missile:s.impact;
            if(allow)until[i]=now+duration[i];
        }
    }
    std::array<Uint16,4> sample(uint64_t now,const FeedbackSettings& s) const {
        std::array<float,4> v{};
        if(!flight || now-fresh>=100)return {};
        if(boost){v[0]=.10f;v[1]=.04f;}
        constexpr std::array<std::array<float,4>,3> strength{{{.12f,.65f,0,.60f},{.65f,.18f,.65f,0},{.90f,.75f,.45f,.45f}}};
        for(unsigned i=0;i<3;++i)if(until[i]>now){float envelope=float(until[i]-now)/duration[i];
            for(unsigned c=0;c<4;++c)v[c]=std::max(v[c],strength[i][c]*envelope);}
        if(s.swap)std::swap(v[2],v[3]);
        std::array<Uint16,4> out{};for(unsigned c=0;c<4;++c)out[c]=Uint16(std::clamp(v[c]*(c<2?s.body:s.triggers)/100.f,0.f,1.f)*65535);return out;
    }
private:
    inline static constexpr std::array<unsigned,3> duration{65,180,350};
    std::array<uint64_t,3> until{};uint64_t last_frame{},last_sequence{},fresh{};bool flight{},boost{};
};
class NativeHaptics {
public:
    FeedbackSettings settings;std::string notice;
    explicit NativeHaptics(NativeInput& input,std::string path):input(input),path(std::move(path)) {
        load();input.before_close=[this](SDL_JoystickID id){if(id==instance)release();};sync();
    }
    ~NativeHaptics(){release();input.before_close={};}
    NativeHaptics(const NativeHaptics&)=delete;
    void sync() {
        const auto& devices=input.connected();
        if(!chosen && settings.device.empty() && !devices.empty()){settings.device=devices.front().key;chosen=true;save();}
        const NativeInput::Device* target=nullptr;
        if(instance)for(auto& d:devices)if(d.id==instance&&d.key==settings.device)target=&d;
        if(!target){unsigned matches=0;for(auto& d:devices)if(d.key==settings.device){target=&d;++matches;}
            if(matches!=1)target=nullptr;}
        if(target && target->id==instance)return;
        release();if(!target)return;
        acquire(*target);
    }
    std::string device_name() const {return instance?"#"+std::to_string(instance)+" "+name:"NOT CONNECTED";}
    std::string capabilities() const {return std::string(body?"BODY":"NO BODY")+" / "+(triggers?"TRIGGERS":"NO TRIGGERS");}
    SDL_JoystickID device_id() const {return instance;}
    bool body_supported() const {return body;}
    bool triggers_supported() const {return triggers;}
    void cycle(int delta) {
        const auto& d=input.connected();if(d.empty()){notice="NO CONTROLLER CONNECTED";return;}
        int index=-1;for(unsigned i=0;i<d.size();++i)if(d[i].id==instance)index=int(i);
        index=(index+int(d.size())+delta)%int(d.size());release();settings.device=d[index].key;chosen=true;
        acquire(d[index]);save();
    }
    void adjust(int row,int delta) {
        stop();notice.clear();
        if(row==0){settings.enabled=!settings.enabled;mixer.restart();}
        if(row==1){cycle(delta);return;}
        if(row==2)settings.body=std::clamp(settings.body+delta*10,0,100);
        if(row==3)settings.triggers=std::clamp(settings.triggers+delta*10,0,100);
        if(row==4)settings.cannon=!settings.cannon;if(row==5)settings.missile=!settings.missile;
        if(row==6)settings.impact=!settings.impact;if(row==7)settings.boost=!settings.boost;
        if(row==8)settings.swap=!settings.swap;save();
    }
    // Tests are finite and deliberately available with gameplay feedback disabled.
    void test(unsigned motor,uint64_t now=SDL_GetTicks()) {
        stop();if(!input.focused){notice="WINDOW MUST HAVE FOCUS";return;}
        if(!instance){notice="NO CONTROLLER CONNECTED";return;}
        if((motor<2&&!body)||(motor>=2&&!triggers)){notice="MOTOR UNSUPPORTED";return;}
        const int gain=motor<2?settings.body:settings.triggers;if(!gain){notice="SET STRENGTH ABOVE ZERO";return;}
        if(motor<2)body_failed=false;else trigger_failed=false;
        std::array<Uint16,4> v{};v[motor]=Uint16(45000*gain/100);
        send(v,650);test_until=now+650;notice=(motor<2?body_failed:trigger_failed)?"TEST FAILED - SEE CONSOLE":"TESTING FOR 650 MS";
    }
    void consume(const Feedback::Frame& f,uint64_t now=SDL_GetTicks()) {
        if(settings.enabled && input.focused)mixer.consume(f,now,settings);
    }
    void update(bool suspended,uint64_t now=SDL_GetTicks()) {
        sync();if(!input.focused){stop();return;}
        if(test_until){if(now>=test_until){stop();notice="TEST FINISHED";}return;}
        if(suspended || !settings.enabled){stop();return;}
        if(now-last_send<20)return;
        auto v=mixer.sample(now,settings);
        if(v!=last || std::any_of(v.begin(),v.end(),[](auto x){return x!=0;})){send(v,80);last_send=now;last=v;}
    }
    void event(const SDL_Event& e) {
        if(e.type==SDL_EVENT_GAMEPAD_REMAPPED && e.gdevice.which==instance){release();return;}
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST || e.type==SDL_EVENT_QUIT)stop();
    }
    void stop(){mixer.clear();test_until=0;if(instance && (last!=std::array<Uint16,4>{} || active)){send({},0);}last={};active=false;last_send=0;}
    void restart(){stop();mixer.restart();}
private:
    NativeInput& input;std::string path,name;SDL_JoystickID instance{};
    SDL_Gamepad* pad{};SDL_Joystick* stick{};SDL_Haptic* haptic{};
    bool body{},triggers{},simple{},body_failed{},trigger_failed{},active{},chosen{};
    FeedbackMixer mixer;std::array<Uint16,4> last{};uint64_t test_until{},last_send{};
    void release(){stop();if(haptic)SDL_CloseHaptic(haptic);haptic=nullptr;instance=0;pad=nullptr;stick=nullptr;body=triggers=simple=false;name.clear();}
    void acquire(const NativeInput::Device& d) {
        instance=d.id;pad=d.pad;stick=d.stick;name=d.name;body_failed=trigger_failed=false;
        auto p=pad?SDL_GetGamepadProperties(pad):SDL_GetJoystickProperties(stick);
        body=SDL_GetBooleanProperty(p,pad?SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN:SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN,false);
        triggers=SDL_GetBooleanProperty(p,pad?SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN:SDL_PROP_JOYSTICK_CAP_TRIGGER_RUMBLE_BOOLEAN,false);
        if(!body && SDL_WasInit(SDL_INIT_HAPTIC) && SDL_IsJoystickHaptic(stick)) {
            haptic=SDL_OpenHapticFromJoystick(stick);
            if(haptic && SDL_HapticRumbleSupported(haptic) && SDL_InitHapticRumble(haptic))simple=body=true;
            else {if(haptic)SDL_CloseHaptic(haptic);haptic=nullptr;}
        }
        notice=body?"READY - TEST MOTORS BELOW":"DEVICE HAS NO RUMBLE";
        std::cerr<<"Feedback: "<<name<<" instance="<<instance<<" body="<<body<<" triggers="<<triggers<<" simple-haptic="<<simple<<'\n';
    }
    void send(const std::array<Uint16,4>& v,unsigned ms) {
        if(!instance)return;active=std::any_of(v.begin(),v.end(),[](auto x){return x!=0;});
        if(body && (!body_failed || !active)) {
            bool ok=simple?(active?SDL_PlayHapticRumble(haptic,std::max(v[0],v[1])/65535.f,ms):SDL_StopHapticRumble(haptic)):
                pad?SDL_RumbleGamepad(pad,v[0],v[1],ms):SDL_RumbleJoystick(stick,v[0],v[1],ms);
            if(!ok && !body_failed){body_failed=true;notice="BODY RUMBLE FAILED";std::cerr<<notice<<": "<<SDL_GetError()<<'\n';}
        }
        if(triggers && (!trigger_failed || !active)) {
            bool ok=pad?SDL_RumbleGamepadTriggers(pad,v[2],v[3],ms):SDL_RumbleJoystickTriggers(stick,v[2],v[3],ms);
            if(!ok && !trigger_failed){trigger_failed=true;notice="TRIGGER RUMBLE FAILED";std::cerr<<notice<<": "<<SDL_GetError()<<'\n';}
        }
    }
    void load(){if(path.empty())return;std::ifstream f(path);std::string magic;FeedbackSettings s;
        if(f>>magic && magic=="AB2HAPTICS1" && f>>s.enabled>>s.body>>s.triggers>>s.cannon>>s.missile>>s.impact>>s.boost>>s.swap>>s.device && s.body>=0&&s.body<=100&&s.triggers>=0&&s.triggers<=100){settings=s;if(settings.device=="-")settings.device.clear();chosen=!settings.device.empty();}}
    void save(){if(path.empty())return;std::ofstream f(path);f<<"AB2HAPTICS1\n"<<settings.enabled<<' '<<settings.body<<' '<<settings.triggers<<' '<<settings.cannon<<' '<<settings.missile<<' '<<settings.impact<<' '<<settings.boost<<' '<<settings.swap<<' '<<(settings.device.empty()?"-":settings.device)<<'\n';}
};
}

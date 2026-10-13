#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "engine/game_controls.hpp"

namespace Frontend {
// Directional half axes also allow buttons/hats to drive analog arcade inputs.
// Only this adapter owns dead zones and remapping; board inputs remain unchanged.
class NativeInput {
public:
    static constexpr int actions=11;
    inline static constexpr const char* labels[actions]={"RIGHT","LEFT","UP","DOWN","BOOST","BRAKE","CANNON","MISSILE","COIN","START","MENU"};
    struct Binding { char kind='-'; int index{},direction=1; bool operator==(const Binding&) const=default; };
    struct Profile { std::array<Binding,actions> bindings{}; int deadzone=15,sensitivity=100; bool full_throttle{}; };
    struct Device { SDL_JoystickID id{}; SDL_Gamepad* pad{}; SDL_Joystick* stick{}; std::string key,name; };
    explicit NativeInput(std::string path):path(std::move(path)) { profiles["keyboard"]=defaults(false,true);load();discover(); }
    ~NativeInput() { for(auto& d:devices)close(d); }
    NativeInput(const NativeInput&)=delete;
    std::string status;
    bool focused=true;
    bool waiting() const {return capture_action>=0;}
    std::string device_name() const {return editing==0?"KEYBOARD":devices[editing-1].name;}
    Profile& profile() { return profiles[editing==0?"keyboard":devices[editing-1].key]; }
    const std::vector<Device>& connected() const {return devices;}
    void cycle(int delta) {cancel();editing=(editing+int(devices.size())+1+delta)%(int(devices.size())+1);}
    void deadzone(int delta) {profile().deadzone=std::clamp(profile().deadzone+delta*5,0,50);save();block();}
    void sensitivity(int delta) {profile().sensitivity=std::clamp(profile().sensitivity+delta*10,50,200);save();block();}
    bool raw_device() const {return editing && !devices[editing-1].pad;}
    void throttle_mode() {if(raw_device()) {profile().full_throttle=!profile().full_throttle;save();block();}}
    void reset() {profile()=editing?device_defaults(devices[editing-1]):defaults(false,true);save();block();}
    void clear(int action) {profile().bindings[action]={};save();block();}
    void begin(int action) {
        capture_action=action;armed.fill(false);status="RELEASE THEN MOVE OR PRESS";block();
        if(editing) {
            auto& d=devices[editing-1];int count=d.pad?SDL_GAMEPAD_AXIS_COUNT:std::min(256,SDL_GetNumJoystickAxes(d.stick));
            for(int i=0;i<count;++i)armed[i]=std::abs(axis(d,i))<12000;
        }
    }
    void cancel() {capture_action=-1;status.clear();block();}
    void block() {blocked=true;}
    std::string binding_name(int action) {
        auto b=profile().bindings[action];
        if(b.kind=='-')return "UNBOUND";
        if(b.kind=='k')return SDL_GetScancodeName(static_cast<SDL_Scancode>(b.index));
        if(editing && devices[editing-1].pad) {
            const char* name=b.kind=='b'?SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b.index)):SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(b.index));
            if(name)return std::string(name)+(b.kind=='a'?(b.direction>0?" +":" -"):"");
        }
        return std::string(b.kind=='b'?"BUTTON ":b.kind=='a'?"AXIS ":"HAT ")+std::to_string(b.index)+(b.kind=='b'?"":b.kind=='a'?(b.direction>0?" +":" -"):" / "+std::to_string(b.direction));
    }
    // Always process lifecycle events, even when the menu consumes controls.
    bool event(const SDL_Event& e) {
        if(e.type==SDL_EVENT_JOYSTICK_ADDED || e.type==SDL_EVENT_JOYSTICK_REMOVED || e.type==SDL_EVENT_GAMEPAD_ADDED || e.type==SDL_EVENT_GAMEPAD_REMOVED || e.type==SDL_EVENT_GAMEPAD_REMAPPED) {discover();return false;}
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) {focused=false;cancel();}
        if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) {focused=true;block();}
        if(!waiting())return false;
        const bool control_event=e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP ||
            e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type==SDL_EVENT_GAMEPAD_BUTTON_UP || e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION ||
            e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN || e.type==SDL_EVENT_JOYSTICK_BUTTON_UP || e.type==SDL_EVENT_JOYSTICK_AXIS_MOTION || e.type==SDL_EVENT_JOYSTICK_HAT_MOTION;
        if(!control_event)return false;
        if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key==SDLK_ESCAPE) {cancel();return true;}
        Binding b;
        if(editing==0) {
            if(e.type!=SDL_EVENT_KEY_DOWN || e.key.repeat)return true;
            if(e.key.scancode==SDL_SCANCODE_UNKNOWN || reserved(e.key.scancode)) {status="KEY RESERVED FOR SYSTEM";return true;}
            b={'k',int(e.key.scancode),1};
        } else {
            auto& d=devices[editing-1];
            if(d.pad) {
                if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.which==d.id) {
                    if(e.gbutton.button==SDL_GAMEPAD_BUTTON_GUIDE)return true;
                    b={'b',e.gbutton.button,1};
                }
                if(e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.which==d.id)b=capture_axis(e.gaxis.axis,e.gaxis.value);
            } else {
                if(e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN && e.jbutton.which==d.id)b={'b',e.jbutton.button,1};
                if(e.type==SDL_EVENT_JOYSTICK_AXIS_MOTION && e.jaxis.which==d.id)b=capture_axis(e.jaxis.axis,e.jaxis.value);
                if(e.type==SDL_EVENT_JOYSTICK_HAT_MOTION && e.jhat.which==d.id) {
                    for(int direction:{SDL_HAT_UP,SDL_HAT_RIGHT,SDL_HAT_DOWN,SDL_HAT_LEFT})if(e.jhat.value&direction){b={'h',e.jhat.hat,direction};break;}
                }
            }
        }
        if(b.kind!='-') {profile().bindings[capture_action]=b;capture_action=-1;save();block();}
        return true;
    }
    SDL_Keycode menu_key(const SDL_Event& e) const {
        if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            switch(e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_NORTH:return SDLK_SPACE;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:return SDLK_UP;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:return SDLK_DOWN;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:return SDLK_LEFT;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:return SDLK_RIGHT;
                case SDL_GAMEPAD_BUTTON_SOUTH:case SDL_GAMEPAD_BUTTON_START:return SDLK_RETURN;
                case SDL_GAMEPAD_BUTTON_EAST:case SDL_GAMEPAD_BUTTON_GUIDE:return SDLK_ESCAPE;
            }
        }
        if(e.type==SDL_EVENT_JOYSTICK_HAT_MOTION && !SDL_IsGamepad(e.jhat.which)) {
            if(e.jhat.value==SDL_HAT_UP)return SDLK_UP;if(e.jhat.value==SDL_HAT_DOWN)return SDLK_DOWN;
            if(e.jhat.value==SDL_HAT_LEFT)return SDLK_LEFT;if(e.jhat.value==SDL_HAT_RIGHT)return SDLK_RIGHT;
        }
        if(e.type==SDL_EVENT_JOYSTICK_BUTTON_DOWN && !SDL_IsGamepad(e.jbutton.which)) {
            if(e.jbutton.button==0)return SDLK_RETURN;if(e.jbutton.button==1)return SDLK_ESCAPE;
        }
        return 0;
    }
    bool menu_pressed() {
        const auto* keys=SDL_GetKeyboardState(nullptr);
        bool down=value(profiles.at("keyboard").bindings[10],nullptr,keys,0)>.5f;
        for(auto& d:devices)down=down || value(profiles.at(d.key).bindings[10],&d,keys,profiles.at(d.key).deadzone)>.5f;
        const bool pressed=down && !menu_held && focused && !waiting() && !blocked;
        menu_held=down;return pressed;
    }
    GameControls read(bool menu=false) {
        const auto* keys=SDL_GetKeyboardState(nullptr);std::array<float,actions> values{};
        for(int a=0;a<actions;++a)values[a]=value(profiles.at("keyboard").bindings[a],nullptr,keys,0);
        float absolute_throttle=0;bool have_throttle=false;
        for(auto& d:devices) {
            const auto& p=profiles.at(d.key);
            for(int a=0;a<actions;++a) {
                if(!d.pad && p.full_throttle && (a==4 || a==5)) {
                    if(a==4 && p.bindings[a].kind=='a' && p.bindings[a].index<SDL_GetNumJoystickAxes(d.stick)) {absolute_throttle=std::clamp(axis(d,p.bindings[a].index)*p.bindings[a].direction/32767.f,-1.f,1.f);have_throttle=true;}
                    continue;
                }
                float v=value(p.bindings[a],&d,keys,p.deadzone);
                if(a<4)v=std::min(1.f,v*p.sensitivity/100.f);
                values[a]=std::max(values[a],v);
            }
        }
        const bool held=std::any_of(values.begin(),values.end(),[](float x){return x>0;});
        if(blocked && !held)blocked=false;
        if(menu || waiting() || blocked || !focused)return {};
        // Preserve original keyboard precedence for simultaneous opposite keys.
        GameControls result;
        result.stick_x=values[0]>0?values[0]:-values[1];
        result.stick_y=values[3]>0?-values[3]:values[2];
        result.throttle=values[5]>0?-values[5]:values[4]>0?values[4]:have_throttle?absolute_throttle:0;
        result.cannon=values[6]>.5f;result.missile=values[7]>.5f;result.coin=values[8]>.5f;result.start=values[9]>.5f;
        return result;
    }
    static Profile defaults(bool gamepad,bool keyboard=false) {
        Profile p;
        if(keyboard) {
            const int keys[]{SDL_SCANCODE_RIGHT,SDL_SCANCODE_LEFT,SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_SPACE,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_5,SDL_SCANCODE_RETURN};
            for(int a=0;a<10;++a)p.bindings[a]={'k',keys[a],1};
        } else {
            p.bindings[0]={'a',0,1};p.bindings[1]={'a',0,-1};p.bindings[2]={'a',1,-1};p.bindings[3]={'a',1,1};
            if(gamepad) {
                p.bindings[4]={'a',SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,1};p.bindings[5]={'a',SDL_GAMEPAD_AXIS_LEFT_TRIGGER,1};
                const int buttons[]{SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_START};
                for(int a=0;a<4;++a)p.bindings[a+6]={'b',buttons[a],1};
            } else {
                p.bindings[4]={'b',4,1};p.bindings[5]={'b',5,1};for(int a=0;a<4;++a)p.bindings[a+6]={'b',a,1};
            }
        }
        return p;
    }
private:
    std::string path;std::map<std::string,Profile> profiles;std::vector<Device> devices;
    int editing{},capture_action=-1;bool blocked{},menu_held{};std::array<bool,256> armed{};
    static bool reserved(SDL_Scancode k) {return k==SDL_SCANCODE_ESCAPE || k==SDL_SCANCODE_F1 || k==SDL_SCANCODE_F3 || k==SDL_SCANCODE_F5 || k==SDL_SCANCODE_F11 || k==SDL_SCANCODE_P;}
    static void close(Device& d) {if(d.pad)SDL_CloseGamepad(d.pad);else if(d.stick)SDL_CloseJoystick(d.stick);}
    static int axis(const Device& d,int index) {return d.pad?SDL_GetGamepadAxis(d.pad,static_cast<SDL_GamepadAxis>(index)):SDL_GetJoystickAxis(d.stick,index);}
    Binding capture_axis(int index,int v) {if(std::abs(v)<12000)armed[index]=true;if(armed[index] && std::abs(v)>20000)return {'a',index,v>0?1:-1};return {};}
    static float value(Binding b,const Device* d,const bool* keys,int deadzone) {
        if(b.kind=='k')return keys[b.index]?1.f:0.f;
        if(!d)return 0;
        if(!d->pad && ((b.kind=='b' && b.index>=SDL_GetNumJoystickButtons(d->stick)) || (b.kind=='a' && b.index>=SDL_GetNumJoystickAxes(d->stick)) || (b.kind=='h' && b.index>=SDL_GetNumJoystickHats(d->stick))))return 0;
        if(b.kind=='b')return (d->pad?SDL_GetGamepadButton(d->pad,static_cast<SDL_GamepadButton>(b.index)):SDL_GetJoystickButton(d->stick,b.index))?1.f:0.f;
        if(b.kind=='h')return !d->pad && (SDL_GetJoystickHat(d->stick,b.index)&b.direction)?1.f:0.f;
        if(b.kind=='a') {
            float x=std::max(0.f,axis(*d,b.index)*b.direction/32767.f);float dz=deadzone/100.f;
            return std::clamp((x-dz)/(1-dz),0.f,1.f);
        }
        return 0;
    }
    static Profile device_defaults(const Device& d) {
        auto p=defaults(d.pad!=nullptr);
        // D-pad-only gamepads and arcade sticks need useful flight defaults too.
        if(d.pad && !SDL_GamepadHasAxis(d.pad,SDL_GAMEPAD_AXIS_LEFTX)) {
            p.bindings[0]={'b',SDL_GAMEPAD_BUTTON_DPAD_RIGHT,1};p.bindings[1]={'b',SDL_GAMEPAD_BUTTON_DPAD_LEFT,1};
        }
        if(d.pad && !SDL_GamepadHasAxis(d.pad,SDL_GAMEPAD_AXIS_LEFTY)) {
            p.bindings[2]={'b',SDL_GAMEPAD_BUTTON_DPAD_UP,1};p.bindings[3]={'b',SDL_GAMEPAD_BUTTON_DPAD_DOWN,1};
        }
        if(d.pad && !SDL_GamepadHasAxis(d.pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))p.bindings[4]={'b',SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,1};
        if(d.pad && !SDL_GamepadHasAxis(d.pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER))p.bindings[5]={'b',SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,1};
        return p;
    }
    void discover() {
        const auto selected_id=editing?devices[editing-1].id:0;
        cancel();
        // Keep handles for connected instances; do not open a mapped gamepad twice.
        int count=0;auto* ids=SDL_GetJoysticks(&count);
        for(auto i=devices.begin();i!=devices.end();) {
            bool exists=false;for(int n=0;n<count;++n)if(ids[n]==i->id)exists=true;
            if(!exists || bool(i->pad)!=SDL_IsGamepad(i->id)) {close(*i);i=devices.erase(i);}else ++i;
        }
        for(int n=0;n<count;++n) {
            if(std::any_of(devices.begin(),devices.end(),[&](auto& d){return d.id==ids[n];}))continue;
            Device d;d.id=ids[n];
            if(SDL_IsGamepad(d.id)) {d.pad=SDL_OpenGamepad(d.id);if(d.pad)d.stick=SDL_GetGamepadJoystick(d.pad);}
            else d.stick=SDL_OpenJoystick(d.id);
            if(!d.stick)continue;
            char guid[33];SDL_GUIDToString(SDL_GetJoystickGUID(d.stick),guid,sizeof(guid));
            d.key=std::string(d.pad?"pad-":"joy-")+guid;
            const char* name=d.pad?SDL_GetGamepadName(d.pad):SDL_GetJoystickName(d.stick);d.name=name?name:"UNKNOWN DEVICE";
            if(!profiles.contains(d.key))profiles[d.key]=device_defaults(d);
            devices.push_back(d);
        }
        SDL_free(ids);editing=0;
        for(unsigned i=0;i<devices.size();++i)if(devices[i].id==selected_id)editing=int(i)+1;
        block();
    }
    static bool valid(Binding b,bool keyboard,bool pad) {
        if(b.kind=='-')return true;
        if(keyboard)return b.kind=='k' && b.index>0 && b.index<SDL_SCANCODE_COUNT && !reserved(static_cast<SDL_Scancode>(b.index));
        if(b.index<0 || b.index>255)return false;
        if(b.kind=='b')return !pad || (b.index<SDL_GAMEPAD_BUTTON_COUNT && b.index!=SDL_GAMEPAD_BUTTON_GUIDE);
        if(b.kind=='a')return (b.direction==1 || b.direction==-1) && (!pad || b.index<SDL_GAMEPAD_AXIS_COUNT);
        return !pad && b.kind=='h' && (b.direction==1 || b.direction==2 || b.direction==4 || b.direction==8);
    }
    void load() {
        std::ifstream file(path);std::string line;if(!std::getline(file,line) || line!="AB2INPUT3")return;
        while(std::getline(file,line)) {
            std::istringstream row(line);std::string key;int dz,sensitivity,full;Profile p;
            if(!(row>>key>>dz>>sensitivity>>full) || dz<0 || dz>50 || sensitivity<50 || sensitivity>200 || (full!=0 && full!=1))continue;
            const bool keyboard=key=="keyboard",pad=key.starts_with("pad-");
            if(!keyboard && !(key.size()==36 && (pad || key.starts_with("joy-")) && key.substr(4).find_first_not_of("0123456789abcdef")==std::string::npos))continue;
            p.deadzone=dz;p.sensitivity=sensitivity;p.full_throttle=full;bool ok=true;
            for(auto& b:p.bindings)if(!(row>>b.kind>>b.index>>b.direction) || !valid(b,keyboard,pad)) {ok=false;break;}
            std::string extra;if(row>>extra)ok=false;
            if(ok)profiles[key]=p;
        }
    }
    void save() {
        if(path.empty()) {status="INPUT SAVE FAILED";return;}
        const auto tmp=path+".tmp";std::ofstream file(tmp,std::ios::trunc);file<<"AB2INPUT3\n";
        for(auto& [key,p]:profiles) {file<<key<<' '<<p.deadzone<<' '<<p.sensitivity<<' '<<p.full_throttle;for(auto b:p.bindings)file<<' '<<b.kind<<' '<<b.index<<' '<<b.direction;file<<'\n';}
        file.close();std::error_code ec;if(file)std::filesystem::rename(tmp,path,ec);
        status=file && !ec?"INPUT SAVED":"INPUT SAVE FAILED";
    }
};
}

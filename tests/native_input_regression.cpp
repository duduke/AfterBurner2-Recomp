#include "frontend/native_input.hpp"
#include "frontend/controller_database.hpp"
#include <iostream>
#include <stdexcept>
using Frontend::NativeInput;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void pump(NativeInput& input) {SDL_UpdateJoysticks();SDL_Event e;while(SDL_PollEvent(&e))input.event(e);}
SDL_JoystickID attach(bool pad,bool digital=false) {
    SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);d.type=pad?SDL_JOYSTICK_TYPE_GAMEPAD:SDL_JOYSTICK_TYPE_FLIGHT_STICK;
    d.naxes=digital?0:pad?SDL_GAMEPAD_AXIS_COUNT:3;d.nbuttons=pad?SDL_GAMEPAD_BUTTON_COUNT:6;d.nhats=pad?0:1;
    d.vendor_id=0x1234;d.product_id=digital?3:pad?1:2;d.name=pad?"AB2 virtual pad":"AB2 virtual flightstick";
    if(pad) {d.axis_mask=digital?0:(1<<SDL_GAMEPAD_AXIS_COUNT)-1;d.button_mask=(1<<SDL_GAMEPAD_BUTTON_COUNT)-1;}
    auto id=SDL_AttachVirtualJoystick(&d);require(id!=0,SDL_GetError());return id;
}
int main()try {
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1234/0x0001,0x1234/0x0002,0x1234/0x0003");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    require(SDL_Init(SDL_INIT_GAMEPAD),SDL_GetError());
    require(Frontend::load_controller_database()>0,"community database failed to load");
    auto path=(std::filesystem::temp_directory_path()/"ab2-input-regression.cfg").string();std::filesystem::remove(path);
    auto pad=attach(true);auto raw=attach(false);
    auto* p=SDL_OpenJoystick(pad);auto* j=SDL_OpenJoystick(raw);require(p && j,"virtual devices failed");
    for(int a:{SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER})SDL_SetJoystickVirtualAxis(p,a,-32768);
    {
        NativeInput input(path);pump(input);require(input.connected().size()==2,"device discovery count");
        require(input.connected()[0].pad && !input.connected()[1].pad,"gamepad/raw classification");
        input.read();SDL_SetJoystickVirtualAxis(p,0,32767);SDL_SetJoystickVirtualAxis(p,1,-32768);
        SDL_SetJoystickVirtualButton(p,SDL_GAMEPAD_BUTTON_SOUTH,true);pump(input);
        auto c=input.read();require(c.stick_x==1 && c.stick_y==1 && c.cannon,"normalized gamepad controls");
        // SDL mapping changes must be reflected without a duplicate raw handle.
        char* original=SDL_GetGamepadMapping(input.connected()[0].pad);require(original,"mapping query failed");
        const std::string mapping(original);SDL_free(original);
        const auto token=mapping.find("a:b0,");require(token!=std::string::npos,"unexpected virtual mapping");
        auto remapped=mapping;remapped.replace(token,5,"a:b1,");
        require(SDL_SetGamepadMapping(pad,remapped.c_str()),"live mapping change failed");pump(input);
        require(input.connected().size()==2,"mapping change duplicated device");
        SDL_SetJoystickVirtualAxis(p,0,0);SDL_SetJoystickVirtualAxis(p,1,0);SDL_SetJoystickVirtualButton(p,0,false);pump(input);input.read();
        SDL_SetJoystickVirtualButton(p,0,true);pump(input);require(!input.read().cannon,"old raw button still used after SDL remap");
        SDL_SetJoystickVirtualButton(p,1,true);pump(input);require(input.read().cannon,"new raw button not used after SDL remap");
        SDL_SetJoystickVirtualButton(p,0,false);SDL_SetJoystickVirtualButton(p,1,false);pump(input);input.read();
        require(SDL_SetGamepadMapping(pad,mapping.c_str()),"mapping restore failed");pump(input);
        SDL_Event nav{};nav.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;nav.gbutton.which=pad;nav.gbutton.button=SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        require(input.menu_key(nav)==SDLK_DOWN,"gamepad menu navigation");
        nav.gbutton.button=SDL_GAMEPAD_BUTTON_GUIDE;require(input.menu_key(nav)==SDLK_ESCAPE,"guide menu back");
        SDL_SetJoystickVirtualAxis(p,0,2000);SDL_SetJoystickVirtualAxis(p,1,0);SDL_SetJoystickVirtualButton(p,SDL_GAMEPAD_BUTTON_SOUTH,false);pump(input);
        require(input.read().stick_x==0,"dead zone drift");
        SDL_SetJoystickVirtualAxis(p,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767);pump(input);require(input.read().throttle==1,"trigger throttle");
        SDL_SetJoystickVirtualAxis(p,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);pump(input);require(input.read().throttle==-1,"brake precedence");
        input.block();require(!input.read().throttle,"held buttons leak from menu");
        for(int a:{SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER})SDL_SetJoystickVirtualAxis(p,a,-32768);pump(input);input.read();
        input.cycle(2);require(input.raw_device(),"raw selection");
        input.deadzone(1);input.sensitivity(1);input.read();
        SDL_SetJoystickVirtualAxis(j,0,16384);pump(input);require(std::abs(input.read().stick_x-.4125f)<.001f,"deadzone rescaling and sensitivity");
        SDL_SetJoystickVirtualAxis(j,0,0);pump(input);input.read();
        input.profile().bindings[10]={'b',5,1};input.read();
        SDL_SetJoystickVirtualButton(j,5,true);pump(input);require(input.menu_pressed() && !input.menu_pressed(),"menu binding rising edge");
        SDL_SetJoystickVirtualButton(j,5,false);pump(input);input.menu_pressed();input.read();
        input.begin(6);SDL_SetJoystickVirtualButton(j,3,true);pump(input);
        require(!input.waiting() && input.profile().bindings[6]==NativeInput::Binding{'b',3,1},"raw button capture");
        require(!input.read().cannon,"captured button leaked");SDL_SetJoystickVirtualButton(j,3,false);pump(input);input.read();
        SDL_SetJoystickVirtualButton(j,3,true);pump(input);require(input.read().cannon && input.read().start,"shared bindings allowed");
        SDL_SetJoystickVirtualButton(j,3,false);pump(input);input.read();
        SDL_SetJoystickVirtualAxis(j,2,-32768);pump(input);input.begin(4);
        SDL_SetJoystickVirtualAxis(j,2,-30000);pump(input);require(input.waiting(),"idle endpoint auto captured");
        SDL_SetJoystickVirtualAxis(j,2,0);pump(input);SDL_SetJoystickVirtualAxis(j,2,32767);pump(input);
        require(!input.waiting() && input.profile().bindings[4]==NativeInput::Binding{'a',2,1},"raw axis capture");
        input.throttle_mode();input.read();require(input.read().throttle==1,"full range throttle output");
        SDL_SetJoystickVirtualAxis(j,2,-32768);pump(input);require(input.read().throttle==-1,"full range throttle negative");
        input.begin(0);SDL_SetJoystickVirtualHat(j,0,SDL_HAT_RIGHT);pump(input);require(input.profile().bindings[0].kind=='h',"hat capture");
        SDL_SetJoystickVirtualHat(j,0,SDL_HAT_CENTERED);pump(input);input.read();SDL_SetJoystickVirtualHat(j,0,SDL_HAT_RIGHT);pump(input);require(input.read().stick_x==1,"hat polling");
        SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;input.event(e);require(input.read().stick_x==0 && input.read().throttle==0,"focus loss leak");
        e.type=SDL_EVENT_WINDOW_FOCUS_GAINED;input.event(e);require(input.read().stick_x==0,"focus gain held input");
        SDL_SetJoystickVirtualHat(j,0,SDL_HAT_CENTERED);pump(input);input.read();
        input.cycle(1);input.begin(6);e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_F1;e.key.key=SDLK_F1;
        require(input.event(e) && input.waiting(),"reserved shortcut rebound");e.key.scancode=SDL_SCANCODE_C;e.key.key=SDLK_C;input.event(e);
        require(input.profile().bindings[6].index==SDL_SCANCODE_C && !input.waiting(),"keyboard capture");
        input.begin(7);e.type=SDL_EVENT_QUIT;require(!input.event(e) && input.waiting(),"capture swallowed window quit");e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_ESCAPE;e.key.key=SDLK_ESCAPE;input.event(e);require(!input.waiting(),"capture cancel");
    }
    {
        NativeInput restored(path);pump(restored);require(restored.profile().bindings[6].index==SDL_SCANCODE_C,"keyboard persistence");
        restored.cycle(2);require(restored.profile().full_throttle && restored.profile().deadzone==20 && restored.profile().sensitivity==110 && restored.profile().bindings[6].index==3,"device profile persistence");
        restored.begin(0);require(SDL_DetachVirtualJoystick(raw),"detach raw");pump(restored);require(restored.connected().size()==1 && !restored.waiting(),"unplug during capture");
        raw=attach(false);pump(restored);restored.cycle(2);require(restored.profile().bindings[6].index==3,"GUID reconnect profile");
        require(SDL_DetachVirtualJoystick(pad),"detach pad");pump(restored);require(restored.connected().size()==1 && restored.read().cannon==false,"unplug held state");
    }
    {
        auto digital=attach(true,true);auto* stick=SDL_OpenJoystick(digital);require(stick,"digital pad open");
        NativeInput input(path);pump(input);input.read();
        SDL_SetJoystickVirtualButton(stick,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true);
        SDL_SetJoystickVirtualButton(stick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);pump(input);
        require(input.read().stick_x==1 && input.read().throttle==1,"D-pad/shoulder fallback defaults");
        SDL_CloseJoystick(stick);SDL_DetachVirtualJoystick(digital);
    }
    {std::ofstream bad(path);bad<<"AB2INPUT3\nkeyboard 15 100 0 k 999999 1\n";}
    {NativeInput malformed(path);require(malformed.profile().bindings[6].index==SDL_SCANCODE_Z,"malformed file fallback");}
    SDL_CloseJoystick(j);SDL_CloseJoystick(p);SDL_DetachVirtualJoystick(raw);SDL_Quit();std::filesystem::remove(path);
    std::cout<<"PASS SDL3 gamepad/joystick hotplug, bindings, axes, hats, focus, capture, persistence and malformed config\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}

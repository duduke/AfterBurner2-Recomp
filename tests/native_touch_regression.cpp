#include "frontend/native_touch.hpp"
#include "frontend/native_rom_picker.hpp"
#include <iostream>
#include <stdexcept>
#include <filesystem>

void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try {
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    Frontend::NativeTouch touch;touch.enabled=true;
    auto finger=[&](SDL_EventType type,SDL_FingerID id,float x,float y,SDL_TouchID device=1) {
        SDL_Event e{};e.type=type;e.tfinger.fingerID=id;e.tfinger.touchID=device;e.tfinger.x=x;e.tfinger.y=y;
        return touch.event(e,2.f);
    };
    using Touch=Frontend::NativeTouch;
    const auto targets=Touch::layout(2);
    auto down=[&](int id,Touch::Control c,SDL_TouchID device=1){return finger(SDL_EVENT_FINGER_DOWN,id,targets[c].x,targets[c].y,device);};
    down(1,Touch::stick);
    finger(SDL_EVENT_FINGER_MOTION,1,targets[Touch::stick].x+Touch::stick_travel/2,targets[Touch::stick].y-Touch::stick_travel);
    auto state=touch.read(2);require(state.stick_x>.70f && state.stick_y>.70f,"stick radial normalization/sign");
    down(2,Touch::cannon);down(3,Touch::missile);down(4,Touch::throttle);
    require(touch.read(2).throttle==0,"slider not centered on initial touch");
    finger(SDL_EVENT_FINGER_MOTION,4,targets[Touch::throttle].x,targets[Touch::throttle].y-Touch::throttle_half);
    state=touch.read(2);require(state.cannon && state.missile && state.throttle>.999f,"simultaneous flight/weapons/throttle");
    down(5,Touch::stick);finger(SDL_EVENT_FINGER_MOTION,5,.01f,.9f);
    require(touch.read(2).stick_x>.70f,"second stick finger stole ownership");
    finger(SDL_EVENT_FINGER_MOTION,2,targets[Touch::throttle].x,targets[Touch::throttle].y);
    require(touch.read(2).cannon && touch.read(2).throttle>.999f,"button motion changed ownership");
    down(6,Touch::throttle);finger(SDL_EVENT_FINGER_MOTION,6,targets[Touch::throttle].x,1.f);
    require(touch.read(2).throttle>.999f,"second slider finger stole ownership");
    finger(SDL_EVENT_FINGER_MOTION,4,targets[Touch::throttle].x,targets[Touch::throttle].y+Touch::throttle_half*.5f);
    require(touch.read(2).throttle<-.4f && touch.read(2).throttle>-.6f,"slider lost analog intermediate values");
    finger(SDL_EVENT_FINGER_MOTION,4,.5f,1.f);
    require(touch.read(2).throttle==-1,"slider motion not clamped/retained");
    finger(SDL_EVENT_FINGER_UP,4,0,0);require(touch.read(2).throttle==0,"released slider did not spring to center");
    down(4,Touch::throttle);finger(SDL_EVENT_FINGER_MOTION,4,targets[Touch::throttle].x,0);
    finger(SDL_EVENT_FINGER_CANCELED,4,0,0);require(touch.read(2).throttle==0,"canceled slider leaves stuck throttle");
    down(2,Touch::cannon,2);finger(SDL_EVENT_FINGER_UP,2,0,0);
    require(touch.read(2).cannon,"touch device IDs conflated");
    require(down(7,Touch::menu),"menu edge missing");
    require(!finger(SDL_EVENT_FINGER_MOTION,7,targets[Touch::menu].x,targets[Touch::menu].y),"menu repeats on motion");
    down(4,Touch::throttle);finger(SDL_EVENT_FINGER_MOTION,4,targets[Touch::throttle].x,0);
    SDL_Event loss{};loss.type=SDL_EVENT_WILL_ENTER_BACKGROUND;touch.event(loss,2);
    state=touch.read(2);require(!state.cannon && !state.missile && !state.stick_x && !state.throttle,"background leaves held controls");
    down(8,Touch::coin);down(9,Touch::start);
    require(touch.read(2).coin && touch.read(2).start,"coin/start missing");
    touch.enabled=false;require(!touch.read(2).coin,"TV/disabled adapter emits input");touch.enabled=true;touch.clear();
    GameControls pad;pad.cannon=true;pad.stick_x=-.5f;pad.throttle=.6f;
    touch.merge(pad,2);require(pad.cannon && pad.stick_x==-.5f && pad.throttle==.6f,"idle touch erases controller");
    down(1,Touch::throttle);touch.merge(pad,2);require(pad.throttle==0,"held slider center does not override controller");touch.clear();
    down(1,Touch::cannon);down(2,Touch::throttle);
    finger(SDL_EVENT_FINGER_MOTION,2,targets[Touch::throttle].x,0);
    require(!down(3,Touch::visibility) && !touch.visible(),"hide toggle missing or opens menu");
    state=touch.read(2);require(!state.cannon && state.throttle==0,"hiding retains held input");
    finger(SDL_EVENT_FINGER_MOTION,3,targets[Touch::visibility].x,targets[Touch::visibility].y);
    require(!touch.visible(),"visibility repeats on motion");
    down(4,Touch::cannon);down(5,Touch::stick);down(6,Touch::throttle);down(7,Touch::coin);down(8,Touch::start);
    require(!down(9,Touch::menu),"hidden menu button still active");
    state=touch.read(2);require(!state.cannon && !state.coin && !state.start && !state.stick_x && !state.throttle,"hidden controls emit input");
    pad={};pad.stick_x=.5f;pad.throttle=.8f;touch.merge(pad,2);
    require(pad.stick_x==.5f && pad.throttle==.8f,"hidden touch overrides controller");
    touch.event(loss,2);require(!touch.visible(),"background resets visibility choice");
    down(10,Touch::visibility);require(touch.visible(),"show toggle unavailable");
    require(!touch.read(2).cannon && !touch.read(2).throttle,"show revives old fingers");
    finger(SDL_EVENT_FINGER_MOTION,10,targets[Touch::cannon].x,targets[Touch::cannon].y);
    require(!touch.read(2).cannon,"show finger drag presses a weapon");
    down(11,Touch::cannon);require(touch.read(2).cannon,"shown controls remain disabled");touch.clear();
    for(float aspect:{4.f/3,16.f/9,16.f/10,21.f/9}) {
        const auto t=Touch::layout(aspect);
        for(int c=0;c<Touch::none;++c) {
            require(Touch::hit(t[c].x,t[c].y,aspect)==c,"layout center hit mismatch");
            require(t[c].x-t[c].radius/aspect>=0 && t[c].x+t[c].radius/aspect<=1,"control extends outside screen");
            if(c!=Touch::stick && c!=Touch::throttle)
                require(Touch::hit(t[c].x+t[c].radius*.9f/aspect,t[c].y+t[c].radius*.9f,aspect)!=c,"round button accepts square corner");
        }
        require(t[Touch::cannon].y<t[Touch::stick].y && t[Touch::missile].y==t[Touch::cannon].y,"weapons not above joystick");
        require(t[Touch::coin].x<t[Touch::start].x && t[Touch::start].x<t[Touch::menu].x && t[Touch::start].x==.5f,"top controls not centered");
    }
    auto* surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);
    auto* renderer=SDL_CreateSoftwareRenderer(surface);require(renderer,"renderer");
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    SDL_SetRenderLogicalPresentation(renderer,320,224,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Rect viewport{12,0,296,224};SDL_SetRenderViewport(renderer,&viewport);
    SDL_SetRenderDrawColor(renderer,1,2,3,4);touch.draw(renderer);
    SDL_Rect after;SDL_GetRenderViewport(renderer,&after);Uint8 r,g,b,a;SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);
    require(after.x==12 && after.w==296 && r==1 && a==4,"overlay corrupts renderer state");
    if(const char* preview=std::getenv("AB2_TOUCH_PREVIEW")){SDL_RenderPresent(renderer);require(SDL_SaveBMP(surface,preview),"overlay preview save");}
    auto* pixels=SDL_RenderReadPixels(renderer,nullptr);require(pixels,"overlay readback");
    bool visible=false;for(int y=0;y<pixels->h;++y)for(int x=0;x<pixels->w;++x){Uint8 rr,gg,bb,aa;SDL_ReadSurfacePixel(pixels,x,y,&rr,&gg,&bb,&aa);visible|=bb>40;}
    require(visible,"touch overlay draws no controls");SDL_DestroySurface(pixels);
    SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);SDL_SetRenderViewport(renderer,nullptr);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    down(12,Touch::visibility);touch.draw(renderer);SDL_RenderPresent(renderer);
    auto lit=[&](Touch::Control c){Uint8 rr,gg,bb,aa;const auto t=Touch::layout(1280.f/720)[c];SDL_ReadSurfacePixel(surface,int(t.x*1280),int(t.y*720),&rr,&gg,&bb,&aa);return rr || gg || bb;};
    require(lit(Touch::visibility),"hidden overlay has no show button");
    for(int c=0;c<Touch::visibility;++c)require(!lit(static_cast<Touch::Control>(c)),"hidden overlay still draws controls");
    down(13,Touch::visibility);touch.draw(renderer);SDL_RenderPresent(renderer);
    require(lit(Touch::stick) && lit(Touch::cannon),"show does not redraw controls");
    SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    auto dir=std::filesystem::temp_directory_path()/"ab2-touch-import-test";std::filesystem::create_directories(dir);
    auto source=dir/"input.zip",destination=dir/"import.zip";
    {std::ofstream file(source,std::ios::binary);file<<"synthetic ZIP input";}
    auto imported=Frontend::import_rom_zip(source.string(),destination.string());
    require(imported.error.empty() && std::filesystem::file_size(destination)==19,"private ROM copy");
    {std::ofstream file(source,std::ios::trunc);}
    imported=Frontend::import_rom_zip(source.string(),destination.string());
    require(!imported.error.empty() && std::filesystem::file_size(destination)==19 && !std::filesystem::exists(destination.string()+".tmp"),"failed import damaged previous file");
    std::filesystem::resize_file(source,64*1024*1024+1);
    imported=Frontend::import_rom_zip(source.string(),destination.string());
    require(!imported.error.empty() && std::filesystem::file_size(destination)==19,"oversize provider input accepted");
    imported=Frontend::import_rom_zip((dir/"missing.zip").string(),destination.string());
    require(!imported.error.empty(),"missing provider input accepted");
    std::filesystem::remove_all(dir);SDL_Quit();
    std::cout<<"Android adapter: multi-touch, ownership, lifecycle, controller merge, overlay state/readback and private import passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

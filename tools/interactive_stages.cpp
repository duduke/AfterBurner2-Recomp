// Native flight-school prototype with optional local ROM art and an isolated
// native landing/departure session. ROM-free authored fallback remains usable.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "frontend/interactive_stages.hpp"
#include "frontend/native_input.hpp"
#include "frontend/controller_database.hpp"
#include "frontend/interactive_stage_sprites.hpp"
#include "engine/interactive_sequence_player.hpp"
#include <charconv>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

using Stage = Frontend::InteractiveStage;
namespace {
const char* stage_name(Stage::Kind kind){return kind==Stage::Kind::refuel?"refuel":kind==Stage::Kind::landing?"landing":kind==Stage::Kind::runway?"runway":"runway-jungle";}
struct View {
    SDL_Renderer* r;
    Frontend::InteractiveStageSprites* sprites{};
    SDL_Texture* native{};
    SDL_Texture* scenery{};
    SDL_Texture* next_scenery{};
    SDL_Texture* aircraft{};
    const InteractiveSceneCapture::Shot* approach{};
    const InteractiveSceneCapture::Shot* next_approach{};
    float scenery_blend{};
    float native_fade{};
    Uint64 draw_clock{};
    void color(SDL_Color c) { SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a); }
    static constexpr SDL_Color white{220,237,245,255}, green{95,238,172,255},
        muted{109,151,173,255}, amber{255,194,96,255}, dark{10,23,40,255};
    void box(float x,float y,float w,float h,SDL_Color c,bool fill=true) {
        color(c);SDL_FRect b{x,y,w,h};if(fill)SDL_RenderFillRect(r,&b);else SDL_RenderRect(r,&b);
    }
    void line(float x,float y,float xx,float yy,SDL_Color c) {color(c);SDL_RenderLine(r,x,y,xx,yy);}
    void text(float x,float y,const std::string& s,SDL_Color c=white,float scale=1.5f) {
        color(c);SDL_SetRenderScale(r,scale,scale);SDL_RenderDebugText(r,x/scale,y/scale,s.c_str());SDL_SetRenderScale(r,1,1);
    }
    void poly(std::initializer_list<SDL_FPoint> p,SDL_Color c) {
        color(c);std::vector<SDL_Vertex> vertices;
        for(auto point:p)vertices.push_back({point,{c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f},{0,0}});
        std::vector<int> indices;for(int i=1;i+1<int(vertices.size());++i){indices.push_back(0);indices.push_back(i);indices.push_back(i+1);}
        SDL_RenderGeometry(r,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
    }
    void circle(float x,float y,float radius,SDL_Color c) {
        for(int i=0;i<40;++i){const double a=i*6.283185307/40,b=(i+1)*6.283185307/40;
            line(x+radius*std::cos(a),y+radius*std::sin(a),x+radius*std::cos(b),y+radius*std::sin(b),c);}
    }
    void plane(float x,float y,float size,float bank,SDL_Color c) {
        // Stylized rear view; rotate every vertex together around the fuselage.
        auto point=[&](float px,float py){const float a=bank*.6f;return SDL_FPoint{x+size*(px*std::cos(a)-py*std::sin(a)),y+size*(px*std::sin(a)+py*std::cos(a))};};
        const auto a=point(-1,0),b=point(-.15f,-.14f),d=point(0,-.52f),e=point(.15f,-.14f),f=point(1,0),g=point(.13f,.13f),h=point(-.13f,.13f);
        poly({a,b,d,e,f,g,h},c);
        const auto l=point(-.09f,.08f),rr=point(.09f,.08f);
        circle(l.x,l.y,size*.06f,amber);circle(rr.x,rr.y,size*.06f,amber);
    }
    void draw(const Stage& s,bool paused) {
        const auto now=SDL_GetTicksNS();
        const double draw_dt=draw_clock?std::min((now-draw_clock)/1.e9,.05):0;draw_clock=now;
        color({14,32,55,255});SDL_RenderClear(r);
        const float horizon=235+float(s.pitch)*28;
        const bool runway=s.type==Stage::Kind::runway || s.type==Stage::Kind::jungle;
        if(scenery){
            SDL_FRect source{0,28,320,160},image{88,78,784,392};SDL_RenderTexture(r,scenery,&source,&image);
            if(next_scenery && scenery_blend>0){SDL_SetTextureAlphaMod(next_scenery,Uint8(scenery_blend*255));SDL_RenderTexture(r,next_scenery,&source,&image);}
        }else{
        box(0,horizon,960,600-horizon,runway?SDL_Color{62,78,44,255}:SDL_Color{11,48,67,255});
        line(0,horizon,960,horizon,{68,116,138,255});
        // A restrained horizon grid gives closure and lateral-motion cues.
        for(int i=0;i<10;++i){float y=horizon+std::pow((i+std::fmod(s.elapsed*.65,1.))/10.,2)*300;
            line(0,y,960,y,{21,65,82,255});}
        for(int i=-5;i<=5;++i)line(480-float(s.x)*2,horizon,480+i*180-float(s.x)*10,550,{21,65,82,255});
        }
        if(s.type==Stage::Kind::refuel) {
            float scale=std::clamp(100.f/(float(s.range)+65),.5f,1.6f);
            float tx=480+float(s.target_x()-s.x)*8*scale;
            float ty=285-float(s.target_height()-s.height)*6*scale;
            if(sprites)sprites->tanker(tx,ty-60*scale,scale);
            else plane(tx,ty-60*scale,110*scale,0,{136,168,182,255});
            line(tx,ty-50*scale,tx+12*scale,ty-20*scale,white);
            line(tx+12*scale,ty-20*scale,tx,ty,white);
            circle(tx,ty,13*scale,s.phase==Stage::Phase::contact?green:amber);
            circle(480,285,7,green);line(457,285,467,285,green);line(493,285,503,285,green);
            if(!scenery){text(30,103,"01 / TANKER RENDEZVOUS",muted);
                text(30,128,"ALIGN THE BASKET WITH THE GREEN RETICLE",white,1);}
        } else {
            float scale=std::clamp(240.f/(float(s.range)+350),.2f,1.8f);
            float cx=480-float(s.x)*6*scale;
            float top=horizon+float(s.height)*.8f*scale;
            if(scenery){}
            else if(runway){
                poly({{cx-24*scale,horizon},{cx+24*scale,horizon},{cx+300*scale,470},{cx-300*scale,470}},{78,82,84,255});
                for(int i=0;i<7;++i)box(cx-3,horizon+20+i*27,6,14,white);
                line(cx-250*scale,450,cx+250*scale,450,white);
            }
            else if(sprites)sprites->ship(cx,top+100*scale,360*scale);
            else {
            poly({{cx-42*scale,top},{cx+42*scale,top},{cx+145*scale,top+230*scale},{cx-145*scale,top+230*scale}},{53,74,88,255});
            line(cx-42*scale,top,cx-145*scale,top+230*scale,muted);
            line(cx+42*scale,top,cx+145*scale,top+230*scale,muted);
            for(int i=0;i<7;++i)box(cx-2,top+i*32*scale,4,14*scale,white);
            for(int i=0;i<4;++i)line(cx-80*scale,top+(140+i*16)*scale,cx+80*scale,top+(140+i*16)*scale,amber);
            poly({{cx+45*scale,top+25*scale},{cx+70*scale,top+15*scale},{cx+80*scale,top+83*scale},{cx+50*scale,top+85*scale}},{100,128,141,255});
            }
            line(460,290,500,290,green);line(480,280,480,300,green);
            if(!scenery){text(30,103,runway?(s.type==Stage::Kind::jungle?"04 / JUNGLE RUNWAY":"03 / AUTUMN RUNWAY"):"02 / CARRIER RECOVERY",muted);
                text(30,128,"FOLLOW THE GLIDESLOPE / FLARE ABOVE THE DECK",white,1);}
        }
        if(scenery && aircraft && approach && s.type!=Stage::Kind::refuel){
            // Native pose, zoom and palette from this same approach frontier.
            // Authored control offsets converge on the native touchdown pose.
            const float dx=float(s.x)*.6f,dy=-float(s.height-s.target_height())*.2f;
            const auto& next=next_approach?*next_approach:*approach;
            auto blend=[&](int a,int b){return float(a)+(b-a)*scenery_blend;};
            SDL_FRect source{float(approach->left),float(approach->top),float(approach->right-approach->left+1),float(approach->bottom-approach->top+1)};
            SDL_FRect image{88+(blend(approach->left,next.left)+dx)*2.45f,78+(blend(approach->top,next.top)-28+dy)*2.45f,
                (blend(approach->right,next.right)-blend(approach->left,next.left)+1)*2.45f,(blend(approach->bottom,next.bottom)-blend(approach->top,next.top)+1)*2.45f};
            SDL_Rect previous_clip;const bool clipped=SDL_RenderClipEnabled(r);SDL_GetRenderClipRect(r,&previous_clip);
            SDL_Rect scene_clip{88,78,784,392};SDL_SetRenderClipRect(r,&scene_clip);
            SDL_RenderTextureRotated(r,aircraft,&source,&image,s.bank*12,nullptr,SDL_FLIP_NONE);
            SDL_SetRenderClipRect(r,clipped?&previous_clip:nullptr);
        }
        else if(sprites)sprites->player(480+float(s.x)*1.6f,416-float(s.pitch)*10,float(s.bank));
        else plane(480+float(s.x)*1.6f,416-float(s.pitch)*10,66,float(s.bank),{211,225,226,255});
        if(native && (s.continuing() || s.phase==Stage::Phase::success)){
            native_fade=std::min(1.f,native_fade+float(draw_dt/.4));
            if(!scenery)box(0,78,960,392,{10,23,40,Uint8(255*native_fade)});
            SDL_SetTextureAlphaMod(native,Uint8(255*native_fade));
            // The standalone HUD owns the arranged missile count/prompt.
            // Crop native HUD bands while preserving scene pixel aspect.
            SDL_FRect source{0,28,320,160},image{88,78,784,392};SDL_RenderTexture(r,native,&source,&image);
        }else native_fade=0;
        box(0,0,960,78,dark);text(28,21,"AFTER BURNER / FLIGHT SCHOOL",white,2);
        if(scenery){const std::string label=s.type==Stage::Kind::refuel?"TANKER RENDEZVOUS":s.type==Stage::Kind::landing?"CARRIER RECOVERY":s.type==Stage::Kind::runway?"AUTUMN RUNWAY":"JUNGLE RUNWAY";
            text(28,53,label+"   /   ORIGINAL ROM SCENE",muted,1);
        }else text(28,53,sprites?"INTERACTIVE STAGES   /   ORIGINAL ROM SPRITES":"INTERACTIVE STAGES   /   STANDALONE PROTOTYPE",muted,1);
        text(742,28,paused?"PAUSED":"EXPERIMENTAL",paused?amber:green);
        box(0,470,960,130,dark);
        auto metric=[&](float x,const char* label,double value,const char* unit){
            text(x,488,label,muted,1);char buf[64];SDL_snprintf(buf,sizeof(buf),"%5.1f %s",value,unit);text(x,508,buf,white,1.5f);};
        const bool landed_display=s.continuing() || (s.type!=Stage::Kind::refuel && s.phase==Stage::Phase::success);
        if(landed_display){
            text(28,488,"LANDING CONTINUATION",muted,1);
            text(28,508,s.native_continuation?"ORIGINAL BOARD ANIMATION":"ROM-FREE PRACTICE ANIMATION",white,1);
            text(384,488,"TAKEOFF CONTROL",muted,1);
            text(384,508,"SPACE / RIGHT TRIGGER: FULL POWER",green,1);
        }else{
            metric(28,"SPEED",s.speed,"U/S");metric(206,"RANGE",std::max(0.,s.range),"U");
            metric(384,s.type==Stage::Kind::refuel?"VERTICAL ERROR":"HEIGHT",s.type==Stage::Kind::refuel?s.height-s.target_height():s.height,"U");
            metric(562,"LATERAL ERROR",s.x-s.target_x(),"U");
        }
        if(landed_display){
            char ammo[48];SDL_snprintf(ammo,sizeof(ammo),"MISSILES %u / %u",s.missiles,s.missile_maximum);
            text(746,488,"REARMING STATUS",muted,1);text(746,508,ammo,green,1);
        }else if(s.type==Stage::Kind::refuel) {
            text(746,488,"TRANSFER / HOLD 4 SEC",muted,1);box(746,509,180,12,{36,57,72,255});box(746,509,float(s.fuel)*180,12,green);
        } else metric(746,"DESCENT",s.sink,"U/S");
        std::string guidance;
        if(landed_display)guidance=s.phase==Stage::Phase::success?"TAKEOFF COMPLETE / CLEAR TO RESUME":s.phase==Stage::Phase::rollout?"TOUCHDOWN / ROLLING TO REARM":s.phase==Stage::Phase::rearming?"MISSILES RELOADING / STAND BY":s.phase==Stage::Phase::ready?"RELOAD COMPLETE / FULL THROTTLE TO TAKE OFF":"FULL POWER / TAKEOFF IN PROGRESS";
        else if(s.type==Stage::Kind::refuel)guidance=s.phase==Stage::Phase::contact?"CONTACT / HOLD STEADY":s.range>18?"CLOSE WITH BOOST / RELEASE BEFORE CONTACT":!s.aligned()?"MATCH HEIGHT, BANK AND SPEED":"HOLD RANGE BETWEEN 3 AND 9";
        else guidance=s.height<12?"FLARE: HOLD BACK / KEEP WINGS LEVEL":s.height>s.target_height()+4?"HIGH / LOWER THE NOSE":s.height<s.target_height()-4?"LOW / PULL UP":"ON GLIDESLOPE / KEEP LINED UP";
        text(28,550,guidance,green,1.5f);
        text(28,580,"DOWN: NOSE UP / UP: NOSE DOWN   SPACE/SHIFT: THROTTLE   P: PAUSE   R: RETRY   TAB: SWITCH   ESC: EXIT",muted,1);
        if(s.phase==Stage::Phase::briefing || s.finished()) {
            box(160,170,640,205,{7,20,34,245});box(160,170,640,205,muted,false);
            text(190,195,s.phase==Stage::Phase::briefing?"YOUR AIRCRAFT. YOUR APPROACH.":s.phase==Stage::Phase::success?"CHECK COMPLETE":"TRY ANOTHER APPROACH",s.phase==Stage::Phase::success?green:amber,2);
            if(s.phase==Stage::Phase::briefing){
                text(190,238,s.type==Stage::Kind::refuel?"Close to 3-9 units. Release boost to match speed.":"Line up with the centerline. Neutral pitch descends.",white,1);
                text(190,260,s.type==Stage::Kind::refuel?"Match the basket height and hold contact for 4 seconds.":"Below 12 units, hold back to flare. Aim 60-120 U/S.",white,1);
                text(190,282,"Pull back / Down: nose up. Push forward / Up: nose down.",muted,1);
            }else text(190,247,std::string(s.message),white,1);
            text(190,330,s.finished()?"ENTER / START: RETRY    TAB / Y: OTHER CHALLENGE":"ENTER / START: BEGIN    TAB / Y: OTHER CHALLENGE",green,1);
        }
        if(paused) {box(350,395,260,38,dark);text(370,406,"P / START TO RESUME",amber,1);}
    }
};

GameControls demo_input(const Stage& s) {
    GameControls c;
    if(s.continuing()){if(s.phase==Stage::Phase::ready || s.phase==Stage::Phase::takeoff)c.throttle=1;return c;}
    c.stick_x=std::clamp(float((s.target_x()-s.x)*.22-s.bank*.8),-1.f,1.f);
    if(s.type==Stage::Kind::refuel) {
        c.stick_y=std::clamp(float((s.target_height()-s.height)*.3-s.pitch*.8),-1.f,1.f);
        c.throttle=std::clamp(float((s.range-6)*.06-(s.speed-140)*.04),-1.f,1.f);
    }else{
        const double wanted_sink=s.height<5.5?1.5:std::clamp((s.height-s.target_height())*.9+5.4,1.,10.);
        c.stick_y=std::clamp(float((6-wanted_sink)/7),-1.f,1.f);
    }
    return c;
}
}

int main(int argc,char** argv) try {
    Stage::Kind kind=Stage::Kind::refuel;unsigned frames=0;bool demo=false,demo_hold=false,no_sprites=false;std::string screenshot,rom_source;
    for(int i=1;i<argc;++i){std::string_view option=argv[i];
        if(option=="--demo"){demo=true;continue;}
        if(option=="--demo-hold"){demo=true;demo_hold=true;continue;}
        if(option=="--no-sprites"){no_sprites=true;continue;}
        if((option=="--stage" || option=="--frames" || option=="--screenshot" || option=="--roms") && i+1<argc){
            std::string_view value=argv[++i];
            if(option=="--stage") {
                if(value=="refuel")kind=Stage::Kind::refuel;
                else if(value=="landing")kind=Stage::Kind::landing;
                else if(value=="runway")kind=Stage::Kind::runway;
                else if(value=="runway-jungle")kind=Stage::Kind::jungle;
                else throw std::runtime_error("Stage must be refuel, landing, runway or runway-jungle");}
            else if(option=="--screenshot")screenshot=value;
            else if(option=="--roms")rom_source=value;
            else {const auto result=std::from_chars(value.data(),value.data()+value.size(),frames);if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || !frames)throw std::runtime_error("Frames must be positive");}
        }else throw std::runtime_error("Usage: interactive-stages [--stage refuel|landing|runway|runway-jungle] [--roms directory|set.zip | --no-sprites] [--frames N] [--demo] [--screenshot image.bmp]");
    }
    if(!screenshot.empty() && !frames)throw std::runtime_error("Screenshot requires --frames");
    if(no_sprites && !rom_source.empty())throw std::runtime_error("Choose ROM sprites or --no-sprites");
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK))throw std::runtime_error(SDL_GetError());
    struct Quit {~Quit(){SDL_Quit();}} quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("After Burner / Interactive stages prototype",1152,720,SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
    if(!window)throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),nullptr),SDL_DestroyRenderer);
    if(!renderer)throw std::runtime_error(SDL_GetError());
    SDL_SetRenderLogicalPresentation(renderer.get(),960,600,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(renderer.get(),SDL_BLENDMODE_BLEND);
    Frontend::load_controller_database();
    std::unique_ptr<Roms> rom_assets;
    std::unique_ptr<Frontend::InteractiveStageSprites> sprites;
    if(!no_sprites && (!rom_source.empty() || std::filesystem::exists("roms/epr-11107.58") || std::filesystem::exists("roms/aburner2.zip"))){
        rom_assets=std::make_unique<Roms>();RomLoader::set_source(rom_source);
        if(!rom_assets->load_aburner2())throw std::runtime_error("Unable to load practice ROM sprites; use --roms or --no-sprites");
        sprites=std::make_unique<Frontend::InteractiveStageSprites>(renderer.get(),*rom_assets);
    }
    std::cout<<"Practice artwork: "<<(sprites?"original ROM sprites":"geometric fallback")<<'\n';
    Frontend::NativeInput input("");Stage stage(kind);View view{renderer.get(),sprites.get()};
    std::unique_ptr<InteractiveSequencePlayer> sequence;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> native_image(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224),SDL_DestroyTexture);
    if(!native_image)throw std::runtime_error(SDL_GetError());
    SDL_SetTextureScaleMode(native_image.get(),SDL_SCALEMODE_NEAREST);SDL_SetTextureBlendMode(native_image.get(),SDL_BLENDMODE_BLEND);
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> scenery_image(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224),SDL_DestroyTexture);
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> next_scenery_image(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224),SDL_DestroyTexture);
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> aircraft_image(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224),SDL_DestroyTexture);
    if(!scenery_image || !next_scenery_image || !aircraft_image)throw std::runtime_error(SDL_GetError());
    SDL_SetTextureScaleMode(scenery_image.get(),SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(next_scenery_image.get(),SDL_SCALEMODE_NEAREST);SDL_SetTextureBlendMode(next_scenery_image.get(),SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(aircraft_image.get(),SDL_SCALEMODE_NEAREST);SDL_SetTextureBlendMode(aircraft_image.get(),SDL_BLENDMODE_BLEND);
    double native_debt=0;
    uint64_t uploaded_native=UINT64_MAX,uploaded_approach=UINT64_MAX,uploaded_next=UINT64_MAX;
    bool running=true,paused=false;unsigned count=0;
    if(demo)stage.start();
    double previous=SDL_GetTicksNS()/1.e9,debt=0;
    while(running){SDL_Event e;
        while(SDL_PollEvent(&e)){
            input.event(e);
            if(e.type==SDL_EVENT_QUIT)running=false;
            if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST && !frames){paused=true;input.block();}
            SDL_Keycode key=0;
            if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat)key=e.key.key;
            if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){
                if(e.gbutton.button==SDL_GAMEPAD_BUTTON_START)key=stage.active() && !paused?SDLK_P:SDLK_RETURN;
                if(e.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH)key=SDLK_RETURN;
                if(e.gbutton.button==SDL_GAMEPAD_BUTTON_NORTH)key=SDLK_TAB;
                if(e.gbutton.button==SDL_GAMEPAD_BUTTON_BACK)key=SDLK_ESCAPE;
            }
            if(key==SDLK_ESCAPE)running=false;
            if(key==SDLK_TAB){stage.reset(static_cast<Stage::Kind>((int(stage.type)+1)%4));sequence.reset();native_debt=0;paused=false;input.block();}
            if(key==SDLK_R || key==SDLK_F5){stage.reset(stage.type);sequence.reset();native_debt=0;paused=false;input.block();}
            if(key==SDLK_P){paused=!paused;input.block();}
            if(key==SDLK_RETURN){if(paused)paused=false;else if(stage.finished()){stage.reset(stage.type);sequence.reset();native_debt=0;}else stage.start();input.block();}
        }
        if(!running)break;
        if(rom_assets && stage.active() && !sequence){
            view.scenery=nullptr;view.approach=nullptr;view.aircraft=nullptr;
            view.draw(stage,true);view.text(270,350,"PREPARING ORIGINAL ROM SCENERY",View::amber,1);SDL_RenderPresent(renderer.get());
            auto surface=stage.type==Stage::Kind::refuel?InteractiveSequencePlayer::Surface::refuel:stage.type==Stage::Kind::landing?InteractiveSequencePlayer::Surface::carrier:stage.type==Stage::Kind::runway?InteractiveSequencePlayer::Surface::runway:InteractiveSequencePlayer::Surface::jungle;
            sequence=std::make_unique<InteractiveSequencePlayer>(*rom_assets,surface,[]{SDL_PumpEvents();});
            uploaded_native=uploaded_approach=uploaded_next=UINT64_MAX;
            stage.native_continuation=stage.type!=Stage::Kind::refuel;stage.missiles=sequence->missile_count();stage.missile_maximum=sequence->missile_maximum();
            previous=SDL_GetTicksNS()/1.e9;debt=0;
        }
        const double now=SDL_GetTicksNS()/1.e9;
        // No catch-up after pause, focus loss or a long debugger/window stall.
        if(frames)debt=Stage::dt;
        else if(!paused)debt=std::min(debt+std::min(now-previous,.1),.1);
        else debt=0;
        previous=now;
        const auto controls=Frontend::interactive_stage_controls(input.read(paused));
        while(debt+1.e-10>=Stage::dt){if(!paused){
            const auto command=demo && !(demo_hold && stage.phase==Stage::Phase::ready)?demo_input(stage):controls;stage.tick(command);
            if(sequence && stage.continuing()){
                native_debt+=Stage::dt;
                while(native_debt>=XBoardTimeline::FRAME_NS/1.e9){sequence->advance(command);native_debt-=XBoardTimeline::FRAME_NS/1.e9;}
                using Native=InteractiveSequencePlayer::Phase;
                stage.phase=sequence->phase()==Native::rollout?Stage::Phase::rollout:sequence->phase()==Native::rearming?Stage::Phase::rearming:sequence->phase()==Native::ready?Stage::Phase::ready:sequence->phase()==Native::takeoff?Stage::Phase::takeoff:Stage::Phase::success;
                stage.missiles=sequence->missile_count();
                if(stage.phase==Stage::Phase::success)stage.message="TAKEOFF COMPLETE - CLEAR TO RESUME";
            }
        }debt-=Stage::dt;}
        view.native=nullptr;
        view.scenery=nullptr;view.next_scenery=nullptr;view.aircraft=nullptr;view.approach=nullptr;view.next_approach=nullptr;
        if(sequence && !sequence->approach().empty()){
            const auto& shots=sequence->approach();
            const double progress=stage.continuing() || stage.finished()?1:std::clamp(1-std::max(0.,stage.range)/(stage.type==Stage::Kind::refuel?100.:900.),0.,1.);
            const double position=progress*(shots.size()-1);const auto index=size_t(position);
            const auto& shot=shots[index];const auto& next=shots[std::min(index+1,shots.size()-1)];
            if(uploaded_approach!=shot.frame){
                SDL_UpdateTexture(scenery_image.get(),nullptr,shot.scenery.data(),320*4);
                SDL_UpdateTexture(aircraft_image.get(),nullptr,shot.aircraft.data(),320*4);uploaded_approach=shot.frame;
            }
            if(uploaded_next!=next.frame){SDL_UpdateTexture(next_scenery_image.get(),nullptr,next.scenery.data(),320*4);uploaded_next=next.frame;}
            view.scenery=scenery_image.get();view.aircraft=aircraft_image.get();view.approach=&shot;
            view.next_scenery=next_scenery_image.get();view.next_approach=&next;view.scenery_blend=float(position-index);
        }
        if(sequence && stage.type!=Stage::Kind::refuel && (stage.continuing() || stage.phase==Stage::Phase::success)){
            if(uploaded_native!=sequence->presentation_frame()){
                std::vector<uint32_t> pixels;pixels.reserve(320*224);
                for(auto c:sequence->pixels())pixels.push_back(0xff000000u|(unsigned(c.r)<<16)|(unsigned(c.g)<<8)|c.b);
                SDL_UpdateTexture(native_image.get(),nullptr,pixels.data(),320*4);uploaded_native=sequence->presentation_frame();
            }
            view.native=native_image.get();
        }
        view.draw(stage,paused);
        if(frames && ++count>=frames){
            if(!screenshot.empty()){
                std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(SDL_RenderReadPixels(renderer.get(),nullptr),SDL_DestroySurface);
                if(!surface || !SDL_SaveBMP(surface.get(),screenshot.c_str()))throw std::runtime_error(SDL_GetError());
            }
            std::cout<<"Interactive stage: "<<stage_name(stage.type)<<", phase="<<int(stage.phase)<<", ticks="<<count<<", time="<<stage.elapsed<<", missiles="<<stage.missiles<<'/'<<stage.missile_maximum<<", burner_visible="<<(sequence && sequence->first_burner_frame() && sequence->presentation_frame()>=sequence->first_burner_frame())<<", queued="<<(sequence?sequence->buffered_frames():0)<<", approach_shots="<<(sequence?sequence->approach().size():0)<<", "<<stage.message<<"\n";
            running=false;
        }
        SDL_RenderPresent(renderer.get());
        if(!frames)SDL_Delay(4);
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

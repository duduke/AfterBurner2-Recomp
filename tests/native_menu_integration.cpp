#include "frontend/native_menu.hpp"
#include "engine/physical_recording.hpp"
#include "frontend/gpu_sprite_filter.hpp"
#include "frontend/native_crt.hpp"
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
std::vector<uint32_t> readback(SDL_Renderer* renderer,const char* capture=nullptr) {
    auto* source=SDL_RenderReadPixels(renderer,nullptr);require(source,"CRT readback failed");
    auto* image=SDL_ConvertSurface(source,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(source);require(image,"CRT readback conversion failed");
    std::vector<uint32_t> pixels(size_t(image->w)*image->h);
    for(int y=0;y<image->h;++y)std::memcpy(pixels.data()+size_t(y)*image->w,static_cast<uint8_t*>(image->pixels)+size_t(y)*image->pitch,size_t(image->w)*4);
    if(capture)require(SDL_SaveBMP(image,capture),"CRT readback capture failed");
    SDL_DestroySurface(image);return pixels;
}
using Menu=Frontend::NativeMenu;
void select(Menu& menu,const std::string& label){
    for(int i=0;i<menu.row_count();++i){if(menu.row_label(menu.selection())==label)return;menu.key(SDLK_DOWN);}
    throw std::runtime_error("Menu entry missing: "+label);
}
void enter(Menu& menu,const std::string& label){select(menu,label);menu.key(SDLK_RETURN);}
void navigate(Menu& menu,std::initializer_list<const char*> path){
    for(int i=0;menu.page()!=Menu::Page::root && i<10;++i)menu.key(SDLK_ESCAPE);
    require(menu.page()==Menu::Page::root,"menu could not return to root");
    for(auto label:path)enter(menu,label);
}
void practice(Menu& menu){navigate(menu,{"CHANGE MODE...","PRACTICE"});}
void shuffle(Menu& menu){navigate(menu,{"CHANGE MODE...","SHUFFLE"});}
int main(int argc,char** argv) try {
    const bool gpu=argc==2 && std::string_view(argv[1])=="--gpu";
    require(roms.load_aburner2(),"ROMs required");
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD),SDL_GetError());
    auto* window=SDL_CreateWindow("Menu integration",960,672,SDL_WINDOW_RESIZABLE);
    require(window,"window unavailable");
    auto* device=gpu?SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,true,nullptr):nullptr;
    require(!gpu || device,"GPU device unavailable");
    auto* renderer=gpu?SDL_CreateGPURenderer(device,window):SDL_CreateRenderer(window,"software");require(renderer,"renderer unavailable");
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224);
    require(texture,"texture unavailable");
    {
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,320,224);
        require(target && SDL_SetRenderTarget(renderer,target),"scrollbar render target unavailable");
        require(SDL_SetRenderLogicalPresentation(renderer,320,224,SDL_LOGICAL_PRESENTATION_DISABLED),"scrollbar native coordinates unavailable");
        Frontend::NativeInput input("");Frontend::NativeHaptics feedback(input,"");
        Frontend::NativeMusic music("","build/oracle/music-fixtures");
        GameSession game(roms);Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.input=&input;menu.feedback=&feedback;menu.music=&music;
        auto thumb=[&](const char* capture=nullptr){
            menu.draw();auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"scrollbar readback failed");
            auto* image=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(raw);require(image,"scrollbar conversion failed");
            require(image->w==320&&image->h==224,"scrollbar readback dimensions changed");
            std::array<int,2> range{-1,-1};
            for(int y=0;y<224;++y){auto* row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(image->pixels)+y*image->pitch);
                if((row[313]&0xffffff)==0x36b8ff){if(range[0]<0)range[0]=y;range[1]=y;}}
            if(capture){std::filesystem::create_directories("build/oracle");require(SDL_SaveBMP(image,capture),"scrollbar capture failed");}
            SDL_DestroySurface(image);return range;
        };
        menu.open(game);navigate(menu,{"SETTINGS"});
        require(thumb()[0]<0,"Settings overview should fit without scrolling");
        enter(menu,"DISPLAY");require(thumb()[0]<0,"short Display page displayed a scrollbar");
        navigate(menu,{"SETTINGS","GRAPHICS"});require(thumb()[0]<0,"short Graphics page displayed a scrollbar");
        navigate(menu,{"SETTINGS","CRT AND SHADERS"});require(thumb()[0]<0,"short CRT page displayed a scrollbar");
        navigate(menu,{"SETTINGS","CONTROLS","BUTTON BINDINGS"});
        auto controls_top=thumb("build/oracle/scrollbar-controls-top.bmp");
        for(int i=0;i<6;++i)menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);auto controls_middle=thumb();
        select(menu,"BACK");auto controls_bottom=thumb("build/oracle/scrollbar-controls-bottom.bmp");
        require(controls_top[0]==60 && controls_middle[0]>controls_top[0] && controls_bottom[0]>controls_middle[0] && controls_bottom[1]==178,"Bindings scrollbar position failed");
        navigate(menu,{"SETTINGS","CONTROLS","FEEDBACK"});require(thumb("build/oracle/scrollbar-feedback-top.bmp")[0]<0,"short Feedback page displayed a scrollbar");
        enter(menu,"TEST FEEDBACK");require(thumb("build/oracle/scrollbar-feedback-bottom.bmp")[0]<0,"short Feedback Tests page displayed a scrollbar");
        menu.close(game);require(!game.info().number,"scrollbar rendering advanced the board");
        require(SDL_SetRenderTarget(renderer,nullptr),"scrollbar target restore failed");SDL_DestroyTexture(target);
        std::cout<<"Menu scrollbars: Bindings top, middle and bottom readbacks passed; short category pages hide indicators.\n";
    }
    {
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,320,224);
        require(target && SDL_SetRenderTarget(renderer,target),"Practice render target unavailable");
        require(SDL_SetRenderLogicalPresentation(renderer,320,224,SDL_LOGICAL_PRESENTATION_DISABLED),"Practice native coordinates unavailable");
        GameSession game(roms);require(game.advance({}),"Practice initial board frame failed");game.take_audio();
        const auto before=game.info();const auto source=game.frame().pixels;
        const std::vector<RGB888> pixels(source.begin(),source.end());
        const auto switches=game.dip_switches();
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());
        const auto count=NativeStageSkip::stage_count(roms);require(count>1,"Practice test needs multiple ROM stages");
        menu.open(game);menu.key(SDLK_UP);
        require(menu.selection()==4 && menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::quit,"Practice displaced root Quit wrap");
        menu.key(SDLK_DOWN);require(menu.selection()==0,"five-row root forward wrap failed");
        enter(menu,"CHANGE MODE...");require(menu.pointer(160,77,true)==Frontend::NativeMenu::Action::none && menu.in_practice(),"Practice pointer entry failed");
        require(menu.practice_stage==1 && !menu.active_practice_stage,"Practice initial selection or active state incorrect");
        menu.key(SDLK_LEFT);require(menu.practice_stage==count,"Practice reverse stage wrap failed");
        menu.key(SDLK_RIGHT);require(menu.practice_stage==1,"Practice forward stage wrap failed");
        menu.key(SDLK_RETURN);require(menu.practice_stage==2,"Practice Enter stage adjustment failed");
        for(unsigned i=0;i<count;++i)menu.key(SDLK_RIGHT);
        require(menu.practice_stage==2,"Practice stage traversal lost the selected ROM slot");
        require(menu.pointer(80,77,true)==Frontend::NativeMenu::Action::practice,"Practice Start pointer action failed");
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::practice,"Practice Start keyboard action failed");
        require(!menu.active_practice_stage,"Practice menu action changed active session before launch");
        require(menu.pointer(80,94,true)==Frontend::NativeMenu::Action::none,"inactive Practice enabled Return to Arcade");
        menu.active_practice_stage=count;
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::arcade,"active Practice Return to Arcade action failed");
        menu.draw();readback(renderer,"build/oracle/practice-menu.bmp");
        menu.pointer(80,60,true);require(menu.practice_stage==3 && menu.active_practice_stage==count,"candidate stage edit changed retry target");
        menu.key(SDLK_ESCAPE);require(!menu.in_practice() && menu.selection()==1,"Practice Escape did not restore root row");
        menu.key(SDLK_RETURN);menu.key(SDLK_UP);
        require(menu.selection()==3 && menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::none && !menu.in_practice(),"Practice Back or reverse wrap failed");
        menu.key(SDLK_RETURN);require(menu.in_practice(),"Practice keyboard reentry failed");
        require(menu.pointer(80,111,true)==Frontend::NativeMenu::Action::none && !menu.in_practice(),"Practice pointer Back failed");
        menu.key(SDLK_RETURN);menu.key(SDLK_F1);require(!menu.in_practice() && menu.selection()==1,"Practice F1 did not restore root row");
        menu.practice_allowed=false;
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::none && !menu.in_practice() && menu.notice=="UNAVAILABLE WHILE RECORDING","recording allowed Practice entry");
        require(menu.pointer(160,77,true)==Frontend::NativeMenu::Action::none && !menu.in_practice(),"recording allowed Practice pointer entry");
        menu.practice_allowed=true;menu.notice.clear();menu.key(SDLK_RETURN);menu.practice_allowed=false;
        const auto selected_stage=menu.practice_stage;menu.key(SDLK_RIGHT);
        require(menu.practice_stage==selected_stage,"recording allowed Practice stage mutation");
        require(menu.pointer(80,77,true)==Frontend::NativeMenu::Action::none && menu.pointer(80,94,true)==Frontend::NativeMenu::Action::none,"recording allowed Practice launch or Arcade return");
        menu.draw();menu.key(SDLK_ESCAPE);require(!menu.in_practice(),"recording guard trapped Practice Back");
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        const auto blank=readback(renderer);menu.active_practice_stage=0;menu.draw_practice_status();
        require(readback(renderer)==blank,"inactive Practice drew a gameplay label");
        menu.active_practice_stage=count;menu.draw_practice_status();const auto active=readback(renderer);
        require(active!=blank && std::equal(active.begin(),active.begin()+28*320,blank.begin()) &&
                std::equal(active.begin()+40*320,active.end(),blank.begin()+40*320),"active Practice label missing or covering native HUD strips");
        menu.practice_stage=1;SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);menu.draw_practice_status();
        require(readback(renderer)==active,"Practice label followed candidate instead of active retry stage");
        require(!game.advance({}) && game.info().number==before.number && game.info().time_ns==before.time_ns &&
                std::memcmp(pixels.data(),game.frame().pixels.data(),pixels.size()*sizeof(RGB888))==0 &&
                game.take_audio().empty() && game.dip_switches()==switches,"Practice UI changed native RGB/PCM/time or DIP state");
        menu.close(game);require(!game.paused(),"Practice UI did not restore original pause state");
        game.set_paused(true);menu.practice_allowed=true;menu.open(game);practice(menu);
        require(menu.in_practice(),"paused Practice entry failed");menu.key(SDLK_ESCAPE);menu.close(game);
        require(game.paused(),"Practice UI lost preexisting pause state");
        require(SDL_SetRenderTarget(renderer,nullptr),"Practice render target restore failed");SDL_DestroyTexture(target);
        std::cout<<"Practice menu: ROM stage wrapping, keyboard/pointer actions, active retry target, recording guard, passive label and native RGB/PCM/time/DIP preservation passed.\n";
    }
    {
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,320,224);
        require(target && SDL_SetRenderTarget(renderer,target),"Shuffle render target unavailable");
        require(SDL_SetRenderLogicalPresentation(renderer,320,224,SDL_LOGICAL_PRESENTATION_DISABLED),"Shuffle native coordinates unavailable");
        GameSession game(roms);require(game.advance({}),"Shuffle initial board frame failed");game.take_audio();
        const auto before=game.info();const auto source=game.frame().pixels;
        const std::vector<RGB888> pixels(source.begin(),source.end());const auto switches=game.dip_switches();
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());
        const auto stage=NativeStageSkip::stage_count(roms);menu.practice_stage=stage;menu.active_practice_stage=stage;
        menu.open(game);enter(menu,"CHANGE MODE...");menu.draw();readback(renderer,"build/oracle/shuffle-root-menu.bmp");
        menu.pointer(160,94,false);require(menu.selection()==2 && !menu.in_shuffle(),"Shuffle root hover entered submenu");
        require(menu.pointer(160,94,true)==Frontend::NativeMenu::Action::none && menu.in_shuffle() && !menu.in_practice(),"Shuffle pointer entry failed");
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::shuffle && !menu.shuffle_active,"Shuffle keyboard action changed active session before launch");
        menu.draw();const auto inactive=readback(renderer);
        require(menu.pointer(80,77,true)==Frontend::NativeMenu::Action::none,"inactive Shuffle enabled Return to Arcade");
        menu.shuffle_active=true;
        require(menu.pointer(80,60,true)==Frontend::NativeMenu::Action::shuffle,"New Shuffle pointer action failed");
        menu.draw();require(readback(renderer,"build/oracle/shuffle-menu.bmp")!=inactive,"active Shuffle did not update New Shuffle label");
        require(menu.pointer(80,77,true)==Frontend::NativeMenu::Action::arcade,"active Shuffle Return to Arcade failed");
        menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);require(menu.selection()==0,"Shuffle forward row wrap failed");
        menu.key(SDLK_UP);require(menu.selection()==2,"Shuffle reverse row wrap failed");
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::none && !menu.in_shuffle() && menu.selection()==2,"Shuffle Back failed to restore root row");
        menu.key(SDLK_RETURN);menu.key(SDLK_ESCAPE);require(!menu.in_shuffle() && menu.selection()==2,"Shuffle Escape failed");
        menu.key(SDLK_RETURN);menu.key(SDLK_F1);require(!menu.in_shuffle() && menu.selection()==2,"Shuffle F1 failed");
        require(menu.practice_stage==stage && menu.active_practice_stage==stage,"Shuffle changed independent Practice selection or retry target");
        menu.active_practice_stage=0;menu.pointer(160,77,true);require(menu.in_practice(),"Practice entry from Shuffle run failed");
        require(menu.pointer(80,94,true)==Frontend::NativeMenu::Action::arcade,"Practice could not return active Shuffle run to Arcade");
        menu.key(SDLK_ESCAPE);menu.shuffle_allowed=false;
        require(menu.pointer(160,94,true)==Frontend::NativeMenu::Action::none && !menu.in_shuffle() && menu.notice=="UNAVAILABLE WHILE RECORDING","recording allowed Shuffle pointer entry");
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::none && !menu.in_shuffle(),"recording allowed Shuffle keyboard entry");
        menu.shuffle_allowed=true;menu.notice.clear();menu.key(SDLK_RETURN);menu.shuffle_allowed=false;
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::none && menu.pointer(80,77,true)==Frontend::NativeMenu::Action::none,"recording allowed Shuffle launch or Arcade return");
        menu.draw();require(menu.pointer(80,94,true)==Frontend::NativeMenu::Action::none && !menu.in_shuffle(),"recording guard trapped Shuffle pointer Back");
        menu.shuffle_active=false;SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        const auto blank=readback(renderer);menu.draw_practice_status();require(readback(renderer)==blank,"inactive Shuffle drew a gameplay label");
        menu.shuffle_active=true;
        for(const auto stats:{Frontend::Stats::off,Frontend::Stats::fps,Frontend::Stats::detailed}) {
            menu.settings.stats=stats;SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
            menu.draw_stats({59.6,59.6,16.8,12,1},false);const auto underlay=readback(renderer);
            menu.draw_practice_status();const auto label=readback(renderer);
            const auto top=stats==Frontend::Stats::detailed?74:stats==Frontend::Stats::fps?36:28;
            require(label!=underlay && std::equal(label.begin(),label.begin()+top*320,underlay.begin()) &&
                    std::equal(label.begin()+(top+12)*320,label.end(),underlay.begin()+(top+12)*320),"Shuffle label covered native HUD strips or active stats");
        }
        require(menu.practice_stage==stage && !menu.active_practice_stage,"Shuffle label changed independent Practice state");
        require(!game.advance({}) && game.info().number==before.number && game.info().time_ns==before.time_ns &&
                std::memcmp(pixels.data(),game.frame().pixels.data(),pixels.size()*sizeof(RGB888))==0 &&
                game.take_audio().empty() && game.dip_switches()==switches,"Shuffle UI changed native RGB/PCM/time or DIP state");
        menu.close(game);require(!game.paused(),"Shuffle UI did not restore original pause state");
        game.set_paused(true);menu.shuffle_allowed=true;menu.open(game);shuffle(menu);
        require(menu.in_shuffle(),"paused Shuffle entry failed");menu.key(SDLK_ESCAPE);menu.close(game);
        require(game.paused(),"Shuffle UI lost preexisting pause state");
        require(SDL_SetRenderTarget(renderer,nullptr),"Shuffle render target restore failed");SDL_DestroyTexture(target);
        std::cout<<"Shuffle menu: continuous-run/New Shuffle labels, keyboard/pointer wrap and actions, independent Practice state, recording guard, protected stats/HUD label and native RGB/PCM/time/DIP preservation passed.\n";
    }
    {
        GameSession game(roms);require(game.advance({}),"initial game advance failed");game.take_audio();
        const auto before=game.info();const auto source=game.frame().pixels;
        const std::vector<RGB888> pixels(source.begin(),source.end());
        // This block exercises Original -> Corrected. Keep its starting state
        // explicit now that fresh application settings default to Corrected.
        Frontend::Settings initial;initial.ground=false;
        Frontend::NativeMenu menu(renderer,roms,initial,Frontend::pixel_art_available());
        menu.apply=[&](const Frontend::Settings& s){return Frontend::apply_settings(window,renderer,texture,s);};
        menu.open(game);require(game.paused(),"opening menu did not freeze board");
        require(!game.advance({}) && game.info().number==before.number,"menu admitted a physical board frame");
        navigate(menu,{"SETTINGS"});require(menu.in_options(),"options navigation failed");
        const auto root_settings=menu.settings;menu.key(SDLK_LEFT);menu.key(SDLK_RIGHT);
        require(menu.settings==root_settings,"Options root changed a video setting");
        enter(menu,"DISPLAY");select(menu,"OUTPUT SCALING");require(menu.in_video(),"Video submenu entry failed");
        menu.key(SDLK_LEFT);menu.key(SDLK_RIGHT); // wrap filter selection without invalid enum
        menu.key(SDLK_RIGHT);require(menu.settings.scaling==Frontend::Scaling::linear,"smooth scaling selection failed");
        SDL_ScaleMode scale{};require(SDL_GetTextureScaleMode(texture,&scale) && scale==SDL_SCALEMODE_LINEAR,"SDL filter not applied");
        menu.key(SDLK_RIGHT);
        SDL_RendererLogicalPresentation presentation{};int w,h;
        require(SDL_GetRenderLogicalPresentation(renderer,&w,&h,&presentation) &&
            presentation==SDL_LOGICAL_PRESENTATION_INTEGER_SCALE && w==320 && h==224,"integer presentation not applied");
        navigate(menu,{"SETTINGS","GRAPHICS"});
        menu.key(SDLK_LEFT);require(menu.settings.sprites==Frontend::SpriteFilter::scalefx,"sprite filter reverse wrap failed");
        menu.key(SDLK_RIGHT);require(menu.settings.sprites==Frontend::SpriteFilter::none,"sprite filter forward wrap failed");
        for(int i=1;i<Frontend::sprite_filter_count;++i) {
            menu.key(SDLK_RIGHT);require(static_cast<int>(menu.settings.sprites)==i,"sprite filter selection failed");
        }
        enter(menu,"TRANSPARENCY");menu.key(SDLK_RIGHT);require(menu.settings.clouds && !menu.settings.trails,"cloud toggle failed");menu.key(SDLK_LEFT);
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.trails && !menu.settings.clouds,"trail toggle failed");menu.key(SDLK_LEFT);
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.explosions && !menu.settings.afterburner,"explosion toggle failed");menu.key(SDLK_LEFT);
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.afterburner && !menu.settings.explosions,"afterburner toggle failed");menu.key(SDLK_LEFT);
        if(gpu) {
            // Match the application's visible, resizable window and active
            // event/render loop before asking Cocoa to enter a fullscreen Space.
            require(SDL_ShowWindow(window) && SDL_RaiseWindow(window),"GPU menu window activation failed");
            SDL_PumpEvents();
            require(SDL_RenderPresent(renderer),"GPU menu initial window presentation failed");
            SDL_PumpEvents();
        }
        navigate(menu,{"SETTINGS","DISPLAY"});menu.key(SDLK_RIGHT);
        const auto fullscreen_deadline=SDL_GetTicks()+3000;
        while(menu.settings.fullscreen && !(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN) && SDL_GetTicks()<fullscreen_deadline) {
            SDL_PumpEvents();SDL_Delay(10);
        }
        require(menu.settings.fullscreen && (SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN),
            (std::string("fullscreen not applied: selected=")+std::to_string(menu.selection())+
                " requested="+std::to_string(menu.settings.fullscreen)+" flags="+std::to_string(SDL_GetWindowFlags(window))+
                " "+menu.notice+"; "+SDL_GetError()).c_str());
        menu.key(SDLK_LEFT);
        const auto windowed_deadline=SDL_GetTicks()+3000;
        while(!menu.settings.fullscreen && (SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN) && SDL_GetTicks()<windowed_deadline) {
            SDL_PumpEvents();SDL_Delay(10);
        }
        require(!menu.settings.fullscreen && !(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN),"windowed restoration failed");
        navigate(menu,{"SETTINGS","DIAGNOSTICS"});menu.key(SDLK_RIGHT);menu.key(SDLK_RIGHT);
        require(menu.settings.stats==Frontend::Stats::detailed,"stats mode selection failed");
        menu.draw();menu.draw_stats({59.6,59.6,16.8,12,1},true);SDL_RenderPresent(renderer);
        require(game.info().number==before.number && game.info().time_ns==before.time_ns &&
            std::memcmp(pixels.data(),game.frame().pixels.data(),pixels.size()*sizeof(RGB888))==0,
            "UI rendering mutated native frame/time");
        navigate(menu,{"SETTINGS","GRAPHICS"});select(menu,"GROUND");menu.key(SDLK_RIGHT);require(menu.settings.ground,"ground correction UI failed");menu.draw(); // Ground scrolls the Video viewport.
        navigate(menu,{"SETTINGS","DISPLAY"});select(menu,"ASPECT RATIO");menu.key(SDLK_RIGHT);require(Frontend::is_wide(menu.settings),"Viewport entry missing");
        require(SDL_GetRenderLogicalPresentation(renderer,&w,&h,&presentation)&&w==398&&h==224,"wide logical viewport not applied");
        SDL_Rect safe{39,0,320,224};require(SDL_SetRenderViewport(renderer,&safe),"centered menu viewport failed");
        float wx,wy;require(SDL_RenderCoordinatesToWindow(renderer,60,65,&wx,&wy),"wide mouse transform unavailable");
        SDL_Event pointer{};pointer.type=SDL_EVENT_MOUSE_MOTION;pointer.motion.windowID=SDL_GetWindowID(window);pointer.motion.x=wx;pointer.motion.y=wy;
        require(SDL_ConvertEventToRenderCoordinates(renderer,&pointer)&&std::abs(pointer.motion.x-60)<.01f&&std::abs(pointer.motion.y-65)<.01f,"wide menu pointer did not match centered drawing");
        menu.key(SDLK_RIGHT);require(menu.settings.viewport==Frontend::Viewport::wide16_10,"16:10 viewport entry missing");
        require(SDL_GetRenderLogicalPresentation(renderer,&w,&h,&presentation)&&w==358&&h==224,"16:10 logical viewport not applied");
        safe.x=19;require(SDL_SetRenderViewport(renderer,&safe),"16:10 centered menu viewport failed");
        require(SDL_RenderCoordinatesToWindow(renderer,60,65,&wx,&wy),"16:10 mouse transform unavailable");
        pointer.motion.x=wx;pointer.motion.y=wy;
        require(SDL_ConvertEventToRenderCoordinates(renderer,&pointer)&&std::abs(pointer.motion.x-60)<.01f&&std::abs(pointer.motion.y-65)<.01f,"16:10 menu pointer did not match centered drawing");
        menu.key(SDLK_RIGHT);require(menu.settings.viewport==Frontend::Viewport::wide21_9,"21:9 viewport entry missing");
        require(SDL_GetRenderLogicalPresentation(renderer,&w,&h,&presentation)&&w==522&&h==224,"21:9 logical viewport not applied");
        safe.x=101;require(SDL_SetRenderViewport(renderer,&safe),"21:9 centered menu viewport failed");
        require(SDL_RenderCoordinatesToWindow(renderer,60,65,&wx,&wy),"21:9 mouse transform unavailable");
        pointer.motion.x=wx;pointer.motion.y=wy;
        require(SDL_ConvertEventToRenderCoordinates(renderer,&pointer)&&std::abs(pointer.motion.x-60)<.01f&&std::abs(pointer.motion.y-65)<.01f,"21:9 menu pointer did not match centered drawing");
        menu.key(SDLK_RIGHT);require(menu.settings.viewport==Frontend::Viewport::original,"viewport forward wrap failed");
        menu.key(SDLK_LEFT);require(menu.settings.viewport==Frontend::Viewport::wide21_9,"viewport reverse wrap failed");
        menu.key(SDLK_LEFT);require(menu.settings.viewport==Frontend::Viewport::wide16_10,"16:10 viewport return failed");
        menu.key(SDLK_LEFT);require(menu.settings.viewport==Frontend::Viewport::wide16_9,"16:9 viewport return failed");
        menu.key(SDLK_LEFT);require(!Frontend::is_wide(menu.settings),"Original viewport return failed");SDL_SetRenderViewport(renderer,nullptr);
        navigate(menu,{"SETTINGS","GRAPHICS","TRANSPARENCY"});menu.pointer(60,60,false); // Hover the first effects row.
        const auto prior_clouds=menu.settings.clouds;menu.key(SDLK_RIGHT);
        require(menu.settings.clouds!=prior_clouds,"scrolled mouse row selected the wrong option");
        const auto accepted=menu.settings;menu.apply=[](const auto&){return false;};menu.key(SDLK_RIGHT);
        require(menu.settings==accepted,"failed option must retain last accepted settings");
        menu.notice.clear();
        menu.key(SDLK_ESCAPE);require(menu.page()==Menu::Page::graphics && menu.row_label(menu.selection())=="TRANSPARENCY","Effects Escape lost parent selection");
        select(menu,"HUD STYLE");
        menu.apply=[&](const Frontend::Settings& s){return Frontend::apply_settings(window,renderer,texture,s);};
        menu.key(SDLK_RIGHT);require(menu.settings.hud==Frontend::Hud::sharp,"Sharp HUD selection failed");
        menu.key(SDLK_RIGHT);require(menu.settings.hud==Frontend::Hud::original,"missing custom atlas trapped forward HUD navigation");
        menu.key(SDLK_LEFT);require(menu.settings.hud==Frontend::Hud::sharp,"missing custom atlas trapped reverse HUD navigation");
        menu.custom_hud_available=true;
        menu.key(SDLK_RIGHT);require(menu.settings.hud==Frontend::Hud::custom,"Custom HUD selection failed");
        menu.key(SDLK_RIGHT);require(menu.settings.hud==Frontend::Hud::original,"HUD forward wrap failed");
        menu.key(SDLK_LEFT);require(menu.settings.hud==Frontend::Hud::custom,"HUD reverse wrap failed");
        menu.key(SDLK_RIGHT);select(menu,"BACK");
        menu.key(SDLK_RETURN);require(menu.page()==Menu::Page::settings && menu.row_label(menu.selection())=="GRAPHICS","Graphics Back failed");
        select(menu,"DISPLAY");menu.pointer(60,60,true);require(menu.page()==Menu::Page::display,"Display pointer entry failed");
        menu.draw();auto* video_image=SDL_RenderReadPixels(renderer,nullptr);require(video_image,"Video menu readback failed");
        require(SDL_SaveBMP(video_image,"build/oracle/video-menu.bmp"),"Video image save failed");SDL_DestroySurface(video_image);
        menu.key(SDLK_ESCAPE);
        menu.key(SDLK_ESCAPE);require(!menu.in_options(),"options Back failed");
        require(menu.key(SDLK_ESCAPE)==Frontend::NativeMenu::Action::close,"home Escape failed");
        menu.close(game);require(!game.paused() && game.advance({}),"menu resume failed");
        game.set_paused(true);menu.open(game);menu.close(game);require(game.paused(),"menu lost preexisting pause");
        menu.open(game);menu.key(SDLK_UP);
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::quit,"menu quit action failed");menu.close(game);
        GameSession cold(roms);menu.open(cold);
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::start,"cold menu Start action failed");
        require(menu.row_label(0)=="ARCADE" && menu.row_label(1)=="PRACTICE" && menu.row_label(2)=="SHUFFLE" && menu.row_label(3)=="SETTINGS","startup mode/settings order wrong");
        menu.pointer(60,123,true);require(menu.in_practice(),"startup Practice pointer entry failed");menu.key(SDLK_ESCAPE);
        menu.pointer(60,140,true);require(menu.in_shuffle(),"startup Shuffle pointer entry failed");menu.key(SDLK_ESCAPE);
        menu.close(cold);menu.open(game);
        require(menu.row_label(0)=="RESUME" && menu.row_label(1)=="RESTART RUN" && menu.row_label(2)=="CHANGE MODE...","pause actions missing");
        select(menu,"RESTART RUN");require(menu.key(SDLK_RETURN)==Menu::Action::restart,"pause Restart Run action failed");
        enter(menu,"CHANGE MODE...");require(menu.key(SDLK_RETURN)==Menu::Action::arcade,"Change Mode Arcade action failed");
        menu.practice_allowed=false;require(menu.key(SDLK_RETURN)==Menu::Action::none,"recording allowed Change Mode Arcade");
        enter(menu,"BACK");require(menu.page()==Menu::Page::root && menu.row_label(menu.selection())=="CHANGE MODE...","Change Mode Back lost root selection");menu.close(game);
    }
    {
        const std::string config="build/oracle/crt-menu.cfg";std::filesystem::remove(config);
        GameSession game(roms);require(game.advance({}),"CRT menu initial frame failed");game.take_audio();const auto before=game.info();
        const auto source=game.frame().pixels;const std::vector<RGB888> pixels(source.begin(),source.end());
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());
        menu.apply=[&](const Frontend::Settings& s){return Frontend::apply_settings(window,renderer,texture,s) && Frontend::save_settings(config,s);};
        const auto persisted=[&]{require(Frontend::load_settings(config,true)==menu.settings,"CRT menu settings not persisted");};
        menu.open(game);navigate(menu,{"SETTINGS","CRT AND SHADERS"});
        unsigned shader_choices=0;menu.choose_shader=[&]{++shader_choices;};
        menu.key(SDLK_UP);require(menu.selection()==4,"CRT rows displaced Video Back");
        menu.key(SDLK_UP);require(menu.selection()==3,"RetroArch preset row missing");
        menu.key(SDLK_RIGHT);menu.key(SDLK_LEFT);require(!shader_choices,"preset picker opened without explicit activation");
        const auto before_choice=menu.settings;menu.key(SDLK_RETURN);
        require(shader_choices==1 && menu.settings==before_choice,"preset picker callback changed settings before an accepted result");
        menu.key(SDLK_UP);require(menu.selection()==2,"RGB Mask menu row missing");
        menu.key(SDLK_RIGHT);require(menu.settings.crt==Frontend::Crt::subtle && menu.settings.crt_scanlines==25 && menu.settings.crt_mask==20,"adjusting mask while Off did not enable CRT independently");persisted();
        menu.key(SDLK_UP);menu.key(SDLK_RIGHT);
        require(menu.settings.crt==Frontend::Crt::subtle && menu.settings.crt_scanlines==30 && menu.settings.crt_mask==20,"scanline adjustment changed mask");
        menu.key(SDLK_UP);require(menu.selection()==0,"CRT mode menu row missing");
        menu.key(SDLK_RIGHT);require(menu.settings.crt==Frontend::Crt::arcade && menu.settings.crt_scanlines==50 && menu.settings.crt_mask==35,"Arcade preset did not reset strengths");persisted();
        menu.key(SDLK_DOWN);menu.key(SDLK_LEFT);
        require(menu.settings.crt==Frontend::Crt::arcade && menu.settings.crt_scanlines==45 && menu.settings.crt_mask==35,"strength edit changed the active preset");
        for(int i=0;i<30;++i)menu.key(SDLK_RIGHT);require(menu.settings.crt_scanlines==100,"scanline upper clamp failed");
        for(int i=0;i<30;++i)menu.key(SDLK_LEFT);require(menu.settings.crt_scanlines==0,"scanline lower clamp failed");
        menu.key(SDLK_DOWN);for(int i=0;i<30;++i)menu.key(SDLK_RIGHT);require(menu.settings.crt_mask==100,"mask upper clamp failed");
        for(int i=0;i<30;++i)menu.key(SDLK_LEFT);require(menu.settings.crt_mask==0,"mask lower clamp failed");persisted();
        menu.key(SDLK_UP);menu.key(SDLK_UP);menu.key(SDLK_RIGHT);
        require(menu.settings.crt==Frontend::Crt::retroarch && !menu.settings.crt_scanlines && !menu.settings.crt_mask,"RetroArch mode reset built-in strengths");persisted();
        menu.shader_status="CRT-GEOM ACTIVE";const auto shader_settings=menu.settings;
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);menu.key(SDLK_DOWN);menu.key(SDLK_LEFT);menu.draw();
        require(menu.settings==shader_settings,"RetroArch mode allowed built-in scanline/mask edits");
        menu.key(SDLK_DOWN);require(menu.selection()==3,"RetroArch preset picker unreachable");menu.key(SDLK_RETURN);
        require(shader_choices==2,"RetroArch preset picker did not activate");
        menu.key(SDLK_UP);menu.key(SDLK_UP);menu.key(SDLK_UP);menu.key(SDLK_RIGHT);
        require(menu.settings.crt==Frontend::Crt::off && !menu.settings.crt_scanlines && !menu.settings.crt_mask,"Off changed retained CRT strengths");persisted();
        menu.key(SDLK_LEFT);require(menu.settings.crt==Frontend::Crt::retroarch,"CRT reverse wrap failed");
        menu.key(SDLK_LEFT);require(menu.settings.crt==Frontend::Crt::arcade && menu.settings.crt_scanlines==50 && menu.settings.crt_mask==35,"Arcade reverse selection failed");
        menu.key(SDLK_LEFT);require(menu.settings.crt==Frontend::Crt::subtle && menu.settings.crt_scanlines==25 && menu.settings.crt_mask==15,"Subtle preset did not reset strengths");
        menu.key(SDLK_LEFT);menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);
        require(menu.settings.crt==Frontend::Crt::subtle && menu.settings.crt_scanlines==30 && menu.settings.crt_mask==15,"adjusting scanlines while Off did not enable CRT independently");
        const auto accepted=menu.settings;menu.apply=[](const auto&){return false;};menu.key(SDLK_RIGHT);
        require(menu.settings==accepted,"rejected CRT strength changed accepted settings");menu.notice.clear();
        menu.apply=[&](const Frontend::Settings& s){return Frontend::apply_settings(window,renderer,texture,s) && Frontend::save_settings(config,s);};
        menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);menu.draw();require(menu.selection()==4,"Video Back unreachable after CRT edits");
        menu.pointer(60,60,true);require(menu.selection()==0 && menu.settings.crt==Frontend::Crt::arcade,"scrolled CRT mode pointer selected the wrong row");persisted();
        menu.pointer(60,77,true);require(menu.selection()==1 && menu.settings.crt_scanlines==55,"scrolled Scanlines pointer selected the wrong row");
        menu.pointer(60,94,true);require(menu.selection()==2 && menu.settings.crt_mask==40,"scrolled RGB Mask pointer selected the wrong row");persisted();
        menu.pointer(60,111,true);require(menu.selection()==3 && shader_choices==3,"scrolled preset pointer selected the wrong row");
        menu.draw();const auto before_rebind=readback(renderer);
        menu.rebind(nullptr,roms);require(menu.is_open() && menu.in_video() && menu.selection()==3 && menu.settings.crt==Frontend::Crt::arcade,"renderer detach lost menu state");
        menu.rebind(renderer,roms);menu.key(SDLK_RETURN);require(shader_choices==4 && menu.selection()==3 && menu.shader_status=="CRT-GEOM ACTIVE","renderer rebind lost callbacks or shader status");
        menu.draw();require(readback(renderer)==before_rebind,"renderer rebind changed menu font/layout pixels");
        menu.draw();const auto ui=readback(renderer);
        std::vector<uint32_t> uploaded(pixels.size());
        for(size_t i=0;i<pixels.size();++i)uploaded[i]=0xff000000u|uint32_t(pixels[i].r)<<16|uint32_t(pixels[i].g)<<8|pixels[i].b;
        require(SDL_UpdateTexture(texture,nullptr,uploaded.data(),320*4) && SDL_RenderTexture(renderer,texture,nullptr,nullptr),"CRT menu game-image draw failed");
        Frontend::NativeCrt crt(renderer);require(crt.draw(menu.settings),"CRT menu display pass failed");
        menu.draw();require(readback(renderer,"build/oracle/crt-video-menu.bmp")==ui,"CRT pass changed the following frontend UI");
        require(!game.advance({}) && game.info().number==before.number && game.info().time_ns==before.time_ns &&
                std::memcmp(pixels.data(),game.frame().pixels.data(),pixels.size()*sizeof(RGB888))==0,"CRT menu changed native frame/time");
        menu.pointer(60,128,true);require(!menu.in_video() && menu.in_options() && menu.row_label(menu.selection())=="CRT AND SHADERS","scrolled Video Back pointer failed");
        menu.key(SDLK_RETURN);menu.key(SDLK_UP);menu.key(SDLK_DOWN);require(menu.selection()==0,"Video forward wrap failed with CRT rows");
        menu.key(SDLK_ESCAPE);menu.close(game);require(!game.paused(),"CRT menu lost pause state");std::filesystem::remove(config);
        std::cout<<"CRT menu: presets, independent 5% strengths/clamps, persistence, rejection, scroll/pointer navigation and native frame/time identity passed.\n";
    }
    {
        GameSession original(roms),enhanced(roms);Frontend::NativeCrt crt(renderer);
        std::vector<uint32_t> uploaded(320*224);unsigned displays=0;
        for(unsigned frame=0;frame<781;++frame){
            GameControls controls;controls.coin=frame>=360&&frame<372;controls.start=frame>=420&&frame<432;
            if(frame>=480){
                const auto phase=((frame-480)/24)%12;
                constexpr float x[]{0,-1,1,0,0,0,0,-1,1,0,0,0},y[]{0,0,0,-1,1,0,0,1,-1,0,0,0};
                controls.stick_x=x[phase];controls.stick_y=y[phase];controls.throttle=phase==5?-1:phase==6?1:0;
                controls.cannon=phase==7||phase==9||phase==10;controls.missile=phase==8||phase==9||phase==10;
            }
            require(original.advance(controls) && enhanced.advance(controls),"CRT physical replay advance failed");
            require(original.info().number==enhanced.info().number && original.info().time_ns==enhanced.info().time_ns,"CRT presentation changed native time");
            const auto expected=original.frame().pixels,actual=enhanced.frame().pixels;
            require(expected.size()==actual.size() && !std::memcmp(expected.data(),actual.data(),expected.size_bytes()),"CRT presentation changed native RGB");
            require(original.take_audio()==enhanced.take_audio(),"CRT presentation changed native PCM");
            if(frame<480 || frame%60)continue;
            Frontend::Settings display;display.viewport=static_cast<Frontend::Viewport>(displays%Frontend::viewport_count);
            const unsigned width=Frontend::viewport_width(display);
            require(Frontend::apply_settings(window,renderer,texture,display) && SDL_SetRenderViewport(renderer,nullptr),"CRT replay presentation setup failed");
            for(size_t i=0;i<actual.size();++i)uploaded[i]=0xff000000u|uint32_t(actual[i].r)<<16|uint32_t(actual[i].g)<<8|actual[i].b;
            require(SDL_UpdateTexture(texture,nullptr,uploaded.data(),320*4),"CRT replay texture upload failed");
            SDL_SetRenderDrawColor(renderer,0,0,0,255);require(SDL_RenderClear(renderer),"CRT replay clear failed");
            SDL_FRect destination{float((width-320)/2),0,320,224};require(SDL_RenderTexture(renderer,texture,nullptr,&destination),"CRT replay draw failed");
            const auto plain=readback(renderer);require(crt.draw(display) && readback(renderer)==plain,"Off changed the displayed native image");
            display.crt=displays%2?Frontend::Crt::arcade:Frontend::Crt::subtle;
            display.crt_scanlines=displays%2?50:25;display.crt_mask=displays%2?35:15;
            require(crt.draw(display),"enabled CRT replay draw failed");
            require(readback(renderer,frame==780?"build/oracle/crt-native-flight.bmp":nullptr)!=plain,"enabled CRT did not visibly affect native game pixels");
            require(original.info().number==enhanced.info().number && original.info().time_ns==enhanced.info().time_ns &&
                    !std::memcmp(expected.data(),enhanced.frame().pixels.data(),expected.size_bytes()) && enhanced.take_audio().empty(),"CRT draw changed native RGB/PCM/time");
            ++displays;
        }
        require(displays==6,"CRT replay did not cover both presets at all three widths");
        require(Frontend::apply_settings(window,renderer,texture,{}),"CRT replay presentation restore failed");
        std::cout<<"CRT presentation: 781 of 781 physical-input native RGB/PCM/time frames identical, 0 skipped; Off identity and both presets visible at 320/358/398.\n";
    }
    {
        GameSession game(roms);game.advance({});const auto before=game.info();
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());
        menu.open(game);navigate(menu,{"SETTINGS","ARCADE SETTINGS"});require(menu.in_dips(),"DIP submenu entry failed");
        menu.key(SDLK_LEFT);require(menu.settings.lives==1,"lives reverse wrap failed");
        menu.key(SDLK_RIGHT);require(menu.settings.lives==0,"lives forward wrap failed");
        menu.key(SDLK_RIGHT);require(menu.settings.lives==1,"four lives selection failed");
        menu.key(SDLK_DOWN);menu.key(SDLK_LEFT);require(menu.settings.difficulty==0,"Easy selection failed");
        menu.key(SDLK_LEFT);require(menu.settings.difficulty==3,"difficulty reverse wrap failed");
        menu.key(SDLK_RIGHT);require(menu.settings.difficulty==0,"difficulty forward wrap failed");
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.lives==3,"Ship Increase Yes failed");
        require(Frontend::dip_switches(menu.settings)[2]==133,"Ship Increase did not clear SWB:5");
        menu.key(SDLK_LEFT);require(menu.settings.lives==1,"Ship Increase No changed lives per credit");
        menu.key(SDLK_RETURN);require(menu.settings.lives==3,"Ship Increase Enter failed");
        const auto accepted=menu.settings;menu.apply=[](const auto&){return false;};menu.key(SDLK_RIGHT);
        require(menu.settings==accepted,"rejected Ship Increase changed saved switches");menu.apply={};menu.notice.clear();
        menu.draw();auto* image=SDL_RenderReadPixels(renderer,nullptr);require(image,"DIP menu readback failed");
        require(SDL_SaveBMP(image,"build/oracle/dip-menu.bmp"),"DIP image save failed");SDL_DestroySurface(image);
        require(game.info().number==before.number && game.info().time_ns==before.time_ns && game.dip_switches()==std::array<uint8_t,3>{255,255,221},"pending DIP edit changed active board");
        menu.key(SDLK_DOWN);require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::restart,"Apply and Restart action missing");
        menu.close(game);game.restart();game.set_dip_switches(Frontend::dip_switches(menu.settings));
        require(game.dip_switches()==std::array<uint8_t,3>{255,255,133},"restart did not apply selected DIP pins");
        const std::string recording_path="build/oracle/dip-menu-input.txt";
        {
            PhysicalRecording recording(recording_path,roms,game.dip_switches());recording.append(0,{});
            std::ifstream file(recording_path);std::string magic;unsigned count,b,c,d;file>>magic>>count>>b>>c>>d;
            require(magic=="AB2PHYS1" && count==1 && b==255 && c==255 && d==133,"recording header lost selected switches");
            std::ifstream metadata(recording_path+".json");std::string json((std::istreambuf_iterator<char>(metadata)),{});
            require(json.find("\"switches\":[255,255,133]")!=std::string::npos,"recording metadata lost selected switches");
        }
        std::filesystem::remove(recording_path);std::filesystem::remove(recording_path+".json");
        game.advance({});bool rejected=false;try{game.set_dip_switches({255,255,221});}catch(const std::runtime_error&){rejected=true;}
        require(rejected,"running board accepted cold-only DIP configuration");
        menu.open(game);navigate(menu,{"SETTINGS","ARCADE SETTINGS"});
        menu.key(SDLK_ESCAPE);require(menu.in_options() && menu.row_label(menu.selection())=="ARCADE SETTINGS","DIP Escape did not restore Options row");
        menu.key(SDLK_RETURN);menu.pointer(40,128,true);require(!menu.in_dips(),"DIP pointer Back failed");menu.close(game);
    }
    {
        Frontend::NativeInput input("");Frontend::NativeHaptics feedback(input,"");
        GameSession game(roms);Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.input=&input;menu.feedback=&feedback;
        menu.open(game);navigate(menu,{"SETTINGS","CONTROLS","FEEDBACK"});
        require(menu.selection()==0,"Feedback submenu entry");menu.key(SDLK_RETURN);require(feedback.settings.enabled,"Feedback enabled UI");
        for(int row=0;row<menu.row_count();++row){menu.draw();SDL_RenderPresent(renderer);menu.key(SDLK_DOWN);}
        enter(menu,"CUES");for(int row=0;row<menu.row_count();++row){menu.draw();SDL_RenderPresent(renderer);menu.key(SDLK_DOWN);}menu.key(SDLK_ESCAPE);
        enter(menu,"TEST FEEDBACK");for(int row=0;row<menu.row_count();++row){menu.draw();SDL_RenderPresent(renderer);menu.key(SDLK_DOWN);}menu.key(SDLK_ESCAPE);
        menu.key(SDLK_ESCAPE);require(menu.in_controls()&&menu.row_label(menu.selection())=="FEEDBACK","Feedback Escape navigation");menu.key(SDLK_RETURN);
        select(menu,"BODY STRENGTH");menu.key(SDLK_RIGHT);require(feedback.settings.body==60,"Feedback strength UI");
        menu.close(game);require(!menu.is_open(),"Feedback menu close");
    }
    {
        const auto config=(std::filesystem::temp_directory_path()/"ab2-menu-input.cfg").string();std::filesystem::remove(config);
        SDL_VirtualJoystickDesc descriptor;SDL_INIT_INTERFACE(&descriptor);
        descriptor.type=SDL_JOYSTICK_TYPE_FLIGHT_STICK;descriptor.naxes=3;descriptor.nbuttons=6;descriptor.nhats=1;
        descriptor.name="Calibration test joystick";
        const auto virtual_id=SDL_AttachVirtualJoystick(&descriptor);require(virtual_id!=0,"calibration joystick attach failed");
        Frontend::NativeInput input(config);
        GameSession game(roms);Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.input=&input;
        menu.open(game);navigate(menu,{"SETTINGS","CONTROLS"});require(menu.in_controls(),"Controls entry unavailable");
        enter(menu,"INPUT MONITOR");require(!menu.in_calibration(),"Keyboard calibration should be disabled");
        select(menu,"INPUT DEVICE");
        for(size_t i=0;i<input.connected().size() && input.device_name()!="Calibration test joystick";++i)menu.key(SDLK_RIGHT);
        require(input.device_name()=="Calibration test joystick","Controls device selection failed");
        enter(menu,"ANALOG SETTINGS");const auto deadzone=input.profile().deadzone;
        menu.key(SDLK_RIGHT);require(input.profile().deadzone==deadzone+5,"Analog dead zone navigation failed");menu.key(SDLK_LEFT);
        select(menu,"SENSITIVITY");const auto sensitivity=input.profile().sensitivity;menu.key(SDLK_RIGHT);require(input.profile().sensitivity==sensitivity+10,"Analog sensitivity navigation failed");menu.key(SDLK_LEFT);
        select(menu,"THROTTLE");const bool full=input.profile().full_throttle;menu.key(SDLK_RETURN);require(input.profile().full_throttle!=full,"Analog throttle navigation failed");menu.key(SDLK_RETURN);menu.key(SDLK_ESCAPE);
        enter(menu,"INPUT MONITOR");require(menu.in_calibration(),"Joystick calibration did not open");
        const auto joystick=std::find_if(input.connected().begin(),input.connected().end(),[&](const auto& d){return d.id==virtual_id;});
        require(joystick!=input.connected().end() && SDL_SetJoystickVirtualAxis(joystick->stick,0,16384),"calibration joystick movement failed");
        SDL_UpdateJoysticks();require(input.calibration().mapped.stick_x>.4f,"live joystick calibration did not respond");
        menu.draw();
        auto* calibration_image=SDL_RenderReadPixels(renderer,nullptr);require(calibration_image,"Calibration readback failed");
        require(SDL_SaveBMP(calibration_image,"build/oracle/input-calibration.bmp"),"Calibration image save failed");SDL_DestroySurface(calibration_image);
        menu.key(SDLK_ESCAPE);require(menu.in_controls()&&menu.row_label(menu.selection())=="INPUT MONITOR","Calibration return navigation");
        select(menu,"INPUT DEVICE");
        for(size_t i=0;i<input.connected().size() && input.controller_selected();++i)menu.key(SDLK_LEFT);
        require(!input.controller_selected(),"Controls keyboard return failed");
        enter(menu,"BUTTON BINDINGS");
        menu.key(SDLK_RETURN);require(input.waiting(),"UI did not start key capture");
        SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_D;e.key.scancode=SDL_SCANCODE_D;
        require(input.event(e) && !input.waiting() && input.profile().bindings[0].index==SDL_SCANCODE_D,"UI key remap failed");
        menu.key(SDLK_BACKSPACE);require(input.profile().bindings[0].kind=='-',"UI clear binding failed");
        for(int page=0;page<17;++page) {
            menu.draw();
            if(page==2 || page==12) {
                auto* image=SDL_RenderReadPixels(renderer,nullptr);require(image,"Controls readback failed");
                std::filesystem::create_directories("build/oracle");require(SDL_SaveBMP(image,page==2?"build/oracle/input-menu.bmp":"build/oracle/input-menu-device.bmp"),"Controls image save failed");SDL_DestroySurface(image);
            }
            SDL_RenderPresent(renderer);menu.key(SDLK_DOWN);
        }
        menu.key(SDLK_ESCAPE);menu.key(SDLK_ESCAPE);require(!menu.in_controls() && menu.in_options(),"Controls back navigation");
        menu.close(game);require(SDL_DetachVirtualJoystick(virtual_id),"calibration joystick detach failed");std::filesystem::remove(config);
    }
    {
        const auto folder=std::filesystem::path("build/oracle/music-fixtures");
        const auto config=(folder/"menu-audio.cfg").string();std::filesystem::remove(config);
        Frontend::NativeMusic music(config,folder);GameSession game(roms);
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.music=&music;
        menu.open(game);navigate(menu,{"SETTINGS","AUDIO"});
        menu.key(SDLK_LEFT);require(music.settings.music==95 && music.settings.effects==100,"music volume UI failed");
        menu.key(SDLK_DOWN);menu.key(SDLK_LEFT);require(music.settings.effects==95,"effects volume UI failed");
        menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
        menu.key(SDLK_RIGHT);for(int i=0;i<50;++i){music.poll();SDL_Delay(2);} // invalid first entry retains Original
        menu.key(SDLK_RIGHT);for(int i=0;i<1000 && music.settings.files[0].empty();++i){music.poll();SDL_Delay(2);}
        require(music.settings.files[0]=="B tone.WAV","folder cycling UI failed");
        menu.draw();auto* image=SDL_RenderReadPixels(renderer,nullptr);require(image,"Music menu readback failed");
        require(SDL_SaveBMP(image,"build/oracle/music-menu.bmp"),"Music menu image save failed");SDL_DestroySurface(image);
        menu.key(SDLK_ESCAPE);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);menu.key(SDLK_LEFT);
        require(music.settings.track_volume[0]==95,"track volume UI failed");
        menu.key(SDLK_ESCAPE); // Audio page, Track Volumes selected.
        const auto output_config=(folder/"menu-output.cfg").string();std::filesystem::remove(output_config);
        Frontend::AudioOutput output(output_config);menu.audio=&output;music.output=&output;output.pause(true);
        enter(menu,"OUTPUT SETUP"); // Output page.
        menu.key(SDLK_RIGHT);require(output.settings.profile==Frontend::AudioProfile::headphones,"headphones profile menu failed");
        require(Frontend::load_output_settings(output_config)==output.settings,"output profile not persisted");
        menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN); // finite speaker test
        menu.draw();image=SDL_RenderReadPixels(renderer,nullptr);require(image,"Audio Output menu readback failed");
        require(SDL_SaveBMP(image,"build/oracle/audio-output-menu.bmp"),"Audio Output menu image save failed");SDL_DestroySurface(image);
        menu.key(SDLK_ESCAPE); // clears test and returns to Audio/Output.
        enter(menu,"MUSIC TRACKS");menu.key(SDLK_SPACE);
        require(music.previewing(),"selected-backend preview did not start");music.update_preview();menu.key(SDLK_SPACE);require(!music.previewing(),"selected-backend preview did not stop");
        menu.key(SDLK_ESCAPE);
        const auto before=game.info();require(!game.advance({}) && game.info().time_ns==before.time_ns,"audio menu advanced native board");
        menu.close(game);music.output=nullptr;menu.audio=nullptr;std::filesystem::remove(config);
        const auto input_config=(folder/"menu-navigation.inputs").string();
        Frontend::NativeInput input(input_config);
        for(unsigned present=0;present<4;++present) {
            Frontend::NativeMenu navigation(renderer,roms,{},Frontend::pixel_art_available());
            navigation.input=present&1?&input:nullptr;navigation.music=present&2?&music:nullptr;
            navigation.open(game);navigate(navigation,{"SETTINGS"});
            navigation.pointer(60,60,true);require(navigation.in_video(),"optional menu layout lost Video entry");
            navigation.key(SDLK_ESCAPE);
            const int dip_row=3+bool(present&1)+bool(present&2);
            navigation.pointer(60,60+dip_row*16,true);require(navigation.in_dips(),"optional menu layout lost DIP entry");
            navigation.key(SDLK_ESCAPE);require(navigation.selection()==dip_row,"optional DIP return row wrong");
            if(present==3){navigation.draw();auto* image=SDL_RenderReadPixels(renderer,nullptr);require(image,"Options readback failed");require(SDL_SaveBMP(image,"build/oracle/options-menu.bmp"),"Options image save failed");SDL_DestroySurface(image);}
            navigation.close(game);
        }
        std::filesystem::remove(input_config);
    }
    {
        require(!Presentation::full_throttle_flame(16,0xfc) && Presentation::full_throttle_flame(16,0xfd) &&
                !Presentation::full_throttle_flame(16,0xffff) && !Presentation::full_throttle_flame(1,0xfd),"full-throttle flame boundaries changed");
        GameSession throttle(roms);throttle.capture_sprite_scene(true);
        unsigned full_flames=0,ordinary_candidates=0,full_changed=0,white_cores=0,opaque_body_white=0,best_cores=0,gpu_flame_checks=0;
        Frontend::SpriteScaler throttle_cpu;
        auto throttle_gpu=gpu?std::make_unique<Frontend::GPUSpriteScaler>(renderer):nullptr;
        auto* throttle_target=gpu?SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,960,672):nullptr;
        if(gpu)require(throttle_target && SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED),"throttle GPU target unavailable");
        for(unsigned f=0;f<3600;++f) {
            GameControls input;input.coin=f>=360 && f<372;input.start=f>=420 && f<432;
            if((f>=1800 && f<2160) || (f>=3000 && f<3360))input.throttle=1;
            if(f>=2520 && f<2880)input.throttle=-1;
            require(throttle.advance(input),"throttle scene advance failed");throttle.take_audio();
            if(f<620 || f%360<20)continue; // ADC/ROM state settles after each physical transition.
            const auto scene=throttle.sprite_scene();const auto planes=throttle.sprite_presentation();
            const bool full=input.throttle==1;
            for(const auto& fragment:scene.fragments) {
                if(fragment.kind==6){require(full,"ordinary throttle classified a translucent flame");++full_flames;}
                if(!full && ((fragment.raw>>4)&255)==0xf2 && ((fragment.raw&15)==1 || (fragment.raw&15)==2 || (fragment.raw&15)==4))++ordinary_candidates;
            }
            std::vector<uint32_t> flame_foreground(320*224),opaque_foreground(320*224),flame_background(planes.background.begin(),planes.background.end());
            unsigned visible_cores=0;
            for(unsigned pixel=0;pixel<320*224;++pixel) {
                uint32_t opaque_background=planes.background[pixel],transparent_background=opaque_background;
                const auto opaque=Presentation::foreground(scene.fragments,scene.heads[pixel],255,255,&opaque_background,255,255);
                const auto translucent=Presentation::foreground(scene.fragments,scene.heads[pixel],255,255,&transparent_background,255,Presentation::afterburner_opacity);
                if(!full)require(opaque==translucent && opaque_background==transparent_background,"idle/braking flame changed composition");
                else full_changed+=opaque!=translucent;
                opaque_foreground[pixel]=opaque;flame_foreground[pixel]=translucent;flame_background[pixel]=transparent_background;
                const auto head=scene.heads[pixel];
                if(full && head!=UINT32_MAX){
                    const auto& fragment=scene.fragments[head];const auto pen=fragment.raw&15;
                    if(fragment.kind==6 && (fragment.color&255)>220 && ((fragment.color>>8)&255)>220 && ((fragment.color>>16)&255)>220){if(opaque!=translucent){++white_cores;++visible_cores;}}
                    if(((fragment.raw>>4)&255)==0xf2 && pen==6 && fragment.kind==0){require(opaque==translucent,"aircraft white pixel became transparent");++opaque_body_white;}
                    if(((fragment.raw>>4)&255)==0xf2)require(opaque==translucent,"airframe/nozzle glow became transparent");
                }
            }
            if(visible_cores>best_cores){
                best_cores=visible_cores;
                const auto save=[&](const char* path,std::span<const uint32_t> pixels){std::ofstream file(path,std::ios::binary);file<<"P6\n320 224\n255\n";for(auto color:pixels){file.put(color>>16);file.put(color>>8);file.put(color);}require(bool(file),"flame comparison image save failed");};
                save("build/oracle/full-flame-opaque.ppm",throttle_cpu.compose(planes.background,opaque_foreground,320,224,Frontend::SpriteFilter::none,planes.protected_pixels));
                save("build/oracle/full-flame-translucent.ppm",throttle_cpu.compose(flame_background,flame_foreground,320,224,Frontend::SpriteFilter::none,planes.protected_pixels));
            }
            if(gpu && visible_cores && !gpu_flame_checks){
                for(unsigned filter=0;filter<Frontend::sprite_filter_count;++filter){
                    const auto selected=static_cast<Frontend::SpriteFilter>(filter);const auto factor=Frontend::sprite_factor(selected);
                    const auto expected=throttle_cpu.compose(flame_background,flame_foreground,320,224,selected,planes.protected_pixels);
                    SDL_SetRenderTarget(renderer,throttle_target);
                    auto* texture=throttle_gpu->compose(planes.background,planes.sprites,320,224,selected,planes.protected_pixels,scene.heads,scene.fragments,255,255,255,Presentation::afterburner_opacity);
                    SDL_FRect destination{0,0,float(320*factor),float(224*factor)};
                    require(SDL_RenderTexture(renderer,texture,nullptr,&destination),"throttle GPU draw failed");
                    SDL_Rect area{0,0,int(320*factor),int(224*factor)};auto* image=SDL_RenderReadPixels(renderer,&area);require(image,"throttle GPU readback failed");
                    auto* argb=SDL_ConvertSurface(image,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(image);require(argb,"throttle GPU conversion failed");
                    for(unsigned y=0;y<224*factor;++y)for(unsigned x=0;x<320*factor;++x)
                        require(reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(argb->pixels)+y*argb->pitch)[x]==expected[y*320*factor+x],"native full-throttle GPU flame differs from CPU reference");
                    SDL_DestroySurface(argb);++gpu_flame_checks;
                }
                SDL_SetRenderTarget(renderer,nullptr);
            }
        }
        SDL_DestroyTexture(throttle_target);
        std::cout<<"Flame white cores: "<<white_cores<<" translucent, "<<opaque_body_white<<" opaque aircraft white pixels; GPU checks="<<gpu_flame_checks<<".\n";
        require(white_cores && opaque_body_white && (!gpu || gpu_flame_checks==6),"flame core/body/GPU coverage missing");
        std::cout<<"Throttle flame: "<<full_flames<<" full-throttle fragments, "<<ordinary_candidates<<" opaque ordinary-throttle candidates, "<<full_changed<<" changed pixels.\n";
        require(full_flames && ordinary_candidates && full_changed,"throttle scene did not cover ordinary and full-throttle flame pixels");
    }
    if(gpu) {
        // Actual native boot/launch planes supplement synthetic shader fixtures.
        GameSession flight(roms);flight.capture_sprite_scene(true);
        Frontend::SpriteScaler cpu;Frontend::GPUSpriteScaler scaler(renderer);
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,960,672);
        require(target,"GPU comparison target unavailable");
        require(SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED),"disable logical presentation failed");
        unsigned comparisons=0,cloud_pixels=0,trail_pixels=0,explosion_pixels=0,flame_pixels=0,crash_pixels=0;bool tested_explosion=false,tested_flame=false,tested_ordinary_flame=false;unsigned visible_explosion_changes=0,visible_crash_changes=0,image_crash_changes=0;
        for(unsigned f=0;f<3601;++f) {
            GameControls input;input.coin=f>=360 && f<372;input.start=f>=420 && f<432;
            if(f>=600){input.stick_x=f%120<60?0.6f:-0.6f;input.cannon=true;input.missile=f%90<12;}
            require(flight.advance(input),"native GPU advance failed");flight.take_audio();
            const auto scene=flight.sprite_scene();require(scene.ready,"sprite scene unavailable");
            bool has_explosion=false,has_flame=false,has_flame_candidate=false;
            for(const auto& fragment:scene.fragments){
                cloud_pixels+=fragment.kind==1;trail_pixels+=fragment.kind==2;explosion_pixels+=fragment.kind==5;flame_pixels+=fragment.kind==6;
                has_explosion|=fragment.kind==5;has_flame|=fragment.kind==6;
                const auto color=(fragment.raw>>4)&255;
                if(fragment.kind==5 && (color==0xa2 || color==0x75 || color==0x2c))++crash_pixels;
                has_flame_candidate|=color==0xf2 && ((fragment.raw&15)==1 || (fragment.raw&15)==2 || (fragment.raw&15)==4);
                if(fragment.kind==6)require(((fragment.raw>>4)&255)==0x80,"flame classification included aircraft body");
            }
            if(f%90 && !(has_explosion && !tested_explosion) && !(has_flame && !tested_flame) && !(has_flame_candidate && !tested_ordinary_flame))continue;
            tested_explosion|=has_explosion;tested_flame|=has_flame;tested_ordinary_flame|=has_flame_candidate;
            const auto planes=flight.sprite_presentation();require(planes.ready,"native sprite planes unavailable");
            const auto retain=[&](const std::string& path) {
                if(std::filesystem::exists(path))return;
                std::filesystem::create_directories(std::filesystem::path(path).parent_path());
                std::ofstream fixture(path,std::ios::binary);
                fixture.write(reinterpret_cast<const char*>(planes.background.data()),planes.background.size_bytes());
                fixture.write(reinterpret_cast<const char*>(planes.sprites.data()),planes.sprites.size_bytes());
                fixture.write(reinterpret_cast<const char*>(planes.protected_pixels.data()),planes.protected_pixels.size_bytes());
                require(bool(fixture),"cannot retain GPU native fixture");
            };
            if(f==600)retain("build/oracle/gpu-native-planes.bin");
            std::vector<uint32_t> original_foreground(320*224),effect_background;
            Frontend::SpriteScaler baseline;
            for(unsigned pixel=0;pixel<320*224;++pixel)original_foreground[pixel]=Presentation::foreground(scene.fragments,scene.heads[pixel],255,255);
            const auto original_view=cpu.compose(planes.background,original_foreground,320,224,Frontend::SpriteFilter::none,planes.protected_pixels);
            const std::vector<uint32_t> original(original_view.begin(),original_view.end());
            for(unsigned pixel=0;pixel<320*224;++pixel){const auto rgb=flight.frame().pixels[pixel];require(original[pixel]==(0xff000000u|uint32_t(rgb.r)<<16|uint32_t(rgb.g)<<8|rgb.b),"scene decoder differs from original native frame");}
            const auto before=flight.info();
            for(unsigned policy=0;policy<16;++policy)for(unsigned i=0;i<Frontend::sprite_filter_count;++i) {
                const unsigned clouds=policy&1?160:255,trails=policy&2?112:255,explosions=policy&4?Presentation::explosion_opacity:255,afterburner=policy&8?Presentation::afterburner_opacity:255;
                effect_background.assign(planes.background.begin(),planes.background.end());
                for(unsigned pixel=0;pixel<320*224;++pixel)original_foreground[pixel]=Presentation::foreground(scene.fragments,scene.heads[pixel],clouds,trails,&effect_background[pixel],explosions,afterburner);
                const auto filter=static_cast<Frontend::SpriteFilter>(i);
                const auto factor=Frontend::sprite_factor(filter);
                SDL_SetRenderTarget(renderer,target);
                auto* filtered=scaler.compose(planes.background,planes.sprites,320,224,filter,planes.protected_pixels,scene.heads,scene.fragments,clouds,trails,explosions,afterburner);
                const auto expected=cpu.compose(effect_background,original_foreground,320,224,filter,planes.protected_pixels);
                if(policy==4 && i==0) {
                    unsigned changed=0,crash_changed=0;
                    for(unsigned pixel=0;pixel<320*224;++pixel)if(expected[pixel]!=original[pixel]) {
                        ++changed;const auto head=scene.heads[pixel];
                        if(head!=UINT32_MAX && scene.fragments[head].kind==5) {
                            const auto color=(scene.fragments[head].raw>>4)&255;
                            if(color==0xa2 || color==0x75 || color==0x2c)++crash_changed;
                        }
                    }
                    visible_explosion_changes+=changed;visible_crash_changes+=crash_changed;
                    if(crash_changed>image_crash_changes) {
                        image_crash_changes=crash_changed;
                        const auto save=[&](const char* path,std::span<const uint32_t> pixels){std::ofstream file(path,std::ios::binary);file<<"P6\n320 224\n255\n";for(auto color:pixels){file.put(color>>16);file.put(color>>8);file.put(color);}require(bool(file),"effect comparison image save failed");};
                        save("build/oracle/crash-opaque.ppm",original);save("build/oracle/crash-translucent.ppm",expected);
                    }
                }
                if(policy==0){const auto old=baseline.compose(planes.background,planes.sprites,320,224,filter,planes.protected_pixels);require(std::equal(old.begin(),old.end(),expected.begin()),"opaque individual composition changed existing scaler output");}
                SDL_FRect destination{0,0,float(320*factor),float(224*factor)};
                require(SDL_RenderTexture(renderer,filtered,nullptr,&destination),"native GPU comparison draw failed");
                SDL_Rect area{0,0,int(320*factor),int(224*factor)};
                auto* read=SDL_RenderReadPixels(renderer,&area);require(read,"native GPU readback failed");
                auto* argb=SDL_ConvertSurface(read,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(read);require(argb,"native GPU conversion failed");
                for(unsigned y=0;y<224*factor;++y)for(unsigned x=0;x<320*factor;++x) {
                    const auto actual=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(argb->pixels)+y*argb->pitch)[x];
                    if(actual!=expected[y*320*factor+x]) {
                        std::cerr<<"Native GPU mismatch frame="<<f<<" policy="<<policy<<" filter="<<i<<" pixel="<<x<<','<<y<<" GPU="<<std::hex<<actual<<" CPU="<<expected[y*320*factor+x]<<std::dec<<'\n';
                        retain("build/oracle/gpu-native-planes-"+std::to_string(f)+"-"+std::to_string(policy)+"-"+std::to_string(i)+".bin");
                        throw std::runtime_error("GPU native presentation differs from CPU reference");
                    }
                }
                SDL_DestroySurface(argb);++comparisons;
            }
            require(flight.info().number==before.number && flight.info().time_ns==before.time_ns,"GPU filtering advanced native state");
        }
        SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(target);
        std::cout<<"Native effect pixels observed: clouds="<<cloud_pixels<<" trails="<<trail_pixels<<" explosions="<<explosion_pixels<<" flame="<<flame_pixels<<'\n';
        require(cloud_pixels && trail_pixels && explosion_pixels && tested_explosion,"native effect classification did not exercise clouds, trails and explosions");
        require(!flame_pixels && !tested_flame && tested_ordinary_flame,"retained ordinary-throttle corpus applied flame translucency");
        require(crash_pixels && visible_explosion_changes && visible_crash_changes,"explosion setting did not visibly alter target and player-crash fire");
        std::cout<<"Explosion changes: "<<visible_explosion_changes<<" visible pixels, "<<visible_crash_changes<<" player-crash pixels; crash fire fragments="<<crash_pixels<<".\n";

        std::cout<<"Native GPU sprite planes: "<<comparisons<<" of "<<comparisons<<" identical, 0 skipped.\n";
    }
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);if(device)SDL_DestroyGPUDevice(device);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Native menu passed: pause/resume and prior pause, frame/time isolation, navigation, SDL filtering/integer/fullscreen, stats, rollback, Start and Quit.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}

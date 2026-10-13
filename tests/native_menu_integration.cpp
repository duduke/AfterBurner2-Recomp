#include "frontend/native_menu.hpp"
#include "engine/physical_recording.hpp"
#include "frontend/gpu_sprite_filter.hpp"
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv) try {
    const bool gpu=argc==2 && std::string_view(argv[1])=="--gpu";
    require(roms.load_aburner2(),"ROMs required");
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD),SDL_GetError());
    auto* window=SDL_CreateWindow("Menu integration",960,672,0);
    require(window,"window unavailable");
    auto* device=gpu?SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,true,nullptr):nullptr;
    require(!gpu || device,"GPU device unavailable");
    auto* renderer=gpu?SDL_CreateGPURenderer(device,window):SDL_CreateRenderer(window,"software");require(renderer,"renderer unavailable");
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224);
    require(texture,"texture unavailable");
    {
        GameSession game(roms);require(game.advance({}),"initial game advance failed");game.take_audio();
        const auto before=game.info();const auto source=game.frame().pixels;
        const std::vector<RGB888> pixels(source.begin(),source.end());
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());
        menu.apply=[&](const Frontend::Settings& s){return Frontend::apply_settings(window,renderer,texture,s);};
        menu.open(game);require(game.paused(),"opening menu did not freeze board");
        require(!game.advance({}) && game.info().number==before.number,"menu admitted a physical board frame");
        menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);require(menu.in_options(),"options navigation failed");
        const auto root_settings=menu.settings;menu.key(SDLK_LEFT);menu.key(SDLK_RIGHT);
        require(menu.settings==root_settings,"Options root changed a video setting");
        menu.key(SDLK_RETURN);require(menu.in_video(),"Video submenu entry failed");
        menu.key(SDLK_LEFT);menu.key(SDLK_RIGHT); // wrap filter selection without invalid enum
        menu.key(SDLK_RIGHT);require(menu.settings.scaling==Frontend::Scaling::linear,"smooth scaling selection failed");
        SDL_ScaleMode scale{};require(SDL_GetTextureScaleMode(texture,&scale) && scale==SDL_SCALEMODE_LINEAR,"SDL filter not applied");
        menu.key(SDLK_RIGHT);
        SDL_RendererLogicalPresentation presentation{};int w,h;
        require(SDL_GetRenderLogicalPresentation(renderer,&w,&h,&presentation) &&
            presentation==SDL_LOGICAL_PRESENTATION_INTEGER_SCALE && w==320 && h==224,"integer presentation not applied");
        menu.key(SDLK_DOWN);
        menu.key(SDLK_LEFT);require(menu.settings.sprites==Frontend::SpriteFilter::scalefx,"sprite filter reverse wrap failed");
        menu.key(SDLK_RIGHT);require(menu.settings.sprites==Frontend::SpriteFilter::none,"sprite filter forward wrap failed");
        for(int i=1;i<Frontend::sprite_filter_count;++i) {
            menu.key(SDLK_RIGHT);require(static_cast<int>(menu.settings.sprites)==i,"sprite filter selection failed");
        }
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.clouds && !menu.settings.trails,"cloud toggle failed");menu.key(SDLK_LEFT);
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.trails && !menu.settings.clouds,"trail toggle failed");menu.key(SDLK_LEFT);
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.explosions && !menu.settings.afterburner,"explosion toggle failed");menu.key(SDLK_LEFT);
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.afterburner && !menu.settings.explosions,"afterburner toggle failed");menu.key(SDLK_LEFT);
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);
        require(menu.settings.fullscreen && (SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN),"fullscreen not applied");
        menu.key(SDLK_LEFT);require(!menu.settings.fullscreen,"windowed restoration failed");
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);menu.key(SDLK_RIGHT);
        require(menu.settings.stats==Frontend::Stats::detailed,"stats mode selection failed");
        menu.draw();menu.draw_stats({59.6,59.6,16.8,12,1},true);SDL_RenderPresent(renderer);
        require(game.info().number==before.number && game.info().time_ns==before.time_ns &&
            std::memcmp(pixels.data(),game.frame().pixels.data(),pixels.size()*sizeof(RGB888))==0,
            "UI rendering mutated native frame/time");
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(menu.settings.ground,"ground correction UI failed");menu.draw(); // Ground scrolls the Video viewport.
        menu.pointer(60,65,false); // Hover its first visible row without shifting it.
        const auto prior_filter=menu.settings.sprites;menu.key(SDLK_RIGHT);
        require(menu.settings.sprites!=prior_filter,"scrolled mouse row selected the wrong option");
        const auto accepted=menu.settings;menu.apply=[](const auto&){return false;};menu.key(SDLK_RIGHT);
        require(menu.settings==accepted,"failed option must retain last accepted settings");
        menu.notice.clear();
        menu.key(SDLK_ESCAPE);require(!menu.in_video() && menu.in_options() && menu.selection()==0,"Video Escape failed");
        menu.key(SDLK_RETURN);menu.key(SDLK_UP);require(menu.selection()==9,"Video reverse navigation failed");
        menu.key(SDLK_RETURN);require(!menu.in_video() && menu.in_options(),"Video Back failed");
        menu.pointer(60,104,true);require(menu.in_video(),"Video pointer entry failed");
        menu.draw();auto* video_image=SDL_RenderReadPixels(renderer,nullptr);require(video_image,"Video menu readback failed");
        require(SDL_SaveBMP(video_image,"build/oracle/video-menu.bmp"),"Video image save failed");SDL_DestroySurface(video_image);
        menu.key(SDLK_ESCAPE);
        menu.key(SDLK_ESCAPE);require(!menu.in_options(),"options Back failed");
        require(menu.key(SDLK_ESCAPE)==Frontend::NativeMenu::Action::close,"home Escape failed");
        menu.close(game);require(!game.paused() && game.advance({}),"menu resume failed");
        game.set_paused(true);menu.open(game);menu.close(game);require(game.paused(),"menu lost preexisting pause");
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::quit,"menu quit action failed");menu.close(game);
        GameSession cold(roms);menu.open(cold);
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::start,"cold menu Start action failed");menu.close(cold);
    }
    {
        GameSession game(roms);game.advance({});const auto before=game.info();
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
        menu.key(SDLK_DOWN);
        menu.key(SDLK_RETURN);require(menu.in_dips(),"DIP submenu entry failed");
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
        menu.key(SDLK_DOWN);require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::restart,"Apply & Restart action missing");
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
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
        menu.key(SDLK_ESCAPE);require(menu.in_options() && menu.selection()==1,"DIP Escape did not restore Options row");
        menu.key(SDLK_RETURN);menu.pointer(40,164,true);require(!menu.in_dips(),"DIP pointer Back failed");menu.close(game);
    }
    {
        Frontend::NativeInput input("");Frontend::NativeHaptics feedback(input,"");
        GameSession game(roms);Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.input=&input;menu.feedback=&feedback;
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
        for(int i=0;i<16;++i)menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
        require(menu.selection()==0,"Feedback submenu entry");menu.key(SDLK_RETURN);require(feedback.settings.enabled,"Feedback enabled UI");
        for(int row=0;row<16;++row){menu.draw();SDL_RenderPresent(renderer);menu.key(SDLK_DOWN);}
        menu.key(SDLK_ESCAPE);require(menu.in_controls()&&menu.selection()==16,"Feedback Escape navigation");menu.key(SDLK_RETURN);
        menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);require(feedback.settings.body==60,"Feedback strength UI");
        menu.close(game);require(!menu.is_open(),"Feedback menu close");
    }
    {
        const auto config=(std::filesystem::temp_directory_path()/"ab2-menu-input.cfg").string();
        Frontend::NativeInput input(config);
        GameSession game(roms);Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.input=&input;
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
        menu.key(SDLK_DOWN);
        menu.key(SDLK_RETURN);require(menu.in_controls(),"Controls entry unavailable");
        for(int i=0;i<4;++i)menu.key(SDLK_DOWN);
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
        menu.key(SDLK_ESCAPE);require(!menu.in_controls() && menu.in_options(),"Controls back navigation");
        menu.close(game);std::filesystem::remove(config);
    }
    {
        const auto folder=std::filesystem::path("build/oracle/music-fixtures");
        const auto config=(folder/"menu-audio.cfg").string();std::filesystem::remove(config);
        Frontend::NativeMusic music(config,folder);GameSession game(roms);
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.music=&music;
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
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
        const auto before=game.info();require(!game.advance({}) && game.info().time_ns==before.time_ns,"audio menu advanced native board");
        menu.close(game);std::filesystem::remove(config);
        const auto input_config=(folder/"menu-navigation.inputs").string();
        Frontend::NativeInput input(input_config);
        for(unsigned present=0;present<4;++present) {
            Frontend::NativeMenu navigation(renderer,roms,{},Frontend::pixel_art_available());
            navigation.input=present&1?&input:nullptr;navigation.music=present&2?&music:nullptr;
            navigation.open(game);navigation.key(SDLK_DOWN);navigation.key(SDLK_RETURN);
            navigation.pointer(60,104,true);require(navigation.in_video(),"optional menu layout lost Video entry");
            navigation.key(SDLK_ESCAPE);
            const int dip_row=1+bool(present&1)+bool(present&2);
            navigation.pointer(60,104+dip_row*20,true);require(navigation.in_dips(),"optional menu layout lost DIP entry");
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

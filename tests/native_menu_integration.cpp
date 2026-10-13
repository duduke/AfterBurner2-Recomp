#include "frontend/native_menu.hpp"
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
        menu.key(SDLK_DOWN);menu.draw(); // Back scrolls the Options viewport.
        menu.pointer(60,104,false); // Hover its first visible row without shifting it.
        const auto prior_filter=menu.settings.sprites;menu.key(SDLK_RIGHT);
        require(menu.settings.sprites!=prior_filter,"scrolled mouse row selected the wrong option");
        const auto accepted=menu.settings;menu.apply=[](const auto&){return false;};menu.key(SDLK_RIGHT);
        require(menu.settings==accepted,"failed option must retain last accepted settings");
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
        const auto config=(std::filesystem::temp_directory_path()/"ab2-menu-input.cfg").string();
        Frontend::NativeInput input(config);
        GameSession game(roms);Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());menu.input=&input;
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
        for(int i=0;i<8;++i)menu.key(SDLK_DOWN);
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
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);for(int i=0;i<8;++i)menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);
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
    }
    if(gpu) {
        // Actual native boot/launch planes supplement synthetic shader fixtures.
        GameSession flight(roms);flight.capture_sprite_scene(true);
        Frontend::SpriteScaler cpu;Frontend::GPUSpriteScaler scaler(renderer);
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,960,672);
        require(target,"GPU comparison target unavailable");
        require(SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED),"disable logical presentation failed");
        unsigned comparisons=0,cloud_pixels=0,trail_pixels=0,explosion_pixels=0,flame_pixels=0;bool tested_explosion=false,tested_flame=false;
        for(unsigned f=0;f<3601;++f) {
            GameControls input;input.coin=f>=360 && f<372;input.start=f>=420 && f<432;
            if(f>=600){input.stick_x=f%120<60?0.6f:-0.6f;input.cannon=true;input.missile=f%90<12;}
            require(flight.advance(input),"native GPU advance failed");flight.take_audio();
            const auto scene=flight.sprite_scene();require(scene.ready,"sprite scene unavailable");
            bool has_explosion=false,has_flame=false;
            for(const auto& fragment:scene.fragments){
                cloud_pixels+=fragment.kind==1;trail_pixels+=fragment.kind==2;explosion_pixels+=fragment.kind==5;flame_pixels+=fragment.kind==6;
                has_explosion|=fragment.kind==5;has_flame|=fragment.kind==6;
                if(fragment.kind==6)require(((fragment.raw>>4)&255)==0xf2 && ((fragment.raw&15)==1 || (fragment.raw&15)==2 || (fragment.raw&15)==4),"flame classification included aircraft body");
            }
            if(f%90 && !(has_explosion && !tested_explosion) && !(has_flame && !tested_flame))continue;
            tested_explosion|=has_explosion;tested_flame|=has_flame;
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
            const auto original=cpu.compose(planes.background,original_foreground,320,224,Frontend::SpriteFilter::none,planes.protected_pixels);
            for(unsigned pixel=0;pixel<320*224;++pixel){const auto rgb=flight.frame().pixels[pixel];require(original[pixel]==(0xff000000u|uint32_t(rgb.r)<<16|uint32_t(rgb.g)<<8|rgb.b),"scene decoder differs from original native frame");}
            const auto before=flight.info();
            for(unsigned policy=0;policy<16;++policy)for(unsigned i=0;i<Frontend::sprite_filter_count;++i) {
                const unsigned clouds=policy&1?160:255,trails=policy&2?112:255,explosions=policy&4?176:255,afterburner=policy&8?192:255;
                effect_background.assign(planes.background.begin(),planes.background.end());
                for(unsigned pixel=0;pixel<320*224;++pixel)original_foreground[pixel]=Presentation::foreground(scene.fragments,scene.heads[pixel],clouds,trails,&effect_background[pixel],explosions,afterburner);
                const auto filter=static_cast<Frontend::SpriteFilter>(i);
                const auto factor=Frontend::sprite_factor(filter);
                SDL_SetRenderTarget(renderer,target);
                auto* filtered=scaler.compose(planes.background,planes.sprites,320,224,filter,planes.protected_pixels,scene.heads,scene.fragments,clouds,trails,explosions,afterburner);
                const auto expected=cpu.compose(effect_background,original_foreground,320,224,filter,planes.protected_pixels);
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
        require(cloud_pixels && trail_pixels && explosion_pixels && flame_pixels && tested_explosion && tested_flame,"native effect classification did not exercise clouds, trails, explosions and player flame");

        std::cout<<"Native GPU sprite planes: "<<comparisons<<" of "<<comparisons<<" identical, 0 skipped.\n";
    }
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);if(device)SDL_DestroyGPUDevice(device);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Native menu passed: pause/resume and prior pause, frame/time isolation, navigation, SDL filtering/integer/fullscreen, stats, rollback, Start and Quit.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}

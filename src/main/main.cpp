// AfterBurner2-Recomp
//
// Application entry point for the literal joint X-Board host.
// Physical frame/CPU/device clocks are independent of display refresh.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <iostream>
#include <charconv>
#include <string_view>
#include <string>
#include <vector>
#include "roms.hpp"
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/screen.hpp"
#include "engine/game_session.hpp"
#include "engine/physical_recording.hpp"
#include "frontend/native_menu.hpp"
#include "hwvideo/wide_scene.hpp"
#include "frontend/gpu_sprite_filter.hpp"
#include "frontend/controller_database.hpp"

namespace
{
    constexpr int SCREEN_WIDTH = GameSession::width;
    constexpr int SCREEN_HEIGHT = GameSession::height;

}

int main(int argc, char** argv)try
{
    unsigned frame_limit = 0;
    unsigned start_stage = 0;
    bool boot_to_flight = false, profile = false;
    std::string recording_path;
    std::string settings_path, menu_preview, frame_preview;
    for (int i=1;i<argc;++i)
    {
        const std::string_view option(argv[i]);
        if (option == "--boot-to-flight") { boot_to_flight=true; continue; }
        if (option == "--profile") { profile=true; continue; }
        if (option == "--record" && i+1<argc) { recording_path=argv[++i]; continue; }
        if (option == "--settings-file" && i+1<argc) { settings_path=argv[++i]; continue; }
        if (option == "--frame-preview" && i+1<argc) { frame_preview=argv[++i]; continue; }
        if (option == "--menu-preview" && i+1<argc) { menu_preview=argv[++i]; continue; }
        if ((option != "--frames" && option != "--stage") || i+1>=argc)
        { std::cerr << "Usage: afterburner2 [--frames positive-count] [--stage number] [--boot-to-flight] [--record path] [--profile] [--settings-file path] [--menu-preview image.bmp] [--frame-preview image.bmp --frames count]\n"; return 1; }
        const std::string_view value(argv[++i]);
        auto& count = option == "--stage" ? start_stage : frame_limit;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !count)
        { std::cerr << (option == "--stage" ? "Stage" : "Frame count") << " must be a positive integer.\n"; return 1; }
    }
    if(!frame_preview.empty() && !frame_limit) throw std::runtime_error("Frame preview requires --frames");
    unsigned simulated_frames = 0;
    if (!recording_path.empty() && start_stage)
        throw std::runtime_error("Natural recording requires normal cold boot without --stage");
    if (!roms.load_aburner2())
    {
        std::cerr << "Failed to load one or more ROM files from roms/ "
                     "(see roms/README.md)." << std::endl;
        return 1;
    }
    std::cout << "ROM set loaded OK." << std::endl;
    if (start_stage && start_stage > NativeStageSkip::stage_count(roms))
        throw std::runtime_error("Stage number out of range (1.." + std::to_string(NativeStageSkip::stage_count(roms)) + ")");

    GameSession game(roms);game.capture_music(true);
    std::unique_ptr<Frontend::NativeMusic> music_player;
    auto take_audio=[&]{auto samples=game.take_audio();auto chunks=game.take_music();return music_player?music_player->mix(std::move(samples),std::move(chunks)):samples;};
    unsigned recording_session=0;
    std::unique_ptr<PhysicalRecording> recording;
    auto advance = [&](const GameControls& input) {
        if (recording && !game.paused()) recording->append(game.info().number,input);
        return game.advance(input);
    };
    auto startup = [&] {
        // Original cold boot with the same physical coin/start pulses as the
        // verified bridge. Never seed gameplay state or skip board frames.
        for(unsigned f=0;f<600;++f) {
            GameControls input;
            input.coin=f>=360&&f<372; input.start=f>=420&&f<432;
            advance(input);
            take_audio();game.take_feedback();
        }
        if (start_stage) {
            game.debug_skip_to_stage(start_stage);if(music_player)music_player->mix({},game.take_music());
            std::cout << "Debug stage skip: entered stage " << start_stage << std::endl;
        }
    };

    const size_t frame_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    std::vector<uint32_t> texture_pixels(frame_pixels);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (settings_path.empty()) {
        if (char* path=SDL_GetPrefPath("AfterBurner2-Recomp","AfterBurner2")) {
            settings_path=std::string(path)+"settings.cfg"; SDL_free(path);
        }
    }
    auto settings=Frontend::load_settings(settings_path,Frontend::pixel_art_available());

    constexpr int SCALE = 3;
    SDL_Window* window = SDL_CreateWindow(
        "AfterBurner2-Recomp",
        Frontend::viewport_width(settings) * SCALE, SCREEN_HEIGHT * SCALE,
        SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GPUDevice* gpu_device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,false,nullptr);
    SDL_Renderer* renderer=gpu_device?SDL_CreateGPURenderer(gpu_device,window):nullptr;
    if(!renderer) {
        std::cerr<<"SDL GPU unavailable; using CPU/software presentation: "<<SDL_GetError()<<'\n';
        if(gpu_device)SDL_DestroyGPUDevice(gpu_device);
        gpu_device=nullptr;
        renderer=SDL_CreateRenderer(window,"software");
    }
    if (!renderer)
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // SDL3 selects the available backend; software/dummy renderers may not
    // support vsync. Physical board pacing remains independent of this setting.
    SDL_SetRenderVSync(renderer, 1);
    if(profile) {
        int vsync=0; SDL_GetRenderVSync(renderer, &vsync);
        std::cout<<"Native renderer: "<<SDL_GetRendererName(renderer)
                 <<" backend="<<(gpu_device?SDL_GetGPUDeviceDriver(gpu_device):"software")
                 <<" SDL="<<SDL_GetVersion()<<" vsync="<<vsync<<'\n';
    }

    if (!SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX))
        throw std::runtime_error(std::string("SDL logical presentation failed: ") + SDL_GetError());

    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!texture)
    {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // SDL3 defaults to linear filtering; retain the arcade pixel presentation.
    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
        throw std::runtime_error(std::string("SDL texture scaling failed: ") + SDL_GetError());

    music_player=std::make_unique<Frontend::NativeMusic>(settings_path.empty()?"":settings_path+".audio");
    Frontend::load_controller_database();
    auto input_manager=std::make_unique<Frontend::NativeInput>(settings_path.empty()?"":settings_path+".inputs");
    SDL_InitSubSystem(SDL_INIT_HAPTIC); // Optional joystick haptic fallback.
    auto feedback=std::make_unique<Frontend::NativeHaptics>(*input_manager,settings_path.empty()?"":settings_path+".haptics");
    game.capture_feedback(feedback->settings.enabled);
    game.set_dip_switches(Frontend::dip_switches(settings));
    if (!recording_path.empty()) recording=std::make_unique<PhysicalRecording>(recording_path,roms,game.dip_switches());
    if (!Frontend::apply_settings(window,renderer,texture,settings)) {
        std::cerr << "Saved presentation settings unavailable: " << SDL_GetError() << '\n';
        const auto lives=settings.lives,difficulty=settings.difficulty;
        settings={};settings.lives=lives;settings.difficulty=difficulty;
        if(!Frontend::apply_settings(window,renderer,texture,settings)) throw std::runtime_error(SDL_GetError());
    }
    Frontend::SpriteScaler sprite_scaler;
    std::vector<uint32_t> effect_pixels,effect_background;
    std::unique_ptr<Frontend::GPUSpriteScaler> gpu_scaler;
    if(gpu_device)gpu_scaler=std::make_unique<Frontend::GPUSpriteScaler>(renderer);
    std::unique_ptr<Presentation::WideScene> wide_renderer;
    SDL_Texture* sprite_texture=nullptr;
    unsigned sprite_texture_factor=0;
    uint64_t last_sprite_frame=UINT64_MAX;
    const auto configure_sprite_texture=[&](const Frontend::Settings& value) {
        if(value.widescreen && !wide_renderer)wide_renderer=std::make_unique<Presentation::WideScene>(roms);
        if(!value.widescreen)wide_renderer.reset();game.capture_wide_scene(value.widescreen);

        if(gpu_scaler) {last_sprite_frame=UINT64_MAX;return true;}
        if(value.sprites==Frontend::SpriteFilter::none && !value.clouds && !value.trails && !value.explosions && !value.afterburner && !value.ground && !value.widescreen) return true;
        const auto factor=Frontend::sprite_factor(value.sprites);
        auto* next=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,Frontend::viewport_width(value)*factor,SCREEN_HEIGHT*factor);
        if(!next || !Frontend::apply_settings(window,renderer,next,value)) {SDL_DestroyTexture(next);return false;}
        SDL_DestroyTexture(sprite_texture);sprite_texture=next;sprite_texture_factor=factor;last_sprite_frame=UINT64_MAX;
        return true;
    };
    if(!configure_sprite_texture(settings)) {
        std::cerr<<"Saved sprite filter unavailable: "<<SDL_GetError()<<'\n';
        settings.sprites=Frontend::SpriteFilter::none;
    }
    game.capture_ground_horizon(settings.ground&&!settings.widescreen);
    game.capture_sprite_scene(!settings.widescreen && (settings.clouds || settings.trails || settings.explosions || settings.afterburner));
    game.capture_sprite_presentation(!settings.widescreen && (gpu_scaler || settings.sprites!=Frontend::SpriteFilter::none || settings.clouds || settings.trails || settings.explosions || settings.afterburner || settings.ground));
    auto menu=std::make_unique<Frontend::NativeMenu>(renderer,roms,settings,Frontend::pixel_art_available());
    menu->feedback=feedback.get();menu->input=input_manager.get();menu->music=music_player.get();
    menu->apply=[&](const Frontend::Settings& changed) {
        const auto previous=menu->settings;
        if(!Frontend::apply_settings(window,renderer,texture,changed)) {
            std::cerr << "Presentation setting unavailable: " << SDL_GetError() << '\n';
            Frontend::apply_settings(window,renderer,texture,previous); return false;
        }
        if(!configure_sprite_texture(changed)) {
            configure_sprite_texture(previous);Frontend::apply_settings(window,renderer,texture,previous);
            return false;
        }
        game.capture_ground_horizon(changed.ground&&!changed.widescreen);
        game.capture_sprite_scene(!changed.widescreen && (changed.clouds || changed.trails || changed.explosions || changed.afterburner));
        game.capture_sprite_presentation(!changed.widescreen && (gpu_scaler || changed.sprites!=Frontend::SpriteFilter::none || changed.clouds || changed.trails || changed.explosions || changed.afterburner || changed.ground));
        if(changed.widescreen!=previous.widescreen&&!changed.fullscreen){
            int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
            SDL_SetWindowSize(window,int(std::lround(wh*Frontend::viewport_width(changed)/224.0)),wh);
            // Refresh the resized GPU swapchain before submitting the new menu.
            SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderDrawColor(renderer,0,0,0,255);
            SDL_RenderClear(renderer);SDL_RenderPresent(renderer);
        }
        SDL_Rect safe_area{int((Frontend::viewport_width(changed)-SCREEN_WIDTH)/2),0,SCREEN_WIDTH,SCREEN_HEIGHT};SDL_SetRenderViewport(renderer,&safe_area);
        menu->notice.clear();
        if(!Frontend::save_settings(settings_path,changed)) {
            menu->notice="SETTINGS NOT SAVED";
            std::cerr << "Cannot save frontend settings to " << settings_path << '\n';
        }
        return true;
    };
    SDL_Rect initial_safe_area{int((Frontend::viewport_width(settings)-SCREEN_WIDTH)/2),0,SCREEN_WIDTH,SCREEN_HEIGHT};SDL_SetRenderViewport(renderer,&initial_safe_area);
    if(!menu_preview.empty()) {
        menu->open(game);menu->key(SDLK_DOWN);menu->key(SDLK_RETURN);menu->draw();
        SDL_SetRenderViewport(renderer,nullptr);
        SDL_Surface* preview=SDL_RenderReadPixels(renderer,nullptr);
        if(!preview || !SDL_SaveBMP(preview,menu_preview.c_str())) throw std::runtime_error(SDL_GetError());
        SDL_DestroySurface(preview);menu.reset();feedback.reset();input_manager.reset();music_player.reset();if(!gpu_scaler)SDL_DestroyTexture(sprite_texture);gpu_scaler.reset();SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);if(gpu_device)SDL_DestroyGPUDevice(gpu_device);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }
    if(!frame_limit && !boot_to_flight && !start_stage) menu->open(game);

    auto audio_output=std::make_unique<Frontend::AudioOutput>(settings_path.empty()?"":settings_path+".output");
    menu->audio=audio_output.get();
    bool cockpit_enabled=audio_output->settings.profile!=Frontend::AudioProfile::original;
    game.capture_cockpit(cockpit_enabled);
    if (boot_to_flight || start_stage) startup();
    music_player->output=audio_output.get();

    const double tick_seconds = GameSession::frame_seconds;
    const double counter_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    uint64_t previous_counter = SDL_GetPerformanceCounter();
    uint64_t profile_start = previous_counter;
    unsigned profile_frames=0, profile_displays=0;
    double profile_core=0, profile_present=0, profile_filter=0, profile_scene=0;
    unsigned filter_frames=0;
    Frontend::Performance performance;
    double accumulated_seconds = tick_seconds;
    bool running = true;
    auto reset_pacing=[&] {
        accumulated_seconds=0; previous_counter=SDL_GetPerformanceCounter();
        profile_start=previous_counter;profile_frames=profile_displays=0;profile_core=profile_present=0;
        performance={};last_sprite_frame=UINT64_MAX;profile_filter=0;profile_scene=0;filter_frames=0;
    };
    auto sync_audio=[&] {audio_output->pause(game.paused());};
    sync_audio();
    auto restart_board=[&] {
        if(wide_renderer)wide_renderer->reset();feedback->restart();input_manager->block();music_player->restarted();game.restart();
        game.set_dip_switches(Frontend::dip_switches(menu->settings));
        if(recording)recording=std::make_unique<PhysicalRecording>(recording_path+".restart"+std::to_string(++recording_session),roms,game.dip_switches());
        if(start_stage)startup();
        audio_output->clear();
        reset_pacing();sync_audio();
        SDL_SetWindowTitle(window,"AfterBurner2-Recomp");
    };
    auto menu_action=[&](Frontend::NativeMenu::Action action) {
        if(action==Frontend::NativeMenu::Action::restart) {menu->close(game);restart_board();return;}
        if(action==Frontend::NativeMenu::Action::quit) {running=false;return;}
        if(action==Frontend::NativeMenu::Action::close || action==Frontend::NativeMenu::Action::start) {
            menu->close(game);
            // Enter the arcade at its current cold-board frame. Only explicit
            // --boot-to-flight/--stage requests supply automatic coin/start.
            input_manager->block();reset_pacing();sync_audio();
            SDL_SetWindowTitle(window,game.paused()?"AfterBurner2-Recomp — Paused (P to resume)":"AfterBurner2-Recomp");
        }
    };
    while (running)
    {
        music_player->poll();music_player->update_preview();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){music_player->stop_preview();audio_output->clear();}
            feedback->event(event);
            if(input_manager->event(event))continue;
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if(menu->is_open()) {
                if(auto key=input_manager->menu_key(event))menu_action(menu->key(key));
                if(event.type==SDL_EVENT_KEY_DOWN && (!event.key.repeat || event.key.key==SDLK_UP || event.key.key==SDLK_DOWN))
                    menu_action(menu->key(event.key.key));
                if(event.type==SDL_EVENT_MOUSE_WHEEL)menu_action(menu->key(event.wheel.y>0?SDLK_UP:SDLK_DOWN));
                if(event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    SDL_ConvertEventToRenderCoordinates(renderer,&event);
                    if(event.type==SDL_EVENT_MOUSE_WHEEL)menu_action(menu->key(event.wheel.y>0?SDLK_UP:SDLK_DOWN));
                if(event.type==SDL_EVENT_MOUSE_MOTION) menu_action(menu->pointer(event.motion.x,event.motion.y,false));
                    else if(event.button.button==SDL_BUTTON_LEFT)menu_action(menu->pointer(event.button.x,event.button.y,true));
                }
                continue;
            }
            if((event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key==SDLK_ESCAPE || event.key.key==SDLK_F1)) || (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button==SDL_GAMEPAD_BUTTON_GUIDE)) {
                menu->open(game);reset_pacing();sync_audio();continue;
            }
            if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key==SDLK_F11) {
                menu->fullscreen_toggle();reset_pacing();continue;
            }
            if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key==SDLK_F3) {
                menu->stats_toggle();reset_pacing();continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F5)
            {
                restart_board();
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P)
            {
                feedback->stop();input_manager->block();game.set_paused(!game.paused());
                sync_audio();
                reset_pacing();
                SDL_SetWindowTitle(window, game.paused() ? "AfterBurner2-Recomp — Paused (P to resume)" : "AfterBurner2-Recomp");
            }
        }

        if (!running) break;
        if(input_manager->menu_pressed()) {
            if(menu->is_open())menu_action(menu->key(SDLK_ESCAPE));
            else {menu->open(game);reset_pacing();sync_audio();}
        }
        const bool wants_cockpit=audio_output->settings.profile!=Frontend::AudioProfile::original;
        if(wants_cockpit!=cockpit_enabled){game.capture_cockpit(wants_cockpit);cockpit_enabled=wants_cockpit;}
        game.capture_feedback(feedback->settings.enabled);
        feedback->update(game.paused() || menu->is_open());
        GameControls input=input_manager->read(menu->is_open());

        if (game.paused())
        {
            previous_counter = SDL_GetPerformanceCounter();
            SDL_Delay(10);
        }
        else
        {
            const uint64_t counter = SDL_GetPerformanceCounter();
            // Bound catch-up after a suspended window or debugger pause.
            accumulated_seconds = std::min(0.25, accumulated_seconds + (counter - previous_counter) / counter_frequency);
            previous_counter = counter;
            if (accumulated_seconds < tick_seconds)
            {
                SDL_Delay(1);
                continue;
            }
        }
        // Keep input polling and presentation responsive when the native
        // board takes longer than real time. Every simulated frame still
        // executes all CPU/device work; wall-time backlog cannot monopolize UI.
        unsigned catchup_frames = 0;
        while (!game.paused() && accumulated_seconds >= tick_seconds && catchup_frames++ < 2)
        {
            const bool measure=profile || menu->settings.stats!=Frontend::Stats::off;
            const auto core_start=measure ? SDL_GetPerformanceCounter() : 0;
            advance(input);
            feedback->consume(game.take_feedback());
            feedback->update(!input_manager->focused);
            if(measure) profile_core+=(SDL_GetPerformanceCounter()-core_start)*1000.0/counter_frequency;
            ++profile_frames;
            // Playback owns bounded queues and never clocks the emulated board.
            take_audio();
            accumulated_seconds -= tick_seconds;
            ++simulated_frames;
            if (frame_limit && simulated_frames >= frame_limit)
            { running = false; break; }
        }

        auto planes=game.sprite_presentation();auto scene=game.sprite_scene();
        unsigned render_width=SCREEN_WIDTH;bool wide_ready=false;
        if(wide_renderer){const auto scene_start=SDL_GetPerformanceCounter();auto captured=game.wide_scene();if(captured.ready)wide_renderer->render(captured.clips,captured.info.number,menu->settings.ground);
            auto wide=wide_renderer->view();wide_ready=wide.ready;
            if(wide_ready){render_width=wide.width;planes={wide.background,wide.sprites,wide.protection,true};scene={wide.heads,wide.fragments,true};}
            else planes.ready=false;
            profile_scene+=(SDL_GetPerformanceCounter()-scene_start)*1000.0/counter_frequency;
        }

        const bool composed=(gpu_scaler || menu->settings.sprites!=Frontend::SpriteFilter::none || menu->settings.clouds || menu->settings.trails || menu->settings.explosions || menu->settings.afterburner || menu->settings.ground || wide_ready) && planes.ready && !menu->is_open();
        if(!composed){
            const auto frame = game.frame().pixels;
            for(size_t i=0;i<frame.size();++i){const auto p=frame[i];texture_pixels[i]=0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;}
            if(!SDL_UpdateTexture(texture,nullptr,texture_pixels.data(),SCREEN_WIDTH*4))throw std::runtime_error(SDL_GetError());
        }
        SDL_SetRenderViewport(renderer,nullptr);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        if(composed) {
            const auto factor=Frontend::sprite_factor(menu->settings.sprites);
            if(!gpu_scaler && (!sprite_texture || sprite_texture_factor!=factor))
                throw std::runtime_error("Sprite texture configuration lost");
            if(last_sprite_frame!=game.info().number) {
                const auto filter_start=SDL_GetPerformanceCounter();

                const unsigned cloud_alpha=menu->settings.clouds?160:255,trail_alpha=menu->settings.trails?112:255,explosion_alpha=menu->settings.explosions?Presentation::explosion_opacity:255,afterburner_alpha=menu->settings.afterburner?Presentation::afterburner_opacity:255;
                if(gpu_scaler) {
                    sprite_texture=gpu_scaler->compose(planes.background,planes.sprites,render_width,SCREEN_HEIGHT,menu->settings.sprites,planes.protected_pixels,scene.heads,scene.fragments,cloud_alpha,trail_alpha,explosion_alpha,afterburner_alpha);
                    if(!Frontend::apply_texture_settings(sprite_texture,menu->settings))throw std::runtime_error(SDL_GetError());
                } else {
                    auto foreground=planes.sprites,background=planes.background;
                    if(scene.ready){effect_background.assign(planes.background.begin(),planes.background.end());effect_pixels.resize(planes.sprites.size());for(size_t i=0;i<effect_pixels.size();++i)effect_pixels[i]=Presentation::foreground(scene.fragments,scene.heads[i],cloud_alpha,trail_alpha,&effect_background[i],explosion_alpha,afterburner_alpha);foreground=effect_pixels;background=effect_background;}
                    const auto pixels=sprite_scaler.compose(background,foreground,render_width,SCREEN_HEIGHT,menu->settings.sprites,planes.protected_pixels);
                    if(!SDL_UpdateTexture(sprite_texture,nullptr,pixels.data(),render_width*factor*4))throw std::runtime_error(SDL_GetError());
                }
                profile_filter+=(SDL_GetPerformanceCounter()-filter_start)*1000.0/counter_frequency;
                ++filter_frames;
                last_sprite_frame=game.info().number;
            }
            SDL_RenderTexture(renderer,sprite_texture,nullptr,nullptr);
        } else {SDL_FRect center{float((Frontend::viewport_width(menu->settings)-SCREEN_WIDTH)/2),0,float(SCREEN_WIDTH),float(SCREEN_HEIGHT)};SDL_RenderTexture(renderer, texture, nullptr, &center);}
        SDL_Rect safe_area{int((Frontend::viewport_width(menu->settings)-SCREEN_WIDTH)/2),0,SCREEN_WIDTH,SCREEN_HEIGHT};
        SDL_SetRenderViewport(renderer,&safe_area);

        if(menu->is_open())menu->draw();
        else menu->draw_stats(performance,game.paused());
        if(!frame_preview.empty() && frame_limit && simulated_frames>=frame_limit) {
            SDL_SetRenderViewport(renderer,nullptr);
            auto* preview=SDL_RenderReadPixels(renderer,nullptr);
            if(!preview || !SDL_SaveBMP(preview,frame_preview.c_str())) throw std::runtime_error(SDL_GetError());
            SDL_DestroySurface(preview);frame_preview.clear();
        }
        const auto present_start=SDL_GetPerformanceCounter();
        SDL_RenderPresent(renderer);
        ++profile_displays;
        {
            const auto now=SDL_GetPerformanceCounter();
            profile_present+=(now-present_start)*1000.0/counter_frequency;
            const double seconds=(now-profile_start)/counter_frequency;
            if(seconds>=1.0) {
                performance={profile_frames/seconds,profile_displays/seconds,seconds*1000/profile_displays,
                    profile_frames?profile_core/profile_frames:0,profile_present/profile_displays,
                    filter_frames?profile_filter/filter_frames:0};
                if(profile && !game.paused()) {
                    const auto title=std::string("AfterBurner2-Recomp — simulation ")+std::to_string(profile_frames/seconds)+" FPS";
                    SDL_SetWindowTitle(window,title.c_str());
                    std::cout<<"Application performance: simulation_fps="<<profile_frames/seconds
                             <<" display_fps="<<profile_displays/seconds
                             <<" filter_submit_ms="<<(filter_frames?profile_filter/filter_frames:0)
                             <<" board_ms="<<(profile_frames?profile_core/profile_frames:0)
                             <<" wide_scene_ms="<<profile_scene/profile_displays
                             <<" present_wait_ms="<<profile_present/profile_displays<<'\n';
                }
                profile_start=now; profile_frames=profile_displays=0; profile_core=profile_present=0; profile_filter=0;profile_scene=0;filter_frames=0;
            }
        }
    }

    music_player->output=nullptr;menu->audio=nullptr;audio_output.reset();
    menu.reset();feedback.reset();input_manager.reset();music_player.reset();
    if(!gpu_scaler)SDL_DestroyTexture(sprite_texture);
    gpu_scaler.reset();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    if(gpu_device)SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
catch(const std::exception& error)
{
    std::cerr << "X-Board host stopped: " << error.what() << '\n';
    SDL_Quit();
    return 1;
}

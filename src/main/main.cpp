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
#include "frontend/sprite_filter.hpp"

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

    GameSession game(roms);
    unsigned recording_session=0;
    std::unique_ptr<PhysicalRecording> recording;
    if (!recording_path.empty()) recording=std::make_unique<PhysicalRecording>(recording_path,roms);
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
            advance(input); game.take_audio();
        }
        if (start_stage) {
            game.debug_skip_to_stage(start_stage);
            std::cout << "Debug stage skip: entered stage " << start_stage << std::endl;
        }
    };
    if (boot_to_flight || start_stage) startup();
    const size_t frame_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    std::vector<uint32_t> texture_pixels(frame_pixels);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    constexpr int SCALE = 3;
    SDL_Window* window = SDL_CreateWindow(
        "AfterBurner2-Recomp",
        SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE,
        SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) renderer = SDL_CreateRenderer(window, "software");
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

    if (settings_path.empty()) {
        if (char* path=SDL_GetPrefPath("AfterBurner2-Recomp","AfterBurner2")) {
            settings_path=std::string(path)+"settings.cfg"; SDL_free(path);
        }
    }
    auto settings=Frontend::load_settings(settings_path,Frontend::pixel_art_available());
    if (!Frontend::apply_settings(window,renderer,texture,settings)) {
        std::cerr << "Saved presentation settings unavailable: " << SDL_GetError() << '\n';
        settings={};
        if(!Frontend::apply_settings(window,renderer,texture,settings)) throw std::runtime_error(SDL_GetError());
    }
    Frontend::SpriteScaler sprite_scaler;
    SDL_Texture* sprite_texture=nullptr;
    unsigned sprite_texture_factor=0;
    uint64_t last_sprite_frame=UINT64_MAX;
    const auto configure_sprite_texture=[&](const Frontend::Settings& value) {
        if(value.sprites==Frontend::SpriteFilter::none) return true;
        const auto factor=Frontend::sprite_factor(value.sprites);
        auto* next=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,SCREEN_WIDTH*factor,SCREEN_HEIGHT*factor);
        if(!next || !Frontend::apply_settings(window,renderer,next,value)) {SDL_DestroyTexture(next);return false;}
        SDL_DestroyTexture(sprite_texture);sprite_texture=next;sprite_texture_factor=factor;last_sprite_frame=UINT64_MAX;
        return true;
    };
    if(!configure_sprite_texture(settings)) {
        std::cerr<<"Saved sprite filter unavailable: "<<SDL_GetError()<<'\n';
        settings.sprites=Frontend::SpriteFilter::none;
    }
    game.capture_sprite_presentation(settings.sprites!=Frontend::SpriteFilter::none);
    auto menu=std::make_unique<Frontend::NativeMenu>(renderer,roms,settings,Frontend::pixel_art_available());
    menu->apply=[&](const Frontend::Settings& changed) {
        const auto previous=menu->settings;
        if(!Frontend::apply_settings(window,renderer,texture,changed)) {
            std::cerr << "Presentation setting unavailable: " << SDL_GetError() << '\n';
            Frontend::apply_settings(window,renderer,texture,previous); return false;
        }
        if(!configure_sprite_texture(changed)) {
            Frontend::apply_settings(window,renderer,texture,previous);
            return false;
        }
        game.capture_sprite_presentation(changed.sprites!=Frontend::SpriteFilter::none);
        menu->notice.clear();
        if(!Frontend::save_settings(settings_path,changed)) {
            menu->notice="SETTINGS NOT SAVED";
            std::cerr << "Cannot save frontend settings to " << settings_path << '\n';
        }
        return true;
    };
    if(!menu_preview.empty()) {
        menu->open(game);menu->key(SDLK_DOWN);menu->key(SDLK_RETURN);menu->draw();
        SDL_Surface* preview=SDL_RenderReadPixels(renderer,nullptr);
        if(!preview || !SDL_SaveBMP(preview,menu_preview.c_str())) throw std::runtime_error(SDL_GetError());
        SDL_DestroySurface(preview);menu.reset();SDL_DestroyTexture(sprite_texture);SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }
    if(!frame_limit && !boot_to_flight && !start_stage) menu->open(game);

    SDL_AudioStream* audio_stream = nullptr;
    constexpr int sample_rate = GameSession::sample_rate;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        SDL_AudioSpec spec{};
        spec.freq = sample_rate;
        spec.format = SDL_AUDIO_S16; spec.channels = 2;
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
        audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (audio_stream && !SDL_ResumeAudioStreamDevice(audio_stream))
        {
            SDL_DestroyAudioStream(audio_stream); audio_stream = nullptr;
        }
    }
    if (!audio_stream) std::cerr << "Audio unavailable: " << SDL_GetError() << '\n';

    const double tick_seconds = GameSession::frame_seconds;
    const double counter_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    uint64_t previous_counter = SDL_GetPerformanceCounter();
    uint64_t profile_start = previous_counter;
    unsigned profile_frames=0, profile_displays=0;
    double profile_core=0, profile_present=0, profile_filter=0;
    unsigned filter_frames=0;
    Frontend::Performance performance;
    double accumulated_seconds = tick_seconds;
    bool running = true;
    bool suppress_controls=false;
    auto reset_pacing=[&] {
        accumulated_seconds=0; previous_counter=SDL_GetPerformanceCounter();
        profile_start=previous_counter;profile_frames=profile_displays=0;profile_core=profile_present=0;
        performance={};last_sprite_frame=UINT64_MAX;profile_filter=0;filter_frames=0;
    };
    auto sync_audio=[&] {
        if(audio_stream) {
            if(game.paused()) SDL_PauseAudioStreamDevice(audio_stream);
            else SDL_ResumeAudioStreamDevice(audio_stream);
        }
    };
    sync_audio();
    auto menu_action=[&](Frontend::NativeMenu::Action action) {
        if(action==Frontend::NativeMenu::Action::quit) {running=false;return;}
        if(action==Frontend::NativeMenu::Action::close || action==Frontend::NativeMenu::Action::start) {
            menu->close(game);
            if(action==Frontend::NativeMenu::Action::start) {startup();if(audio_stream)SDL_ClearAudioStream(audio_stream);}
            suppress_controls=true;reset_pacing();sync_audio();
            SDL_SetWindowTitle(window,game.paused()?"AfterBurner2-Recomp — Paused (P to resume)":"AfterBurner2-Recomp");
        }
    };
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if(menu->is_open()) {
                if(event.type==SDL_EVENT_KEY_DOWN && (!event.key.repeat || event.key.key==SDLK_UP || event.key.key==SDLK_DOWN))
                    menu_action(menu->key(event.key.key));
                if(event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    SDL_ConvertEventToRenderCoordinates(renderer,&event);
                    if(event.type==SDL_EVENT_MOUSE_MOTION) menu_action(menu->pointer(event.motion.x,event.motion.y,false));
                    else if(event.button.button==SDL_BUTTON_LEFT)menu_action(menu->pointer(event.button.x,event.button.y,true));
                }
                continue;
            }
            if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key==SDLK_ESCAPE || event.key.key==SDLK_F1)) {
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
                game.restart();
                if (recording) recording=std::make_unique<PhysicalRecording>(recording_path+".restart"+std::to_string(++recording_session),roms);
                if (start_stage) startup();
                if (audio_stream) SDL_ClearAudioStream(audio_stream);
                if (audio_stream) SDL_ResumeAudioStreamDevice(audio_stream);
                reset_pacing();
                SDL_SetWindowTitle(window, "AfterBurner2-Recomp");
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P)
            {
                game.set_paused(!game.paused());
                if (audio_stream) {
                    if (game.paused()) SDL_PauseAudioStreamDevice(audio_stream);
                    else SDL_ResumeAudioStreamDevice(audio_stream);
                }
                reset_pacing();
                SDL_SetWindowTitle(window, game.paused() ? "AfterBurner2-Recomp — Paused (P to resume)" : "AfterBurner2-Recomp");
            }
        }

        if (!running) break;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        GameControls input;
        if(suppress_controls) {
            const SDL_Scancode controls[]{SDL_SCANCODE_RIGHT,SDL_SCANCODE_LEFT,SDL_SCANCODE_DOWN,SDL_SCANCODE_UP,
                SDL_SCANCODE_LSHIFT,SDL_SCANCODE_SPACE,SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_5,SDL_SCANCODE_RETURN};
            suppress_controls=std::any_of(std::begin(controls),std::end(controls),[&](auto key){return keys[key];});
        }
        // Preserve the existing keyboard precedence when opposite keys are
        // held. Other devices supply normalized axes through the same API.
        input.stick_x = keys[SDL_SCANCODE_RIGHT] ? 1 : keys[SDL_SCANCODE_LEFT] ? -1 : 0;
        input.stick_y = keys[SDL_SCANCODE_DOWN] ? -1 : keys[SDL_SCANCODE_UP] ? 1 : 0;
        input.throttle = keys[SDL_SCANCODE_LSHIFT] ? -1 : keys[SDL_SCANCODE_SPACE] ? 1 : 0;
        input.cannon = keys[SDL_SCANCODE_Z];
        input.missile = keys[SDL_SCANCODE_X];
        input.coin = keys[SDL_SCANCODE_5];
        input.start = keys[SDL_SCANCODE_RETURN];
        if(menu->is_open() || suppress_controls) input={};

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
            if(measure) profile_core+=(SDL_GetPerformanceCounter()-core_start)*1000.0/counter_frequency;
            ++profile_frames;
            // Backpressure bounds playback latency without dropping sound.
            // The board still advances when no speaker device is available.
            constexpr int queue_limit_bytes = sample_rate * 2 * sizeof(int16_t) / 10;
            const auto samples = game.take_audio();
            if (!samples.empty())
            {
                if (audio_stream)
                {
                    while (audio_stream)
                    {
                        const int queued = SDL_GetAudioStreamQueued(audio_stream);
                        if (queued < 0 || !SDL_GetAudioStreamDevice(audio_stream)
                            || SDL_AudioStreamDevicePaused(audio_stream))
                        {
                            std::cerr << "Audio device stopped; continuing without a speaker.\n";
                            SDL_DestroyAudioStream(audio_stream); audio_stream = nullptr;
                            break;
                        }
                        if (queued <= queue_limit_bytes) break;
                        SDL_Delay(1);
                    }
                    if (audio_stream && !SDL_PutAudioStreamData(audio_stream, samples.data(), static_cast<int>(samples.size() * sizeof(int16_t))))
                    {
                        std::cerr << "Audio queue failed: " << SDL_GetError() << '\n';
                        SDL_DestroyAudioStream(audio_stream); audio_stream = nullptr;
                    }
                }
            }
            accumulated_seconds -= tick_seconds;
            ++simulated_frames;
            if (frame_limit && simulated_frames >= frame_limit)
            { running = false; break; }
        }

        const auto frame = game.frame().pixels;
        for (size_t i = 0; i < frame.size(); i++)
        {
            const RGB888& p = frame[i];
            texture_pixels[i] = 0xFF000000u | (static_cast<uint32_t>(p.r) << 16) |
                                 (static_cast<uint32_t>(p.g) << 8) | p.b;
        }
        SDL_UpdateTexture(texture, nullptr, texture_pixels.data(), SCREEN_WIDTH * 4);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const auto planes=game.sprite_presentation();
        if(menu->settings.sprites!=Frontend::SpriteFilter::none && planes.ready && !menu->is_open()) {
            const auto factor=Frontend::sprite_factor(menu->settings.sprites);
            if(!sprite_texture || sprite_texture_factor!=factor)
                throw std::runtime_error("Sprite texture configuration lost");
            if(last_sprite_frame!=game.info().number) {
                const auto filter_start=SDL_GetPerformanceCounter();
                const auto pixels=sprite_scaler.compose(planes.background,planes.sprites,SCREEN_WIDTH,SCREEN_HEIGHT,menu->settings.sprites,planes.protected_pixels);
                if(!SDL_UpdateTexture(sprite_texture,nullptr,pixels.data(),SCREEN_WIDTH*factor*4))
                    throw std::runtime_error(SDL_GetError());
                profile_filter+=(SDL_GetPerformanceCounter()-filter_start)*1000.0/counter_frequency;
                ++filter_frames;
                last_sprite_frame=game.info().number;
            }
            SDL_RenderTexture(renderer,sprite_texture,nullptr,nullptr);
        } else SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        if(menu->is_open())menu->draw();
        else menu->draw_stats(performance,game.paused());
        if(!frame_preview.empty() && frame_limit && simulated_frames>=frame_limit) {
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
                             <<" board_ms="<<(profile_frames?profile_core/profile_frames:0)
                             <<" present_wait_ms="<<profile_present/profile_displays<<'\n';
                }
                profile_start=now; profile_frames=profile_displays=0; profile_core=profile_present=0; profile_filter=0;filter_frames=0;
            }
        }
    }

    if (audio_stream) SDL_DestroyAudioStream(audio_stream);
    menu.reset();
    SDL_DestroyTexture(sprite_texture);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
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

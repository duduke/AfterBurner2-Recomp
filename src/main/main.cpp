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
#include <random>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <utility>
#include <cstdlib>
#include "roms.hpp"
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/screen.hpp"
#include "engine/game_session.hpp"
#include "engine/physical_recording.hpp"
#include "frontend/native_menu.hpp"
#include "frontend/native_practice.hpp"
#include "hwvideo/wide_scene.hpp"
#include "frontend/gpu_sprite_filter.hpp"
#include "frontend/native_hud.hpp"
#include "frontend/native_crt.hpp"
#include "frontend/native_slang.hpp"
#include "frontend/native_metal_sprite_filter.hpp"
#include "frontend/native_vulkan_sprite_filter.hpp"
#include "frontend/native_player_aircraft.hpp"
#include "frontend/controller_database.hpp"
#include "frontend/native_rom_picker.hpp"
#include "frontend/native_touch.hpp"
#include "frontend/frame_timings.hpp"
#include "frontend/frame_pacing.hpp"

namespace
{
    constexpr int SCREEN_WIDTH = GameSession::width;
    constexpr int SCREEN_HEIGHT = GameSession::height;
    constexpr unsigned PRESENTATION_FRAMES_IN_FLIGHT = 3;

    struct ShaderDialogReply {std::string path,error;bool cancelled{};};
    struct ShaderDialogState {
        std::mutex mutex;
        bool active{};
        std::optional<ShaderDialogReply> reply;
    };
    struct ShaderDialogRequest {
        std::weak_ptr<ShaderDialogState> state;
        std::string initial_path;
    };
    void SDLCALL shader_dialog_result(void* userdata,const char* const* files,int) {
        std::unique_ptr<ShaderDialogRequest> request(static_cast<ShaderDialogRequest*>(userdata));
        auto state=request->state.lock();if(!state)return;
        ShaderDialogReply reply;
        if(!files){reply.error=SDL_GetError();if(reply.error.empty())reply.error="File dialog unavailable";}
        else if(!files[0])reply.cancelled=true;
        else reply.path=files[0];
        std::lock_guard lock(state->mutex);
        state->active=false;state->reply=std::move(reply);
    }

}

int main(int argc, char** argv)try
{
    unsigned frame_limit = 0;
    unsigned start_stage = 0;
    unsigned practice_start_stage = 0;
    bool shuffle_start = false;
    std::optional<uint32_t> shuffle_seed;
    bool boot_to_flight = false, profile = false,integrated_scene=true;
    std::string recording_path, rom_source;
    std::string settings_path, menu_preview, frame_preview, hud_atlas, player_aircraft,shader_preset,shader_runtime;
    for (int i=1;i<argc;++i)
    {
        const std::string_view option(argv[i]);
        if (option == "--boot-to-flight") { boot_to_flight=true; continue; }
        if (option == "--profile") { profile=true; continue; }
        if(option=="--scene-renderer" && i+1<argc){
            const std::string_view value(argv[++i]);
            if(value!="snapshot"&&value!="integrated")throw std::runtime_error("Scene renderer must be snapshot or integrated");
            integrated_scene=value=="integrated";continue;
        }
        if (option == "--shuffle") { shuffle_start=true; continue; }
        if (option == "--shuffle-seed" && i+1<argc) {
            const std::string_view value(argv[++i]);uint32_t seed;
            const auto result=std::from_chars(value.data(),value.data()+value.size(),seed);
            if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size())
                throw std::runtime_error("Shuffle seed must be an unsigned 32-bit integer");
            shuffle_seed=seed;shuffle_start=true;continue;
        }
        if (option == "--roms" && i+1<argc) { rom_source=argv[++i]; continue; }
        if (option == "--record" && i+1<argc) { recording_path=argv[++i]; continue; }
        if (option == "--settings-file" && i+1<argc) { settings_path=argv[++i]; continue; }
        if (option == "--frame-preview" && i+1<argc) { frame_preview=argv[++i]; continue; }
        if (option == "--hud-atlas" && i+1<argc) { hud_atlas=argv[++i]; continue; }
        if (option == "--player-aircraft" && i+1<argc) { player_aircraft=argv[++i]; continue; }
        if (option == "--shader-preset" && i+1<argc) { shader_preset=argv[++i]; continue; }
        if (option == "--shader-runtime" && i+1<argc) { shader_runtime=argv[++i]; continue; }
        if (option == "--menu-preview" && i+1<argc) { menu_preview=argv[++i]; continue; }
        if ((option != "--frames" && option != "--stage" && option != "--practice-stage") || i+1>=argc)
        { std::cerr << "Usage: afterburner2 [--roms directory|set.zip] [--frames positive-count] [--stage number | --practice-stage number | --shuffle [--shuffle-seed number]] [--boot-to-flight] [--record path] [--profile] [--scene-renderer snapshot|integrated] [--settings-file path] [--hud-atlas image.png] [--player-aircraft image.png|pack.aircraft] [--shader-preset preset.slangp] [--shader-runtime library-path] [--menu-preview image.bmp] [--frame-preview image.bmp --frames count]\n"; return 1; }
        const std::string_view value(argv[++i]);
        auto& count = option == "--stage" ? start_stage : option == "--practice-stage" ? practice_start_stage : frame_limit;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !count)
        { std::cerr << (option == "--frames" ? "Frame count" : "Stage") << " must be a positive integer.\n"; return 1; }
    }
    if(!frame_preview.empty() && !frame_limit) throw std::runtime_error("Frame preview requires --frames");
    unsigned simulated_frames = 0;
    if (start_stage && practice_start_stage)
        throw std::runtime_error("Choose either --stage or --practice-stage");
    if(shuffle_start && (start_stage || practice_start_stage))
        throw std::runtime_error("Shuffle cannot be combined with selected-stage startup");
    if (!recording_path.empty() && (start_stage || practice_start_stage || shuffle_start))
        throw std::runtime_error("Natural recording requires normal cold boot without selected-stage startup");
#ifdef SDL_PLATFORM_ANDROID
    // SDL's resizable window otherwise permits portrait despite the manifest.
    SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
#ifdef SDL_PLATFORM_ANDROID
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
#endif

    if (settings_path.empty()) {
        if (char* path=SDL_GetPrefPath("AfterBurner2-Recomp","AfterBurner2")) {
            settings_path=std::string(path)+"settings.cfg"; SDL_free(path);
        }
    }
    const auto saved_rom_path = settings_path.empty() ? std::string{} : settings_path + ".roms";
    bool roms_loaded = false;
    if (!rom_source.empty()) {
        RomLoader::set_source(rom_source);
        roms_loaded = roms.load_aburner2();
    } else {
        roms_loaded = roms.load_aburner2();
        if (!roms_loaded && !saved_rom_path.empty()) {
            std::ifstream saved(saved_rom_path);
            if (std::getline(saved, rom_source) && !rom_source.empty()) {
                RomLoader::set_source(rom_source);
                roms_loaded = roms.load_aburner2();
            }
        }
    }
    // Bounded CLI checks must fail promptly when their fixture is missing.
    while (!roms_loaded && !frame_limit) {
        const auto reply = Frontend::pick_rom_zip();
        if (reply.cancelled) { SDL_Quit(); return 0; }
        if (!reply.error.empty()) {
            std::cerr << "ROM file picker: " << reply.error << '\n';
            SDL_Quit(); return 1;
        }
        RomLoader::set_source(reply.path);
        if (!(roms_loaded = roms.load_aburner2())) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to load ROM set",
                "Choose a complete After Burner II (aburner2) ROM ZIP. "
                "Every required file must match the expected size and CRC. See roms/README.md.", nullptr);
            continue;
        }
        if (!saved_rom_path.empty()) {
            std::ofstream saved(saved_rom_path);
            if (!(saved << reply.path << '\n'))
                std::cerr << "Cannot save ROM ZIP location to " << saved_rom_path << '\n';
        }
    }
    if (!roms_loaded) {
        std::cerr << "Failed to load ROM set (see roms/README.md).\n";
        SDL_Quit(); return 1;
    }
    std::cout << "ROM set loaded OK." << std::endl;
    if (start_stage && start_stage > NativeStageSkip::stage_count(roms))
        throw std::runtime_error("Stage number out of range (1.." + std::to_string(NativeStageSkip::stage_count(roms)) + ")");
    if (practice_start_stage) StageCatalog::get(roms, practice_start_stage);

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
            if ((f & 15) == 0) SDL_PumpEvents();
            GameControls input;
            input.coin=f>=360&&f<372; input.start=f>=420&&f<432;
            advance(input);
            take_audio();game.take_feedback();
        }
        if (start_stage) {
            SDL_PumpEvents();
            game.debug_skip_to_stage(start_stage);if(music_player)music_player->mix({},game.take_music());
            SDL_PumpEvents();
            std::cout << "Debug stage skip: entered stage " << start_stage << std::endl;
        }
    };

    const size_t frame_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    std::vector<uint32_t> texture_pixels(frame_pixels);

    auto settings=Frontend::load_settings(settings_path,Frontend::pixel_art_available());
    const auto stored_shader_preset=settings.shader_preset,stored_shader_runtime=settings.shader_runtime;
    if(!hud_atlas.empty()){settings.hud_atlas=hud_atlas;settings.hud=Frontend::Hud::custom;}
    if(!shader_runtime.empty())settings.shader_runtime=shader_runtime;
    if(!shader_preset.empty()){settings.shader_preset=shader_preset;settings.crt=Frontend::Crt::retroarch;}

    constexpr int SCALE = 3;
    SDL_Window* window = SDL_CreateWindow(
        "AfterBurner2-Recomp",
        Frontend::viewport_width(settings) * SCALE, SCREEN_HEIGHT * SCALE,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GPUDevice* gpu_device=nullptr;
    SDL_Renderer* renderer=nullptr;
    bool native_shader_backend=false;
    const auto create_renderer=[&](bool native_shader) {
        native_shader_backend=false;
#ifdef __APPLE__
        if(native_shader) {
            renderer=SDL_CreateRenderer(window,"metal");
            native_shader_backend=renderer!=nullptr;
        } else
#elif defined(AB2_NATIVE_VULKAN_SPRITES)
        if(native_shader) {
            renderer=SDL_CreateRenderer(window,"vulkan");
            native_shader_backend=renderer!=nullptr;
        } else
#else
        (void)native_shader;
#endif
        {
            gpu_device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,false,nullptr);
            renderer=gpu_device?SDL_CreateGPURenderer(gpu_device,window):nullptr;
            if(renderer)SDL_SetGPUAllowedFramesInFlight(gpu_device,PRESENTATION_FRAMES_IN_FLIGHT);
            else {
                if(gpu_device)SDL_DestroyGPUDevice(gpu_device);gpu_device=nullptr;
                renderer=SDL_CreateRenderer(window,nullptr);
            }
        }
        if(!renderer)return false;
#ifdef _WIN32
        const bool win_vulkan=(gpu_device && SDL_GetGPUDeviceDriver(gpu_device) &&
            std::string_view(SDL_GetGPUDeviceDriver(gpu_device))=="vulkan") ||
            std::string_view(SDL_GetRendererName(renderer))=="vulkan";
        SDL_SetRenderVSync(renderer,win_vulkan?0:1);
#else
        SDL_SetRenderVSync(renderer,1);
#endif
        return true;
    };
    std::string shader_status;
    std::unique_ptr<Frontend::NativeSlang> slang_renderer;
    const bool requested_shader=settings.crt==Frontend::Crt::retroarch;
    const bool requested_shader_backend=requested_shader && !settings.shader_preset.empty();
    if(!create_renderer(requested_shader_backend)) {
        if(!requested_shader_backend)throw std::runtime_error(std::string("SDL renderer unavailable: ")+SDL_GetError());
        shader_status="Shader display backend unavailable: "+std::string(SDL_GetError());
        std::cerr<<shader_status<<"; trying the normal game display.\n";
        settings.crt=Frontend::Crt::off;
        if(!shader_preset.empty())settings.shader_preset=stored_shader_preset;
        if(!shader_runtime.empty())settings.shader_runtime=stored_shader_runtime;
        if(!create_renderer(false))throw std::runtime_error(std::string("SDL renderer unavailable: ")+SDL_GetError());
    }
    if(settings.crt==Frontend::Crt::retroarch) {
        slang_renderer=std::make_unique<Frontend::NativeSlang>(renderer,window);
        if(settings.shader_preset.empty() || !slang_renderer->load(settings.shader_preset,settings.shader_runtime)) {
            shader_status=settings.shader_preset.empty()?"CHOOSE A .SLANGP PRESET":slang_renderer->error();
            std::cerr<<"RetroArch shader unavailable: "<<shader_status<<"; using Off.\n";
            settings.crt=Frontend::Crt::off;slang_renderer.reset();
            if(!shader_preset.empty())settings.shader_preset=stored_shader_preset;
            if(!shader_runtime.empty())settings.shader_runtime=stored_shader_runtime;
            if(native_shader_backend) {
                SDL_DestroyRenderer(renderer);renderer=nullptr;
                if(!create_renderer(false))throw std::runtime_error(SDL_GetError());
            }
        } else shader_status="ACTIVE: "+std::filesystem::path(settings.shader_preset).filename().string();
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
    Frontend::NativeTouch touch;
#ifdef SDL_PLATFORM_ANDROID
    touch.enabled=!SDL_IsTV();
#endif
    auto touch_aspect=[&]{int w=1,h=1;SDL_GetWindowSize(window,&w,&h);return float(w)/std::max(1,h);};
    std::optional<SDL_FingerID> menu_finger;
    float menu_touch_y{},menu_scroll_y{};
    bool menu_touch_moved{};
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
    auto hud_renderer=std::make_unique<Frontend::NativeHud>(renderer,roms);
    auto crt_renderer=std::make_unique<Frontend::NativeCrt>(renderer);
    auto aircraft_renderer=std::make_unique<Frontend::NativePlayerAircraft>(renderer);
    if(!player_aircraft.empty()) {
        if(!aircraft_renderer->load(player_aircraft))
            std::cerr<<"Player-aircraft prototype unavailable: "<<aircraft_renderer->load_error()<<"; using Original.\n";
        else std::cout<<"Player-aircraft HD prototype: "<<aircraft_renderer->supported_roots().size()<<" pose families; other poses use Original.\n";
    }
    const auto atlas_path=settings.hud_atlas.empty()?settings_path+".hud.png":settings.hud_atlas;
    if(!hud_renderer->load_custom(atlas_path) && settings.hud==Frontend::Hud::custom) {
        std::cerr<<"Custom HUD atlas unavailable: "<<atlas_path<<" ("<<hud_renderer->custom_error()<<"); using Sharp 3x.\n";
        settings.hud=Frontend::Hud::sharp;
    }
    std::vector<uint32_t> effect_pixels,effect_background;
    std::unique_ptr<Frontend::GPUSpriteScaler> gpu_scaler;
#ifdef __APPLE__
    std::unique_ptr<Frontend::NativeMetalSpriteScaler> metal_scaler;
#elif defined(AB2_NATIVE_VULKAN_SPRITES)
    std::unique_ptr<Frontend::NativeVulkanSpriteScaler> vulkan_scaler;
#endif
    const auto accelerated_sprites=[&] {
#ifdef __APPLE__
        if(metal_scaler)return true;
#elif defined(AB2_NATIVE_VULKAN_SPRITES)
        if(vulkan_scaler)return true;
#endif
        return bool(gpu_scaler);
    };
    const auto create_sprite_scaler=[&] {
        if(gpu_device)gpu_scaler=std::make_unique<Frontend::GPUSpriteScaler>(renderer);
#ifdef __APPLE__
        else if(native_shader_backend)metal_scaler=std::make_unique<Frontend::NativeMetalSpriteScaler>(renderer);
#elif defined(AB2_NATIVE_VULKAN_SPRITES)
        else if(native_shader_backend) {
            try {vulkan_scaler=std::make_unique<Frontend::NativeVulkanSpriteScaler>(renderer);}
            catch(const std::exception& error) {std::cerr<<"Native Vulkan sprite filters unavailable: "<<error.what()<<"; using CPU composition.\n";}
        }
#endif
        if(profile) {
            int vsync=0;SDL_GetRenderVSync(renderer,&vsync);
            std::cout<<"Native renderer: "<<SDL_GetRendererName(renderer)
                <<" backend="<<(gpu_device?SDL_GetGPUDeviceDriver(gpu_device):SDL_GetRendererName(renderer))
                <<" continuation="<<XBoardCpuContinuation::backend_name()
                <<" filters="<<(gpu_device?"gpu":
#ifdef __APPLE__
                    metal_scaler?"metal-gpu":
#elif defined(AB2_NATIVE_VULKAN_SPRITES)
                    vulkan_scaler?"vulkan-gpu":
#endif
                    "cpu")<<" SDL="<<SDL_GetVersion()<<" vsync="<<vsync<<'\n';
        }
    };
    const auto release_sprite_scalers=[&] {
        gpu_scaler.reset();
#ifdef __APPLE__
        metal_scaler.reset();
#elif defined(AB2_NATIVE_VULKAN_SPRITES)
        vulkan_scaler.reset();
#endif
    };
    create_sprite_scaler();
    std::unique_ptr<Presentation::WideScene> wide_renderer;
    double primary_scene_ms=0;
    SDL_Texture* sprite_texture=nullptr;
    unsigned sprite_texture_factor=0;
    uint64_t last_sprite_frame=UINT64_MAX;
    auto last_sprite_filter=Frontend::SpriteFilter::none;
    const auto configure_sprite_texture=[&](const Frontend::Settings& value) {
        const bool detached=Frontend::is_wide(value)||value.hud!=Frontend::Hud::original||aircraft_renderer->available();
        if(detached && (!wide_renderer || wide_renderer->view().width!=Frontend::viewport_width(value)))
            wide_renderer=std::make_unique<Presentation::WideScene>(roms,Frontend::viewport_width(value));
        if(!detached)wide_renderer.reset();
        if(detached&&integrated_scene){
            // Keep a settings copy for the whole partial frame, including
            // hidden Practice/Shuffle startup and renderer replacement.
            game.capture_wide_scene(true,Frontend::viewport_width(value),
                [&,options=value](const auto& clip,uint64_t frame,const auto& layers){
                    const auto started=SDL_GetPerformanceCounter();
                    wide_renderer->render_primary_clip(clip,frame,layers,options.ground,Frontend::is_wide(options),options.hud==Frontend::Hud::original);
                    primary_scene_ms+=(SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency();
                },false,[&](uint64_t frame){
                    const auto started=SDL_GetPerformanceCounter();wide_renderer->finish_primary_frame(frame);
                    primary_scene_ms+=(SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency();
                });
        }else game.capture_wide_scene(detached,Frontend::viewport_width(value));
        if(wide_renderer){
            const unsigned translucent=(value.clouds?(1u<<unsigned(Presentation::Effect::cloud)):0)|
                (value.trails?(1u<<unsigned(Presentation::Effect::missile_trail)):0)|
                (value.explosions?(1u<<unsigned(Presentation::Effect::explosion)):0)|
                (value.afterburner?(1u<<unsigned(Presentation::Effect::afterburner)):0);
            wide_renderer->player_aircraft(aircraft_renderer->available(),translucent,aircraft_renderer->supported_roots());
        }

        if(accelerated_sprites()) {last_sprite_frame=UINT64_MAX;return true;}
        if(value.sprites==Frontend::SpriteFilter::none && !value.clouds && !value.trails && !value.explosions && !value.afterburner && !value.ground && !detached) return true;
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
    const bool native_planes=!Frontend::is_wide(settings)&&settings.hud==Frontend::Hud::original&&!aircraft_renderer->available();
    game.capture_ground_horizon(settings.ground&&native_planes);
    game.capture_sprite_scene(native_planes && (settings.clouds || settings.trails || settings.explosions || settings.afterburner));
    game.capture_sprite_presentation(native_planes && (accelerated_sprites() || settings.sprites!=Frontend::SpriteFilter::none || settings.clouds || settings.trails || settings.explosions || settings.afterburner || settings.ground));
    auto menu=std::make_unique<Frontend::NativeMenu>(renderer,roms,settings,Frontend::pixel_art_available());
    Frontend::NativePractice practice;
    menu->practice_allowed=recording_path.empty();
    menu->shuffle_allowed=recording_path.empty();
    menu->custom_hud_available=hud_renderer->custom_available();
    menu->feedback=feedback.get();menu->input=input_manager.get();menu->music=music_player.get();
    menu->shader_status=shader_status;
    auto accepted_settings=settings;
    std::optional<Frontend::Settings> pending_shader_settings;
    bool applying_shader_settings=false;
    auto shader_dialog=std::make_shared<ShaderDialogState>();
    menu->choose_shader=[&] {
        {
            std::lock_guard lock(shader_dialog->mutex);
            if(shader_dialog->active || shader_dialog->reply)return;
            shader_dialog->active=true;
        }
        static const SDL_DialogFileFilter filter{"RetroArch Slang preset","slangp"};
        auto request=std::make_unique<ShaderDialogRequest>();
        request->state=shader_dialog;request->initial_path=menu->settings.shader_preset;
#ifdef __APPLE__
        if(request->initial_path.empty())if(const char* user_home=std::getenv("HOME")) {
            const auto installed=std::filesystem::path(user_home)/"Library/Application Support/RetroArch/shaders/shaders_slang/crt";
            std::error_code error;
            if(std::filesystem::is_directory(installed,error))request->initial_path=installed.string();
        }
#endif
        auto* raw=request.release();
        SDL_ShowOpenFileDialog(shader_dialog_result,raw,window,&filter,1,
            raw->initial_path.empty()?nullptr:raw->initial_path.c_str(),false);
    };
    const auto prepare_fullscreen=[&](const Frontend::Settings& value) {
#ifdef _WIN32
        if(!value.fullscreen)return;
        // Cold fullscreen submissions can block for hundreds of milliseconds.
        // Prepare the allowed in-flight queue before playback/pacing starts,
        // or while transition callers have playback paused. Never advance the
        // board or consume/generate PCM to repay renderer initialization time.
        const bool had_viewport=SDL_RenderViewportSet(renderer);
        SDL_Rect viewport{};SDL_GetRenderViewport(renderer,&viewport);
        std::fill(texture_pixels.begin(),texture_pixels.end(),0xff000000u);
        if(!SDL_UpdateTexture(texture,nullptr,texture_pixels.data(),SCREEN_WIDTH*4))
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderViewport(renderer,nullptr);
        for(unsigned i=0;i<PRESENTATION_FRAMES_IN_FLIGHT;++i) {
            SDL_SetRenderDrawColor(renderer,0,0,0,255);
            if(!SDL_RenderClear(renderer) || !SDL_RenderTexture(renderer,texture,nullptr,nullptr) ||
                !SDL_RenderPresent(renderer))throw std::runtime_error(SDL_GetError());
        }
        SDL_SetRenderViewport(renderer,had_viewport?&viewport:nullptr);
#else
        (void)value;
#endif
    };
    // Renderer-owned objects release their textures before the backend dies.
    // Board, audio, input, menus and the current frame remain live throughout.
    const auto rebuild_renderer=[&](bool native_shader,const Frontend::Settings& value) {
        slang_renderer.reset();menu->rebind(nullptr,roms);hud_renderer->rebind(nullptr);
        aircraft_renderer.reset();crt_renderer.reset();
        if(!accelerated_sprites())SDL_DestroyTexture(sprite_texture);
        release_sprite_scalers();sprite_texture=nullptr;sprite_texture_factor=0;
        SDL_DestroyTexture(texture);texture=nullptr;
        SDL_DestroyRenderer(renderer);renderer=nullptr;
        if(gpu_device)SDL_DestroyGPUDevice(gpu_device);gpu_device=nullptr;
        if(!create_renderer(native_shader))throw std::runtime_error(std::string("Display backend unavailable: ")+SDL_GetError());
        texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,SCREEN_WIDTH,SCREEN_HEIGHT);
        if(!texture || !Frontend::apply_settings(window,renderer,texture,value))throw std::runtime_error(SDL_GetError());
        hud_renderer->rebind(renderer);crt_renderer=std::make_unique<Frontend::NativeCrt>(renderer);
        aircraft_renderer=std::make_unique<Frontend::NativePlayerAircraft>(renderer);
        if(!player_aircraft.empty() && !aircraft_renderer->load(player_aircraft))
            std::cerr<<"Player-aircraft art reload unavailable: "<<aircraft_renderer->load_error()<<'\n';
        create_sprite_scaler();
        menu->rebind(renderer,roms);
        if(!configure_sprite_texture(value))throw std::runtime_error(SDL_GetError());
        last_sprite_frame=UINT64_MAX;
        prepare_fullscreen(value);
    };
    menu->apply=[&](const Frontend::Settings& changed) {
        const auto previous=accepted_settings;
        const bool shader_change=changed.crt!=previous.crt &&
            (changed.crt==Frontend::Crt::retroarch || previous.crt==Frontend::Crt::retroarch);
        if(!applying_shader_settings && (pending_shader_settings || shader_change ||
                changed.shader_preset!=previous.shader_preset || changed.shader_runtime!=previous.shader_runtime)) {
            if(changed.crt==Frontend::Crt::retroarch && changed.shader_preset.empty()) {
                menu->shader_status="CHOOSE A .SLANGP PRESET";menu->choose_shader();return false;
            }
            pending_shader_settings=changed;return true;
        }
        if(changed.hud==Frontend::Hud::custom && !hud_renderer->custom_available())return false;
        if(!Frontend::apply_settings(window,renderer,texture,changed)) {
            std::cerr << "Presentation setting unavailable: " << SDL_GetError() << '\n';
            Frontend::apply_settings(window,renderer,texture,previous); return false;
        }
        if(!configure_sprite_texture(changed)) {
            configure_sprite_texture(previous);Frontend::apply_settings(window,renderer,texture,previous);
            return false;
        }
        const bool native_planes=!Frontend::is_wide(changed)&&changed.hud==Frontend::Hud::original&&!aircraft_renderer->available();
        game.capture_ground_horizon(changed.ground&&native_planes);
        game.capture_sprite_scene(native_planes && (changed.clouds || changed.trails || changed.explosions || changed.afterburner));
        game.capture_sprite_presentation(native_planes && (accelerated_sprites() || changed.sprites!=Frontend::SpriteFilter::none || changed.clouds || changed.trails || changed.explosions || changed.afterburner || changed.ground));
        if(changed.viewport!=previous.viewport&&!changed.fullscreen){
            int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
            SDL_SetWindowSize(window,int(std::lround(wh*Frontend::viewport_width(changed)/224.0)),wh);
            // Refresh the resized GPU swapchain before submitting the new menu.
            SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderDrawColor(renderer,0,0,0,255);
            SDL_RenderClear(renderer);SDL_RenderPresent(renderer);
        }
        SDL_Rect safe_area{int((Frontend::viewport_width(changed)-SCREEN_WIDTH)/2),0,SCREEN_WIDTH,SCREEN_HEIGHT};SDL_SetRenderViewport(renderer,&safe_area);
        if(changed.fullscreen&&!previous.fullscreen)prepare_fullscreen(changed);
        menu->notice.clear();
        accepted_settings=changed;
        if(slang_renderer)slang_renderer->invalidate();
        if(!Frontend::save_settings(settings_path,changed)) {
            menu->notice="SETTINGS NOT SAVED";
            std::cerr << "Cannot save frontend settings to " << settings_path << '\n';
        }
        return true;
    };
    SDL_Rect initial_safe_area{int((Frontend::viewport_width(settings)-SCREEN_WIDTH)/2),0,SCREEN_WIDTH,SCREEN_HEIGHT};SDL_SetRenderViewport(renderer,&initial_safe_area);
    if(!menu_preview.empty()) {
        menu->open(game);for(int row=0;row<3;++row)menu->key(SDLK_DOWN);menu->key(SDLK_RETURN);menu->draw();
        SDL_SetRenderViewport(renderer,nullptr);
        SDL_Surface* preview=SDL_RenderReadPixels(renderer,nullptr);
        if(!preview || !SDL_SaveBMP(preview,menu_preview.c_str())) throw std::runtime_error(SDL_GetError());
        SDL_DestroySurface(preview);shader_dialog.reset();slang_renderer.reset();menu.reset();aircraft_renderer.reset();crt_renderer.reset();hud_renderer.reset();feedback.reset();input_manager.reset();music_player.reset();if(!accelerated_sprites())SDL_DestroyTexture(sprite_texture);release_sprite_scalers();SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);if(gpu_device)SDL_DestroyGPUDevice(gpu_device);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }
    if(!frame_limit && !boot_to_flight && !start_stage && !practice_start_stage && !shuffle_start) menu->open(game);

    prepare_fullscreen(settings);

    auto audio_output=std::make_unique<Frontend::AudioOutput>(settings_path.empty()?"":settings_path+".output");
    menu->audio=audio_output.get();
    bool cockpit_enabled=audio_output->settings.profile!=Frontend::AudioProfile::original;
    game.capture_cockpit(cockpit_enabled);
    if (!shuffle_start && !practice_start_stage && (boot_to_flight || start_stage)) startup();
    music_player->output=audio_output.get();

    const double tick_seconds = GameSession::frame_seconds;
    const double counter_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    uint64_t previous_counter = SDL_GetPerformanceCounter();
    uint64_t profile_start = previous_counter;
    unsigned profile_frames=0, profile_displays=0;
    double profile_core=0, profile_present=0, profile_filter=0, profile_scene=0, profile_crt=0, profile_audio=0;
    unsigned filter_frames=0;
    Frontend::Performance performance;
    Frontend::FrameTimings frame_timings(SDL_getenv("AB2_FRAME_TIMINGS"),frame_limit);
    const auto timing_phases=[&] {
        Frontend::FrameTimings::Phases phases{profile_core,profile_scene,profile_filter,profile_crt,profile_present};
        if(wide_renderer)std::copy(wide_renderer->profile_counters().begin(),wide_renderer->profile_counters().end(),phases.begin()+5);
        phases[9]=profile_audio;
        return phases;
    };
    Frontend::FramePacing pacing(tick_seconds);
#ifdef _WIN32
    uint64_t observed_audio_reprimes=audio_output->status().reprimes;
#endif
    const auto pacing_seconds=[&](double elapsed) {
#ifdef _WIN32
        const auto reprimes=audio_output->status().reprimes;
        // Both Windows playback paths reserve 80 ms. A longer host outage
        // also needs rebasing if the suspended audio worker has not reported
        // exhaustion yet. Check work time and pacing waits: either can stall.
        constexpr double playback_reserve_seconds=0.080;
        if(elapsed>playback_reserve_seconds || reprimes!=observed_audio_reprimes) {
            // Repaying the outage as a burst fills the bounded audio queues
            // and leaves lasting latency. Re-anchor host pacing only; retain
            // every board frame and PCM sample.
            observed_audio_reprimes=reprimes;pacing.reset();return tick_seconds;
        }
#endif
        return elapsed;
    };
    bool running = true;
    auto reset_pacing=[&] {
        pacing.reset(); previous_counter=SDL_GetPerformanceCounter();
        profile_start=previous_counter;profile_frames=profile_displays=0;profile_core=profile_present=0;
        performance={};last_sprite_frame=UINT64_MAX;profile_filter=0;profile_scene=0;profile_crt=0;profile_audio=0;filter_frames=0;
    };
    auto sync_audio=[&] {audio_output->pause(game.paused());};
    sync_audio();
    uint64_t last_shader_frame=UINT64_MAX;
    auto apply_pending_shader=[&] {
        if(!pending_shader_settings)return;
        const auto requested=*pending_shader_settings;pending_shader_settings.reset();
        const auto previous=accepted_settings;const bool previous_native=native_shader_backend;
#if defined(__APPLE__) || defined(AB2_NATIVE_VULKAN_SPRITES)
        const bool wants_native=requested.crt==Frontend::Crt::retroarch;
#else
        const bool wants_native=false;
#endif
        bool rebuilt=false,loaded=false;
        audio_output->pause(true);audio_output->clear();
        try {
            if(wants_native!=native_shader_backend) {rebuilt=true;rebuild_renderer(wants_native,requested);}
            if(requested.crt==Frontend::Crt::retroarch) {
                if(!slang_renderer)slang_renderer=std::make_unique<Frontend::NativeSlang>(renderer,window);
                if(!slang_renderer->load(requested.shader_preset,requested.shader_runtime))
                    throw std::runtime_error(slang_renderer->error());
                loaded=true;
            } else slang_renderer.reset();
            applying_shader_settings=true;
            const bool applied=menu->apply(requested);applying_shader_settings=false;
            if(!applied)throw std::runtime_error(std::string("Shader presentation settings unavailable: ")+SDL_GetError());
            menu->settings=requested;
            menu->shader_status=requested.crt==Frontend::Crt::retroarch?
                "ACTIVE: "+std::filesystem::path(requested.shader_preset).filename().string():"";
            last_shader_frame=UINT64_MAX;
        } catch(const std::exception& error) {
            applying_shader_settings=false;
            const std::string failure=error.what();
            auto restored=previous;
            // A failed preset reload keeps its accepted chain. A backend
            // replacement needs fresh renderer textures and a restored chain.
            if(rebuilt)rebuild_renderer(previous_native,restored);
            if(restored.crt==Frontend::Crt::retroarch && (rebuilt || loaded)) {
                if(!slang_renderer)slang_renderer=std::make_unique<Frontend::NativeSlang>(renderer,window);
                if(!slang_renderer->load(restored.shader_preset,restored.shader_runtime)) {
                    restored.crt=Frontend::Crt::off;rebuild_renderer(false,restored);
                }
            }
            applying_shader_settings=true;
            if(!menu->apply(restored))throw std::runtime_error(std::string("Display rollback failed: ")+SDL_GetError());
            applying_shader_settings=false;menu->settings=restored;
            menu->shader_status=failure;menu->notice="SHADER UNAVAILABLE";
            std::cerr<<"RetroArch shader unavailable: "<<failure<<"; previous game display restored.\n";
        }
        audio_output->clear();reset_pacing();sync_audio();
    };
    auto update_title=[&] {
        const auto title=game.shuffled()?"AfterBurner2-Recomp — Shuffle (F5 retries this order)":practice.stage()?"AfterBurner2-Recomp — Practice from Stage "+std::to_string(practice.stage())+" (F5 to retry)":std::string("AfterBurner2-Recomp");
        SDL_SetWindowTitle(window,(title+(game.paused()?" — Paused (P to resume)":"")).c_str());
    };
    auto restart_board=[&](unsigned selected_practice=0,bool return_arcade=false,const Shuffle::Plan* new_shuffle=nullptr) {
        const bool retry_shuffle=game.shuffled() && !selected_practice && !return_arcade && !new_shuffle;
        const auto target=return_arcade?0:selected_practice?selected_practice:practice.stage();
        if((target || new_shuffle || retry_shuffle) && recording)throw std::runtime_error("Enhanced gameplay unavailable while recording");
        if(wide_renderer)wide_renderer->reset();feedback->restart();input_manager->block();music_player->restarted();
        if(slang_renderer)slang_renderer->invalidate();last_shader_frame=UINT64_MAX;
        audio_output->pause(true);audio_output->clear();
        const auto switches=Frontend::dip_switches(menu->settings);
        // Follow hidden boot music events without queueing hidden playback.
        auto* playback=music_player->output;music_player->output=nullptr;
        try {
            if(new_shuffle || retry_shuffle) {
                if(new_shuffle)game.start_shuffle(*new_shuffle);else game.retry_shuffle();
                game.set_dip_switches(switches);practice={};start_stage=0;
                const auto* plan=game.shuffle_plan();
                std::cout<<"Shuffle seed "<<plan->seed<<": ";
                for(unsigned i=0;i<plan->stages.size();++i)std::cout<<(i?",":"")<<plan->stages[i];
                std::cout<<std::endl;
                startup();
            }
            else if(target)practice.start(game,target,switches,[&]{SDL_PumpEvents();take_audio();game.take_feedback();});
            else {
                practice.arcade(game,switches);
                if(return_arcade)start_stage=0;
                if(recording)recording=std::make_unique<PhysicalRecording>(recording_path+".restart"+std::to_string(++recording_session),roms,game.dip_switches());
                if(start_stage)startup();
            }
        } catch(...) {music_player->output=playback;throw;}
        music_player->output=playback;
        menu->active_practice_stage=practice.stage();
        menu->shuffle_active=game.shuffled();
        audio_output->clear();
        reset_pacing();sync_audio();
        update_title();
    };
    auto menu_action=[&](Frontend::NativeMenu::Action action) {
        if(action==Frontend::NativeMenu::Action::shuffle) {
            if(recording){menu->notice="UNAVAILABLE WHILE RECORDING";return;}
            const auto plan=Shuffle::generate(game.stage_count(),std::random_device{}());
            menu->close(game);restart_board(0,false,&plan);return;
        }
        if(action==Frontend::NativeMenu::Action::practice || action==Frontend::NativeMenu::Action::arcade) {
            if(recording){menu->notice="UNAVAILABLE WHILE RECORDING";return;}
            menu->close(game);
            restart_board(menu->practice_stage,action==Frontend::NativeMenu::Action::arcade);return;
        }
        if(action==Frontend::NativeMenu::Action::restart) {menu->close(game);restart_board();return;}
        if(action==Frontend::NativeMenu::Action::quit) {running=false;return;}
        if(action==Frontend::NativeMenu::Action::close || action==Frontend::NativeMenu::Action::start) {
            menu->close(game);
            // A settings edit can capture the plain menu background at this
            // same paused frame. Recompose the enhanced scene when it closes.
            if(slang_renderer)slang_renderer->invalidate();
            // Enter the arcade at its current cold-board frame. Only explicit
            // --boot-to-flight/--stage requests supply automatic coin/start.
            input_manager->block();reset_pacing();sync_audio();
            update_title();
        }
    };
    if(practice_start_stage){menu->practice_stage=practice_start_stage;restart_board(practice_start_stage);}
    if(shuffle_start){const auto plan=Shuffle::generate(game.stage_count(),shuffle_seed?*shuffle_seed:std::random_device{}());restart_board(0,false,&plan);}
    while (running)
    {
        if(frame_timings.enabled())frame_timings.begin(timing_phases());
        if(wide_renderer)wide_renderer->profile_scene(frame_timings.enabled());
        music_player->poll();music_player->update_preview();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
#ifdef SDL_PLATFORM_ANDROID
            // Android remote Back and Menu use consumer keycodes.
            if(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) {
                if(event.key.key==SDLK_AC_BACK)event.key.key=SDLK_ESCAPE;
                if(event.key.key==SDLK_MENU)event.key.key=SDLK_F1;
                if(event.key.key==SDLK_SELECT)event.key.key=SDLK_RETURN;
            }
            if(event.type==SDL_EVENT_WILL_ENTER_BACKGROUND || event.type==SDL_EVENT_WINDOW_FOCUS_LOST) {
                touch.clear();
                menu_finger.reset();
                // Bounded Android checks must report an interrupted prefix,
                // rather than waiting indefinitely in the paused frontend.
                if(frame_limit){running=false;continue;}
                if(!menu->is_open())menu->open(game);
                feedback->stop();reset_pacing();sync_audio();
            }
            if(event.type==SDL_EVENT_DID_ENTER_FOREGROUND) {reset_pacing();sync_audio();}
            if(event.type==SDL_EVENT_RENDER_DEVICE_RESET || event.type==SDL_EVENT_RENDER_TARGETS_RESET) {
                rebuild_renderer(false,menu->settings);reset_pacing();continue;
            }
#endif
            const bool touch_menu=!menu->is_open() && touch.event(event,touch_aspect());
            if(touch_menu && !menu->is_open()) {
                touch.clear();menu->open(game);reset_pacing();sync_audio();continue;
            }
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){music_player->stop_preview();audio_output->clear();}
            feedback->event(event);
            if(input_manager->event(event))continue;
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if(menu->is_open()) {
                if(touch.enabled) {
                    if(event.type==SDL_EVENT_FINGER_DOWN && !menu_finger) {
                        menu_finger=event.tfinger.fingerID;
                        menu_touch_y=menu_scroll_y=event.tfinger.y;menu_touch_moved=false;
                    }
                    if(event.type==SDL_EVENT_FINGER_MOTION && menu_finger && *menu_finger==event.tfinger.fingerID) {
                        menu_touch_moved|=std::abs(event.tfinger.y-menu_touch_y)>.035f;
                        if(std::abs(event.tfinger.y-menu_scroll_y)>.07f) {
                            menu_action(menu->key(event.tfinger.y<menu_scroll_y?SDLK_DOWN:SDLK_UP));
                            menu_scroll_y=event.tfinger.y;
                        }
                    }
                    if(event.type==SDL_EVENT_FINGER_UP && menu_finger && *menu_finger==event.tfinger.fingerID) {
                        if(!menu_touch_moved) {
                            int w,h;SDL_GetWindowSize(window,&w,&h);float x,y;
                            if(SDL_RenderCoordinatesFromWindow(renderer,event.tfinger.x*w,event.tfinger.y*h,&x,&y))
                                menu_action(menu->pointer(x,y,true));
                        }
                        menu_finger.reset();
                    }
                    if(event.type==SDL_EVENT_FINGER_CANCELED)menu_finger.reset();
                }
                touch.clear();
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
#ifdef _WIN32
                audio_output->pause(true);
#endif
                menu->fullscreen_toggle();reset_pacing();
#ifdef _WIN32
                sync_audio();
#endif
                continue;
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
                update_title();
            }
        }

        if (!running) break;
        std::optional<ShaderDialogReply> shader_reply;
        {
            std::lock_guard lock(shader_dialog->mutex);
            shader_reply=std::move(shader_dialog->reply);shader_dialog->reply.reset();
        }
        if(shader_reply) {
            if(!shader_reply->error.empty())menu->shader_status="FILE DIALOG: "+shader_reply->error;
            else if(!shader_reply->cancelled) {
                auto candidate=menu->settings;candidate.shader_preset=shader_reply->path;candidate.crt=Frontend::Crt::retroarch;
                pending_shader_settings=std::move(candidate);
            }
        }
        apply_pending_shader();
        if(input_manager->menu_pressed()) {
            if(menu->is_open())menu_action(menu->key(SDLK_ESCAPE));
            else {menu->open(game);reset_pacing();sync_audio();}
        }
        const bool wants_cockpit=audio_output->settings.profile!=Frontend::AudioProfile::original;
        if(wants_cockpit!=cockpit_enabled){game.capture_cockpit(wants_cockpit);cockpit_enabled=wants_cockpit;}
        game.capture_feedback(feedback->settings.enabled);
        feedback->update(game.paused() || menu->is_open());
        GameControls input=input_manager->read(menu->is_open());
        if(!menu->is_open() && !game.paused() && input_manager->focused)touch.merge(input,touch_aspect());

        // Keep input polling and presentation responsive 1:1 with simulated
        // frames. Every simulated frame executes all CPU/device work and is
        // presented immediately, then paced to the exact frame period.
        if (!game.paused())
        {
            const bool measure=profile || frame_timings.enabled() || menu->settings.stats!=Frontend::Stats::off;
            const auto core_start=measure ? SDL_GetPerformanceCounter() : 0;
            const double scene_before=primary_scene_ms;
            advance(input);
            const auto cues=game.take_feedback();
            unsigned refill_delay=0;
            if(std::any_of(cues.sequence_events.begin(),cues.sequence_events.end(),[](const auto& cue){return cue.cue==Feedback::SequenceCue::refill;})){
                const auto playback=audio_output->status();
                refill_delay=Frontend::refill_audio_delay_ms(playback.buffered,playback.pending,playback.ready);
            }
            feedback->consume(cues,SDL_GetTicks(),refill_delay);
            feedback->update(!input_manager->focused);
            const double scene_elapsed=primary_scene_ms-scene_before;
            profile_scene+=scene_elapsed;
            if(measure) profile_core+=(SDL_GetPerformanceCounter()-core_start)*1000.0/counter_frequency-scene_elapsed;
            ++profile_frames;
            // Playback owns bounded queues and never clocks the emulated board.
            const auto audio_start=measure ? SDL_GetPerformanceCounter() : 0;
            take_audio();
            if(measure)profile_audio+=(SDL_GetPerformanceCounter()-audio_start)*1000.0/counter_frequency;
            ++simulated_frames;
            if (frame_limit && simulated_frames >= frame_limit)
                running = false;
        }

        auto planes=game.sprite_presentation();auto scene=game.sprite_scene();
        unsigned render_width=SCREEN_WIDTH;bool wide_ready=false;
        if(wide_renderer){const auto scene_start=SDL_GetPerformanceCounter();auto captured=game.wide_scene();if(!integrated_scene&&captured.ready)wide_renderer->render(captured.clips,captured.info.number,menu->settings.ground,Frontend::is_wide(menu->settings),menu->settings.hud==Frontend::Hud::original);
            auto wide=wide_renderer->view();wide_ready=wide.ready;
            if(wide_ready){render_width=wide.width;planes={wide.background,wide.sprites,wide.protection,true};scene={wide.heads,wide.fragments,true};}
            else planes.ready=false;
            profile_scene+=(SDL_GetPerformanceCounter()-scene_start)*1000.0/counter_frequency;
        }

        const bool composed=(accelerated_sprites() || menu->settings.sprites!=Frontend::SpriteFilter::none || menu->settings.clouds || menu->settings.trails || menu->settings.explosions || menu->settings.afterburner || menu->settings.ground || wide_ready) && planes.ready && !menu->is_open();
        if(!composed){
            const auto frame = game.frame().pixels;
            for(size_t i=0;i<frame.size();++i){const auto p=frame[i];texture_pixels[i]=0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;}
            if(!SDL_UpdateTexture(texture,nullptr,texture_pixels.data(),SCREEN_WIDTH*4))throw std::runtime_error(SDL_GetError());
        }
        SDL_SetRenderViewport(renderer,nullptr);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        const uint64_t shader_frame=game.info().number;
        const bool reset_shader_history=last_shader_frame!=UINT64_MAX && shader_frame<last_shader_frame;
        if(reset_shader_history && slang_renderer)slang_renderer->invalidate();
        const bool shader_active=menu->settings.crt==Frontend::Crt::retroarch && slang_renderer && slang_renderer->available();
        const bool shader_cached=shader_active && !slang_renderer->needs_frame(shader_frame);
        bool shader_capture=false;
        if(shader_active && !shader_cached) {
            const auto started=SDL_GetPerformanceCounter();
            shader_capture=slang_renderer->begin(Frontend::viewport_width(menu->settings),shader_frame);
            profile_crt+=(SDL_GetPerformanceCounter()-started)*1000.0/counter_frequency;
            if(!shader_capture)menu->shader_status=slang_renderer->error();
            else {SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);}
        }
        if(!shader_cached) {
            if(composed) {
                auto sprite_filter=menu->settings.sprites;
#ifdef AB2_NATIVE_VULKAN_SPRITES
                // Slang takes native rows. Exact 3:1 point/pixel-art reduction
                // samples ScaleFX's unchanged centre subpixel, so its four
                // analysis passes and expanded output cannot affect this input.
                if(vulkan_scaler && shader_capture && render_width==Frontend::viewport_width(menu->settings) &&
                    menu->settings.scaling!=Frontend::Scaling::linear && sprite_filter==Frontend::SpriteFilter::scalefx)
                    sprite_filter=Frontend::SpriteFilter::none;
#endif
                const auto factor=Frontend::sprite_factor(sprite_filter);
                if(!accelerated_sprites() && (!sprite_texture || sprite_texture_factor!=factor))
                    throw std::runtime_error("Sprite texture configuration lost");
                if(last_sprite_frame!=game.info().number || last_sprite_filter!=sprite_filter) {
                    const auto filter_start=SDL_GetPerformanceCounter();

                    const unsigned cloud_alpha=menu->settings.clouds?160:255,trail_alpha=menu->settings.trails?112:255,explosion_alpha=menu->settings.explosions?Presentation::explosion_opacity:255,afterburner_alpha=menu->settings.afterburner?Presentation::afterburner_opacity:255;
                    if(gpu_scaler) {
                        sprite_texture=gpu_scaler->compose(planes.background,planes.sprites,render_width,SCREEN_HEIGHT,sprite_filter,planes.protected_pixels,scene.heads,scene.fragments,cloud_alpha,trail_alpha,explosion_alpha,afterburner_alpha);
                        if(!Frontend::apply_texture_settings(sprite_texture,menu->settings))throw std::runtime_error(SDL_GetError());
#ifdef __APPLE__
                    } else if(metal_scaler) {
                        sprite_texture=metal_scaler->compose(planes.background,planes.sprites,render_width,SCREEN_HEIGHT,sprite_filter,planes.protected_pixels,scene.heads,scene.fragments,cloud_alpha,trail_alpha,explosion_alpha,afterburner_alpha);
                        if(!Frontend::apply_texture_settings(sprite_texture,menu->settings))throw std::runtime_error(SDL_GetError());
#elif defined(AB2_NATIVE_VULKAN_SPRITES)
                    } else if(vulkan_scaler) {
                        sprite_texture=vulkan_scaler->compose(planes.background,planes.sprites,render_width,SCREEN_HEIGHT,sprite_filter,planes.protected_pixels,scene.heads,scene.fragments,cloud_alpha,trail_alpha,explosion_alpha,afterburner_alpha,shader_capture);
                        if(!Frontend::apply_texture_settings(sprite_texture,menu->settings))throw std::runtime_error(SDL_GetError());
#endif
                    } else {
                        auto foreground=planes.sprites,background=planes.background;
                        if(scene.ready){effect_background.assign(planes.background.begin(),planes.background.end());effect_pixels.resize(planes.sprites.size());for(size_t i=0;i<effect_pixels.size();++i)effect_pixels[i]=Presentation::foreground(scene.fragments,scene.heads[i],cloud_alpha,trail_alpha,&effect_background[i],explosion_alpha,afterburner_alpha);foreground=effect_pixels;background=effect_background;}
                        const auto pixels=sprite_scaler.compose(background,foreground,render_width,SCREEN_HEIGHT,sprite_filter,planes.protected_pixels);
                        if(!SDL_UpdateTexture(sprite_texture,nullptr,pixels.data(),render_width*factor*4))throw std::runtime_error(SDL_GetError());
                    }
                    profile_filter+=(SDL_GetPerformanceCounter()-filter_start)*1000.0/counter_frequency;
                    ++filter_frames;
                    last_sprite_frame=game.info().number;
                    last_sprite_filter=sprite_filter;
                }
                SDL_RenderTexture(renderer,sprite_texture,nullptr,nullptr);
            } else {SDL_FRect center{float((Frontend::viewport_width(menu->settings)-SCREEN_WIDTH)/2),0,float(SCREEN_WIDTH),float(SCREEN_HEIGHT)};SDL_RenderTexture(renderer, texture, nullptr, &center);}
            if(composed && wide_ready)aircraft_renderer->draw(wide_renderer->view().player,Frontend::viewport_width(menu->settings));
            if(composed && wide_ready && menu->settings.hud!=Frontend::Hud::original)
                hud_renderer->draw(wide_renderer->view().hud,menu->settings.hud,Frontend::viewport_width(menu->settings));
            if(shader_capture) {
                const auto started=SDL_GetPerformanceCounter();
                if(!slang_renderer->end(shader_frame,1.0/GameSession::frame_seconds,
                        GameSession::frame_seconds*1000000.0,reset_shader_history))
                    menu->shader_status=slang_renderer->error();
                profile_crt+=(SDL_GetPerformanceCounter()-started)*1000.0/counter_frequency;
                last_shader_frame=shader_frame;
            }
        } else {
            const auto started=SDL_GetPerformanceCounter();slang_renderer->draw_cached();
            profile_crt+=(SDL_GetPerformanceCounter()-started)*1000.0/counter_frequency;
            if(!slang_renderer->error().empty())menu->shader_status=slang_renderer->error();
        }
        const auto crt_start=SDL_GetPerformanceCounter();
        if(menu->settings.crt!=Frontend::Crt::retroarch && !crt_renderer->draw(menu->settings)) {
            std::cerr<<"CRT display unavailable; switching Off: "<<SDL_GetError()<<'\n';
            menu->settings.crt=Frontend::Crt::off;crt_renderer->clear();menu->notice="CRT DISPLAY UNAVAILABLE";
            accepted_settings.crt=Frontend::Crt::off;
        }
        profile_crt+=(SDL_GetPerformanceCounter()-crt_start)*1000.0/counter_frequency;
        SDL_Rect safe_area{int((Frontend::viewport_width(menu->settings)-SCREEN_WIDTH)/2),0,SCREEN_WIDTH,SCREEN_HEIGHT};
        SDL_SetRenderViewport(renderer,&safe_area);

        if(menu->is_open())menu->draw();
        else {menu->draw_stats(performance,game.paused());menu->draw_practice_status();}
        if(!menu->is_open())touch.draw(renderer);
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
            const auto present_end=SDL_GetPerformanceCounter();
            profile_present+=(present_end-present_start)*1000.0/counter_frequency;
            if (game.paused())
            {
                SDL_Delay(10);
                previous_counter = SDL_GetPerformanceCounter();
            }
            else
            {
                uint64_t counter = present_end;
                // Bound catch-up after a suspended window or debugger pause.
                pacing.elapsed(pacing_seconds((counter - previous_counter) / counter_frequency));
                previous_counter = counter;
                if (pacing.delay()>0)
                {
                    for (;;)
                    {
                        counter = SDL_GetPerformanceCounter();
                        const double elapsed = (counter - previous_counter) / counter_frequency;
                        const double remaining = pacing.delay(elapsed);
                        if (remaining <= 0.0)
                        {
                            pacing.waited(pacing_seconds(elapsed));
                            previous_counter = counter;
                            break;
                        }
                        if (remaining > 0.0045)
                            SDL_Delay(1);
                    }
                }
                pacing.presented();
            }
            if(frame_timings.enabled())frame_timings.finish(game.info().number,game.paused(),present_end,previous_counter,timing_phases());
            const auto now=previous_counter;
            const double seconds=(now-profile_start)/counter_frequency;
            if(seconds>=1.0) {
                performance={profile_frames/seconds,profile_displays/seconds,seconds*1000/profile_displays,
                    profile_frames?profile_core/profile_frames:0,profile_present/profile_displays,
                    filter_frames?profile_filter/filter_frames:0};
                if(profile && !game.paused()) {
                    const auto audio=audio_output->status();
                    const auto title=std::string("AfterBurner2-Recomp — simulation ")+std::to_string(profile_frames/seconds)+" FPS";
                    SDL_SetWindowTitle(window,title.c_str());
                    std::cout<<"Application performance: simulation_fps="<<profile_frames/seconds
                             <<" display_fps="<<profile_displays/seconds
                             <<" filter_submit_ms="<<(filter_frames?profile_filter/filter_frames:0)
                             <<" board_ms="<<(profile_frames?profile_core/profile_frames:0)
                             <<" audio_submit_ms="<<(profile_frames?profile_audio/profile_frames:0)
                             <<" wide_scene_ms="<<profile_scene/profile_displays
                             <<" scene_renderer="<<(wide_renderer?(integrated_scene?"integrated":"snapshot"):"native")
                             <<" crt_submit_ms="<<profile_crt/profile_displays
                             <<" crt_cache_bytes="<<crt_renderer->cached_bytes()
                             <<" crt_rebuilds="<<crt_renderer->rebuilds()
                             <<" shader="<<(shader_active?(shader_cached?"cached":"active"):"off")
                             <<" shader_parameters="<<(slang_renderer?slang_renderer->parameters().size():0)
                             <<" present_wait_ms="<<profile_present/profile_displays
                             <<" audio_buffer_ms="<<audio.buffered*1000
                             <<" audio_pending_ms="<<audio.pending*1000
                             <<" audio_peak_buffer_ms="<<audio.peak_buffered*1000
                             <<" audio_peak_pending_ms="<<audio.peak_pending*1000
                             <<" audio_underruns="<<audio.underruns
                             <<" audio_sdl_priming="<<audio.sdl_priming
                             <<" audio_reprimes="<<audio.reprimes
                             <<" audio_recoveries="<<audio.recoveries<<'\n';
                }
                profile_start=now; profile_frames=profile_displays=0; profile_core=profile_present=0; profile_filter=0;profile_scene=0;profile_crt=0;profile_audio=0;filter_frames=0;
            }
        }
    }

    music_player->output=nullptr;menu->audio=nullptr;audio_output.reset();
    shader_dialog.reset();slang_renderer.reset();menu.reset();aircraft_renderer.reset();crt_renderer.reset();hud_renderer.reset();feedback.reset();input_manager.reset();music_player.reset();
    if(!accelerated_sprites())SDL_DestroyTexture(sprite_texture);
    release_sprite_scalers();
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

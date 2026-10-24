// Exercise the real frontend loop and its renderer-switch lambda. SDL entry
// points are observed only in this translation unit; production has no test hook.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
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
#include "frontend/native_player_aircraft.hpp"
#include "frontend/controller_database.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
#ifdef _WIN32
constexpr const char* shader_renderer_name="vulkan";
#else
constexpr const char* shader_renderer_name="metal";
#endif
void require_shader(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message+": "+SDL_GetError());
}
class ObservedGameSession:public GameSession {
public:
    inline static ObservedGameSession* current{};
    std::vector<int16_t> last_nonempty_pcm;
    uint64_t pcm_frames{},pcm_samples{};
    explicit ObservedGameSession(Roms& rom):GameSession(rom){current=this;}
    ~ObservedGameSession(){current=nullptr;}
    std::vector<int16_t> take_audio(){
        auto result=GameSession::take_audio();
        if(!result.empty()){last_nonempty_pcm=result;++pcm_frames;pcm_samples+=result.size();}
        return result;
    }
};
struct ShaderMainState {
    enum class Step {initial,metal,gpu,rollback,finished};
    Step step{Step::initial};
    std::string valid,invalid,config,failure;
    GameSession::FrameInfo native_info;
    std::vector<RGB888> native_rgb;
    std::vector<int16_t> native_pcm;
    uint64_t native_pcm_frames{},native_pcm_samples{};
    unsigned gpu_creations{},metal_creations{},dialogs{};
} shader_test;

void key(SDL_Keycode code) {
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=code;event.key.down=true;
    require_shader(SDL_PushEvent(&event),"Shader main key injection failed");
}
void quit_shader_test() {
    SDL_Event event{};event.type=SDL_EVENT_QUIT;SDL_PushEvent(&event);
}
void same_native_output() {
    const auto* game=ObservedGameSession::current;require_shader(game,"Observed game is unavailable");
    const auto info=game->info();const auto rgb=game->frame().pixels;
    require_shader(game->paused(),"Renderer switching resumed a paused board");
    require_shader(info.number==shader_test.native_info.number && info.time_ns==shader_test.native_info.time_ns,
        "Renderer switching advanced native board time");
    require_shader(rgb.size()==shader_test.native_rgb.size() &&
        !std::memcmp(rgb.data(),shader_test.native_rgb.data(),rgb.size_bytes()),
        "Renderer switching changed native RGB");
    require_shader(game->last_nonempty_pcm==shader_test.native_pcm &&
        game->pcm_frames==shader_test.native_pcm_frames && game->pcm_samples==shader_test.native_pcm_samples,
        "Renderer switching changed native PCM or returned new audio while paused");
}
SDL_Renderer* observed_gpu_renderer(SDL_GPUDevice* device,SDL_Window* window) {
    auto* renderer=SDL_CreateGPURenderer(device,window);
    if(renderer)++shader_test.gpu_creations;
    return renderer;
}
SDL_Renderer* observed_renderer(SDL_Window* window,const char* name) {
    auto* renderer=SDL_CreateRenderer(window,name);
    if(renderer && std::string_view(SDL_GetRendererName(renderer))==shader_renderer_name)++shader_test.metal_creations;
    return renderer;
}
void observed_file_dialog(SDL_DialogFileCallback callback,void* userdata,SDL_Window*,
        const SDL_DialogFileFilter* filters,int count,const char*,bool many) {
    require_shader(filters && count==1 && !many && std::string_view(filters[0].pattern)=="slangp",
        "Shader main chooser did not request one Slang preset");
    ++shader_test.dialogs;
    const std::string& path=shader_test.dialogs==1?shader_test.valid:shader_test.invalid;
    // Exercise SDL's permitted worker-thread callback. The production callback
    // must queue the result; renderer/menu operations stay in the main loop.
    std::jthread completion([callback,userdata,selected=path] {
        const char* files[]{selected.c_str(),nullptr};callback(userdata,files,0);
    });
}
bool observed_present(SDL_Renderer* renderer) {
    const bool result=SDL_RenderPresent(renderer);
    if(!result || !shader_test.failure.empty())return result;
    try {
        auto* game=ObservedGameSession::current;require_shader(game,"Observed game is unavailable");
        const bool metal=std::string_view(SDL_GetRendererName(renderer))==shader_renderer_name;
        using Step=ShaderMainState::Step;
        if(shader_test.step==Step::initial) {
            require_shader(shader_test.gpu_creations==1 && !metal && game->info().number>=600,
                "Shader main did not start with the normal GPU frontend and physical boot");
            shader_test.native_info=game->info();const auto rgb=game->frame().pixels;
            shader_test.native_rgb.assign(rgb.begin(),rgb.end());shader_test.native_pcm=game->last_nonempty_pcm;
            shader_test.native_pcm_frames=game->pcm_frames;shader_test.native_pcm_samples=game->pcm_samples;
            require_shader(!shader_test.native_pcm.empty(),"Shader main PCM baseline was empty");
            // Pause, open Options/Video, then select RetroArch at row 11. With
            // no saved preset this invokes the real main-thread chooser path.
            key(SDLK_P);key(SDLK_ESCAPE);key(SDLK_DOWN);key(SDLK_RETURN);key(SDLK_RETURN);
            for(unsigned i=0;i<11;++i)key(SDLK_DOWN);
            key(SDLK_RIGHT);key(SDLK_RIGHT);key(SDLK_RIGHT);
            shader_test.step=Step::metal;
        } else if(shader_test.step==Step::metal) {
            same_native_output();
            require_shader(metal && shader_test.metal_creations==1 && shader_test.dialogs==1,
                "Valid preset did not switch the live main renderer to the native shader backend");
            const auto saved=Frontend::load_settings(shader_test.config,Frontend::pixel_art_available());
            require_shader(saved.crt==Frontend::Crt::retroarch && saved.shader_preset==shader_test.valid,
                "Accepted shader preset was not persisted");
            key(SDLK_LEFT);shader_test.step=Step::gpu;
        } else if(shader_test.step==Step::gpu) {
            same_native_output();
            require_shader(!metal && shader_test.gpu_creations==2,
                "Leaving RetroArch did not restore the normal GPU frontend");
            // Select Shader Preset row 14; the second chooser supplies a
            // preset with a missing pass, forcing a real compile/load error.
            key(SDLK_DOWN);key(SDLK_DOWN);key(SDLK_DOWN);key(SDLK_RETURN);
            shader_test.step=Step::rollback;
        } else if(shader_test.step==Step::rollback) {
            same_native_output();
            require_shader(!metal && shader_test.gpu_creations==3 && shader_test.metal_creations==2 && shader_test.dialogs==2,
                "Invalid preset did not roll back the main renderer to GPU");
            const auto saved=Frontend::load_settings(shader_test.config,Frontend::pixel_art_available());
            require_shader(saved.crt==Frontend::Crt::arcade && saved.shader_preset==shader_test.valid,
                "Invalid shader preset replaced accepted settings");
            shader_test.step=Step::finished;quit_shader_test();
        }
    } catch(const std::exception& error) {
        shader_test.failure=error.what();quit_shader_test();
    }
    return result;
}
}

#define GameSession ObservedGameSession
#define SDL_CreateRenderer observed_renderer
#define SDL_CreateGPURenderer observed_gpu_renderer
#define SDL_ShowOpenFileDialog observed_file_dialog
#define SDL_RenderPresent observed_present
#define main embedded_shader_main
#include "../src/main/main.cpp"
#undef main
#undef SDL_RenderPresent
#undef SDL_ShowOpenFileDialog
#undef SDL_CreateGPURenderer
#undef SDL_CreateRenderer
#undef GameSession

int main(int argc,char** argv)try {
    SDL_SetMainReady();
    require_shader(argc==2 || argc==3,"Usage: native_shader_main_integration preset.slangp [librashader.dylib]");
    shader_test.valid=std::filesystem::absolute(argv[1]).string();
    const auto directory=std::filesystem::path("build/oracle/shader-main")/
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(directory);
    shader_test.config=(directory/"settings.cfg").string();
    shader_test.invalid=std::filesystem::absolute(directory/"invalid.slangp").string();
    {std::ofstream file(shader_test.invalid);file<<"shaders = 1\nshader0 = \"missing/shader.slang\"\n";}
    {std::ofstream file(shader_test.config);file<<"AB2SETTINGS1\nviewport=16:10\nhud=sharp\nsprites=none\n";}
    std::vector<std::string> arguments{"shader-main","--boot-to-flight","--scene-renderer","integrated","--settings-file",shader_test.config};
    if(argc==3){arguments.push_back("--shader-runtime");arguments.emplace_back(argv[2]);}
    std::vector<char*> pointers;for(auto& argument:arguments)pointers.push_back(argument.data());
    std::atomic_bool timeout{};
    std::jthread watchdog([&](std::stop_token stop){
        for(unsigned i=0;i<1800 && !stop.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if(!stop.stop_requested()){timeout=true;quit_shader_test();}
    });
    const int status=embedded_shader_main(int(pointers.size()),pointers.data());
    watchdog.request_stop();
    require_shader(!timeout,"Shader main integration timed out");
    require_shader(shader_test.failure.empty(),shader_test.failure);
    require_shader(status==0 && shader_test.step==ShaderMainState::Step::finished,"Shader main integration did not finish every transition");
    std::cout<<"Native shader main: real GPU -> "<<shader_renderer_name<<" -> GPU, queued chooser, failed-preset GPU rollback and accepted settings; paused native RGB/PCM/time identical, 0 skipped.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

// AfterBurner2-Recomp
//
// Application entry point for the ROM-backed flight/combat prototype.
// Simulation advances at 60 Hz independently of the display refresh rate.

#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <charconv>
#include <string_view>
#include <vector>
#include "roms.hpp"
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/screen.hpp"
#include "engine/oengine.hpp"
#include "hwaudio/arcade_audio.hpp"

namespace
{
    constexpr int SCREEN_WIDTH = 320;
    constexpr int SCREEN_HEIGHT = 224;

}

int main(int argc, char** argv)
{
    unsigned frame_limit = 0;
    if (argc != 1)
    {
        if (argc != 3 || std::string_view(argv[1]) != "--frames")
        { std::cerr << "Usage: afterburner2 [--frames positive-count]\n"; return 1; }
        const std::string_view value(argv[2]);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), frame_limit);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !frame_limit)
        { std::cerr << "Frame count must be a positive integer.\n"; return 1; }
    }
    unsigned simulated_frames = 0;
    if (!roms.load_aburner2())
    {
        std::cerr << "Failed to load one or more ROM files from roms/ "
                     "(see roms/README.md)." << std::endl;
        return 1;
    }
    std::cout << "ROM set loaded OK." << std::endl;

    HWPalette palette(8192);
    HWRoad road(0x1700, 0x1720, 0x1780, -166);
    road.init(roms.road.rom, roms.road.length);

    HWSprites sprites;
    sprites.init(roms.sprites.rom, roms.sprites.length);

    HWTiles tiles;
    tiles.init_gfx(roms.tiles.rom, roms.tiles.length);

    OEngine engine;
    engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length, roms.subcpu.rom, roms.subcpu.length);

    Screen screen(SCREEN_WIDTH, SCREEN_HEIGHT, palette, road, sprites, tiles);
    screen.init_stage1(roms.sprites.rom, roms.sprites.length);
    std::vector<RGB888> frame(static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT);
    std::vector<uint32_t> texture_pixels(frame.size());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    constexpr int SCALE = 3;
    SDL_Window* window = SDL_CreateWindow(
        "AfterBurner2-Recomp",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

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

    SDL_AudioDeviceID audio_device = 0;
    std::unique_ptr<ArcadeAudio> audio;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
    {
        SDL_AudioSpec spec{};
        spec.freq = ArcadeAudio::SAMPLE_RATE;
        spec.format = AUDIO_S16SYS; spec.channels = 2; spec.samples = 1024;
        audio_device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        if (audio_device)
        {
            audio = std::make_unique<ArcadeAudio>(roms);
            SDL_PauseAudioDevice(audio_device, 0);
        }
    }
    if (!audio_device) std::cerr << "Audio unavailable: " << SDL_GetError() << '\n';

    const double tick_seconds = 1.0 / 60.0;
    const double counter_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    uint64_t previous_counter = SDL_GetPerformanceCounter();
    double accumulated_seconds = tick_seconds;
    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN)
            {
                engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length,
                            roms.subcpu.rom, roms.subcpu.length);
                if (audio_device)
                {
                    SDL_ClearQueuedAudio(audio_device);
                    audio.reset();
                    audio = std::make_unique<ArcadeAudio>(roms);
                }
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }

        if (!running) break;
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        int stick_x = 0;
        int stick_y = 0;
        if (keys[SDL_SCANCODE_LEFT])  stick_x -= 1;
        if (keys[SDL_SCANCODE_RIGHT]) stick_x += 1;
        if (keys[SDL_SCANCODE_UP])    stick_y -= 1;
        if (keys[SDL_SCANCODE_DOWN])  stick_y += 1;

        bool fire_vulcan = keys[SDL_SCANCODE_Z];
        bool fire_missile = keys[SDL_SCANCODE_X];
        int throttle = keys[SDL_SCANCODE_SPACE] ? 2 : 1;

        const uint64_t counter = SDL_GetPerformanceCounter();
        // Bound catch-up after a suspended window or debugger pause.
        accumulated_seconds += std::min(0.25, (counter - previous_counter) / counter_frequency);
        previous_counter = counter;
        if (accumulated_seconds < tick_seconds)
        {
            SDL_Delay(1);
            continue;
        }
        while (accumulated_seconds >= tick_seconds)
        {
            engine.handle_input(stick_x, stick_y, fire_vulcan, fire_missile, throttle);
            engine.tick(palette, road, sprites, tiles);
            for (const auto command : engine.get_objects().take_audio_commands())
                if (audio) audio->command(command);
            if (audio)
            {
                const auto samples = audio->render(ArcadeAudio::SAMPLE_RATE / 60);
                if (SDL_GetQueuedAudioSize(audio_device) < ArcadeAudio::SAMPLE_RATE)
                    SDL_QueueAudio(audio_device, samples.data(), static_cast<uint32_t>(samples.size() * sizeof(int16_t)));
            }
            accumulated_seconds -= tick_seconds;
            if (frame_limit && ++simulated_frames >= frame_limit)
            { running = false; break; }
        }

        screen.set_camera(engine.get_objects().get_roll(), engine.get_objects().get_pitch(), engine.get_objects().distance());
        screen.render_frame(frame.data());
        for (size_t i = 0; i < frame.size(); i++)
        {
            const RGB888& p = frame[i];
            texture_pixels[i] = 0xFF000000u | (static_cast<uint32_t>(p.r) << 16) |
                                 (static_cast<uint32_t>(p.g) << 8) | p.b;
        }
        SDL_UpdateTexture(texture, nullptr, texture_pixels.data(), SCREEN_WIDTH * 4);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    if (audio_device) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

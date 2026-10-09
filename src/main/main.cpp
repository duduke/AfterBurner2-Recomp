// AfterBurner2-Recomp
//
// Application entry point for the literal joint X-Board host.
// Physical frame/CPU/device clocks are independent of display refresh.

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
#include "engine/game_session.hpp"

namespace
{
    constexpr int SCREEN_WIDTH = GameSession::width;
    constexpr int SCREEN_HEIGHT = GameSession::height;

}

int main(int argc, char** argv)try
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

    GameSession game(roms);
    const size_t frame_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    std::vector<uint32_t> texture_pixels(frame_pixels);

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
    constexpr int sample_rate = GameSession::sample_rate;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
    {
        SDL_AudioSpec spec{};
        spec.freq = sample_rate;
        spec.format = AUDIO_S16SYS; spec.channels = 2; spec.samples = 1024;
        audio_device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        if (audio_device)
        {
            SDL_PauseAudioDevice(audio_device, 0);
        }
    }
    if (!audio_device) std::cerr << "Audio unavailable: " << SDL_GetError() << '\n';

    const double tick_seconds = GameSession::frame_seconds;
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
            if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym == SDLK_F5)
            {
                game.restart();
                if (audio_device) SDL_ClearQueuedAudio(audio_device);
                if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
                accumulated_seconds = 0;
                previous_counter = SDL_GetPerformanceCounter();
                SDL_SetWindowTitle(window, "AfterBurner2-Recomp");
            }
            if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym == SDLK_p)
            {
                game.set_paused(!game.paused());
                if (audio_device) SDL_PauseAudioDevice(audio_device, game.paused());
                accumulated_seconds = 0;
                previous_counter = SDL_GetPerformanceCounter();
                SDL_SetWindowTitle(window, game.paused() ? "AfterBurner2-Recomp — Paused (P to resume)" : "AfterBurner2-Recomp");
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }

        if (!running) break;
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        GameControls input;
        // Preserve the existing keyboard precedence when opposite keys are
        // held. Other devices supply normalized axes through the same API.
        input.stick_x = keys[SDL_SCANCODE_RIGHT] ? 1 : keys[SDL_SCANCODE_LEFT] ? -1 : 0;
        input.stick_y = keys[SDL_SCANCODE_DOWN] ? -1 : keys[SDL_SCANCODE_UP] ? 1 : 0;
        input.throttle = keys[SDL_SCANCODE_LSHIFT] ? -1 : keys[SDL_SCANCODE_SPACE] ? 1 : 0;
        input.cannon = keys[SDL_SCANCODE_Z];
        input.missile = keys[SDL_SCANCODE_X];
        input.coin = keys[SDL_SCANCODE_5];
        input.start = keys[SDL_SCANCODE_RETURN];

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
            game.advance(input);
            // Backpressure bounds playback latency without dropping sound.
            // The board still advances when no speaker device is available.
            constexpr unsigned queue_limit_bytes = sample_rate * 2 * sizeof(int16_t) / 10;
            const auto samples = game.take_audio();
            if (!samples.empty())
            {
                if (audio_device)
                {
                    while (SDL_GetQueuedAudioSize(audio_device) > queue_limit_bytes)
                    {
                        if (SDL_GetAudioDeviceStatus(audio_device) != SDL_AUDIO_PLAYING)
                        {
                            std::cerr << "Audio device stopped; continuing without a speaker.\n";
                            SDL_CloseAudioDevice(audio_device); audio_device = 0;
                            break;
                        }
                        SDL_Delay(1);
                    }
                    if (audio_device && SDL_QueueAudio(audio_device, samples.data(), static_cast<uint32_t>(samples.size() * sizeof(int16_t))) < 0)
                    {
                        std::cerr << "Audio queue failed: " << SDL_GetError() << '\n';
                        SDL_CloseAudioDevice(audio_device); audio_device = 0;
                    }
                }
            }
            accumulated_seconds -= tick_seconds;
            if (frame_limit && ++simulated_frames >= frame_limit)
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
catch(const std::exception& error)
{
    std::cerr << "X-Board host stopped: " << error.what() << '\n';
    SDL_Quit();
    return 1;
}

// AfterBurner2-Recomp
//
// Application entry point. Currently: loads ROMs, brings up the X-Board
// video hardware layer (hwvideo/*, see docs/HARDWARE.md), and pokes a
// hand-built test scene into tile/palette RAM so the render pipeline is
// visibly exercised end-to-end. There is no ported game logic yet — that's
// the next phase (see docs/ROADMAP.md) — so nothing here is driven by the
// real 68000 program.

#include <SDL.h>
#include <iostream>
#include <vector>
#include "roms.hpp"
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/screen.hpp"
#include "engine/oengine.hpp"

namespace
{
    constexpr int SCREEN_WIDTH = 320;
    constexpr int SCREEN_HEIGHT = 224;

}

int main(int, char**)
{
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
    engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length);

    Screen screen(SCREEN_WIDTH, SCREEN_HEIGHT, palette, road, sprites, tiles);
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

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }

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

        engine.handle_input(stick_x, stick_y, fire_vulcan, fire_missile, throttle);
        engine.tick(palette, road, sprites, tiles);

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

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

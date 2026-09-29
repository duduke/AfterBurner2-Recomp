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

namespace
{
    constexpr int SCREEN_WIDTH = 320;
    constexpr int SCREEN_HEIGHT = 224;

    // Temporary hand-built scene: a handful of tiles on the foreground
    // layer, in a couple of colors, so the tile/palette/compositor path has
    // something visible to draw. Replace once real game logic drives this.
    void build_test_scene(HWPalette& palette, HWTiles& tiles)
    {
        // Palette entries 0x1c00 + color*8 + pixel, pixel 1..7 visible (0 = transparent).
        // color 0: a blue-ish ramp; color 1: a green-ish ramp.
        for (int pixel = 1; pixel < 8; pixel++)
        {
            const uint16_t shade = static_cast<uint16_t>(pixel * 4);
            palette.write(0x1c00 + 0 * 8 + pixel, static_cast<uint16_t>(shade << 8 | shade));       // blue+green bits
            palette.write(0x1c00 + 1 * 8 + pixel, static_cast<uint16_t>(shade << 4));                // green bits
        }

        // Fill a 16x16 tile block near the top-left of page 0 with tile #1 in color 0,
        // and an adjacent block with tile #2 in color 1 (whatever those ROM tiles decode
        // to — this is just to prove tiles/colors/positions all line up correctly).
        for (int ty = 2; ty < 10; ty++)
        {
            for (int tx = 2; tx < 10; tx++)
                tiles.write_tile(static_cast<uint32_t>(ty * 64 + tx), (1 << 6) | 1); // color1, tile1

            for (int tx = 12; tx < 20; tx++)
                tiles.write_tile(static_cast<uint32_t>(ty * 64 + tx), (0 << 6) | 2); // color0, tile2
        }

        // FG layer: page 0 in every quadrant, no scroll.
        tiles.write_text(0x740, 0x0000);
        tiles.write_text(0x748, 0);
        tiles.write_text(0x74C, 0);
        tiles.latch_frame_registers();
    }
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

    build_test_scene(palette, tiles);

    Screen screen(SCREEN_WIDTH, SCREEN_HEIGHT, palette, road, sprites, tiles);
    std::vector<RGB888> frame(static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT);
    std::vector<uint32_t> texture_pixels(frame.size());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "AfterBurner2-Recomp",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
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

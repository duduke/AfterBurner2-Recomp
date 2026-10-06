// Headless visual check using the user's local ROM set. Writes a binary PPM.
#include <fstream>
#include <iostream>
#include <vector>
#include "roms.hpp"
#include "engine/oengine.hpp"
#include "hwvideo/screen.hpp"

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4)
    {
        std::cerr << "Usage: render_scene output.ppm [right|left|level|combat] [ticks]\n";
        return 1;
    }
    const std::string direction = argc >= 3 ? argv[2] : "";
    if (!direction.empty() && direction != "right" && direction != "left" && direction != "level" && direction != "combat")
        return 1;
    if (!roms.load_aburner2()) return 1;

    HWPalette palette(8192);
    HWRoad road(0x1700, 0x1720, 0x1780, -166);
    road.init(roms.road.rom, roms.road.length);
    HWSprites sprites;
    sprites.init(roms.sprites.rom, roms.sprites.length);
    HWTiles tiles;
    tiles.init_gfx(roms.tiles.rom, roms.tiles.length);
    OEngine engine;
    engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length, roms.subcpu.rom, roms.subcpu.length);
    Screen screen(320, 224, palette, road, sprites, tiles);
    // Road attitude is driven through the arcade input and ADC pipeline.
    const int ticks = argc == 4 ? std::stoi(argv[3]) : 240;
    for (int i = 0; i < ticks; ++i)
    {
        const int stick = direction == "right" ? 1 : direction == "left" ? -1 : 0;
        engine.handle_input(stick, 0, direction == "combat", direction == "combat", 1);
        engine.tick(palette, road, sprites, tiles);
    }
    std::vector<RGB888> frame(320 * 224);
    screen.render_frame(frame.data());
    std::ofstream output(argv[1], std::ios::binary);
    output << "P6\n320 224\n255\n";
    for (const auto pixel : frame)
    {
        output.put(static_cast<char>(pixel.r));
        output.put(static_cast<char>(pixel.g));
        output.put(static_cast<char>(pixel.b));
    }
    return output ? 0 : 1;
}

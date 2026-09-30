#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "engine/oengine.hpp"
#include "hwvideo/screen.hpp"
#include "hwaudio/arcade_audio.hpp"

void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main()
{
    if (!roms.load_aburner2()) return 1;
    HWPalette palette(8192);
    HWRoad road(0x1700, 0x1720, 0x1780, -166);
    road.init(roms.road.rom, roms.road.length);
    HWSprites sprites; sprites.init(roms.sprites.rom, roms.sprites.length);
    HWTiles tiles; tiles.init_gfx(roms.tiles.rom, roms.tiles.length);
    OEngine engine;
    engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length,
                roms.subcpu.rom, roms.subcpu.length);
    Screen screen(320, 224, palette, road, sprites, tiles);
    screen.init_stage1(roms.sprites.rom, roms.sprites.length);
    std::vector<RGB888> level(320 * 224), banked(level.size());
    const auto jet = read_sprite_frame(roms.subcpu.rom, roms.subcpu.length, 0x100f0);
    require(jet.bank == 0 && jet.offset == 0xab2 && jet.height == 47, "Native player frame table mismatch");
    for (int tick = 0; tick < 3600; ++tick)
    {
        const int stick = tick > 600 && tick < 720 ? 1 : tick > 900 && tick < 1020 ? -1 : 0;
        engine.handle_input(stick, 0, true, true, 1);
        engine.tick(palette, road, sprites, tiles);
        if (tick == 360 || tick == 700)
        {
            screen.set_camera(engine.get_objects().get_roll(), engine.get_objects().get_pitch(), engine.get_objects().distance());
            screen.render_frame(tick == 360 ? level.data() : banked.data());
        }
        // Headless checks consume the same event queue as the app.
        engine.get_objects().take_audio_commands();
    }
    require(engine.get_objects().hud().score > 0, "Stage 1 weapons must hit fighters and update score");
    require(engine.get_objects().hud().missiles < 50, "Missile inventory must update");
    require(engine.get_objects().is_completed(), "Stage 1 must reach its clear state");
    const auto old_distance = engine.get_objects().distance();
    engine.handle_input(0, 0, true, true, 1); engine.tick(palette, road, sprites, tiles);
    require(engine.get_objects().distance() == old_distance, "Cleared stage must stop advancing terrain");
    const auto same = [](RGB888 a, RGB888 b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
    for (int y = 8; y < 32; ++y)
        for (int x = 16; x < 192; ++x)
            if (x < 72 || x >= 160) require(same(level[y * 320 + x], banked[y * 320 + x]), "Banking must leave score/hits HUD upright and stable");
    unsigned changed = 0;
    for (size_t i = 0; i < level.size(); ++i) changed += !same(level[i], banked[i]);
    require(changed > 10000, "Banking must change the projected terrain across the viewport");
    engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length,
                roms.subcpu.rom, roms.subcpu.length);
    require(engine.get_objects().hud().score == 0 && engine.get_objects().hud().missiles == 50 &&
            !engine.get_objects().is_completed(), "Restart must restore Stage 1 state");
    for (int tick = 0; tick < 600; ++tick)
    {
        engine.handle_input(0, 0, false, false, 1);
        engine.tick(palette, road, sprites, tiles);
        engine.get_objects().take_audio_commands();
    }
    require(engine.get_objects().hud().hits > 0, "Enemy missiles must reach the player and update HITS");
    engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length,
                roms.subcpu.rom, roms.subcpu.length);
    ArcadeAudio audio(roms);
    for (auto command : engine.get_objects().take_audio_commands()) audio.command(command);
    double energy = 0;
    for (int tick = 0; tick < 240; ++tick)
    {
        if (tick == 120) audio.command(0x90);
        if (tick == 180) audio.command(0x40);
        const auto samples = audio.render(800);
        require(samples.size() == 1600, "Audio output must contain 800 stereo frames per game tick");
        for (auto sample : samples) energy += double(sample) * sample;
    }
    require(energy > 1000000, "Original sound ROM must generate audible output");
    std::cout << "Stage 1 integration passed; audio RMS " << std::sqrt(energy / (240 * 1600)) << '\n';
}

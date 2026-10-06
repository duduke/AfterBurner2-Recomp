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
    std::vector<uint16_t> hud_before(4*64),hud_after(hud_before.size());
    std::vector<RGB888> level(320 * 224), banked(level.size());
    const auto jet = read_sprite_frame(roms.subcpu.rom, roms.subcpu.length, 0x100f0);
    require(jet.bank == 0 && jet.offset == 0xab2 && jet.height == 47, "Native player frame table mismatch");
    for (int tick = 0; tick < 7200; ++tick)
    {
        const int stick = tick > 600 && tick < 720 ? 1 : tick > 900 && tick < 1020 ? -1 : 0;
        engine.handle_input(stick, 0, true, true, 1);
        // Road attitude comes from the arcade bus and is driven naturally
        // by the ported input and ADC pipeline.
        engine.tick(palette, road, sprites, tiles);
        if (tick == 650) {
            require(engine.arcade_bus().read16(0x29c0ce) == 0x007f, "Right stick must bank aircraft right ($7F)");
            require((engine.arcade_bus().read8(0x29c076) & 0x30) == 0x30, "Vulcan and missile buttons must register in arcade RAM");
        }
        if (tick == 800) {
            require(engine.arcade_bus().read16(0x29c0ce) == 0, "Neutral stick must return bank target to zero");
        }
        if (tick == 950) {
            require(int16_t(engine.arcade_bus().read16(0x29c0ce)) < 0, "Left stick must bank aircraft left");
        }
        if (tick == 360 || tick == 700)
        {
            screen.render_frame(tick == 360 ? level.data() : banked.data());
            auto& text=tick==360?hud_before:hud_after;
            for (unsigned i=0;i<text.size();++i) text[i]=tiles.read_text(i);
        }
        // Headless checks consume the same event queue as the app.
        engine.get_objects().take_audio_commands();
        engine.take_sound_commands();
    }
    require(engine.get_objects().hud().score > 0, "Stage 1 weapons must hit fighters and update score");
    require(engine.get_objects().hud().missiles < 50, "Missile inventory must update");
    require(engine.get_objects().is_completed(), "Native Stage 1 must clear after 3600 logic ticks (7200 display ticks)");
    const auto old_distance = engine.get_objects().distance();
    engine.handle_input(0, 0, true, true, 1); engine.tick(palette, road, sprites, tiles);
    require(engine.get_objects().distance() == old_distance, "Cleared stage must stop advancing terrain");
    const auto same = [](RGB888 a, RGB888 b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
    // Compare the text layer itself: transparent HUD pixels legitimately
    // reveal a different road when banked, so their RGB need not be equal.
    for (int y=1;y<4;++y)
        for (int x=2;x<24;++x)
            if (x<9 || x>=20) require(hud_before[y*64+x]==hud_after[y*64+x],
                                    "Arcade road banking must leave HUD text stable");
    unsigned changed = 0;
    for (size_t i = 0; i < level.size(); ++i) changed += !same(level[i], banked[i]);
    require(changed > 10000, "Arcade bank target must change the hardware road across the viewport");
    engine.init(palette, road, sprites, tiles, roms.maincpu.rom, roms.maincpu.length,
                roms.subcpu.rom, roms.subcpu.length);
    require(engine.get_objects().hud().score == 0 && engine.get_objects().hud().missiles == 50 &&
            !engine.get_objects().is_completed(), "Restart must restore Stage 1 state");
    for (int tick = 0; tick < 1200; ++tick)
    {
        engine.handle_input(0, 0, false, false, 1);
        engine.tick(palette, road, sprites, tiles);
        engine.get_objects().take_audio_commands();
        engine.take_sound_commands();
    }
    require(engine.get_objects().hud().hits > 0, "Enemy missiles must reach the player and update HITS");

    // Verify ported sound ring and reload sequence in Stage 1 environment with real ROMs:
    require(engine.arcade_bus().read8(0xffff8902) == 0xff, "Arcade sound queue must be enabled");
    engine.arcade_bus().write16(0xffff8d66, 10);
    engine.arcade_bus().write8(0xffff9102, 2);
    std::vector<uint8_t> ported_cmds;
    for (int i = 0; i < 10; ++i) {
        engine.tick(palette, road, sprites, tiles);
        auto c = engine.take_sound_commands();
        if (!c.empty()) {
            ported_cmds = std::move(c);
            break;
        }
    }
    require(!ported_cmds.empty() && ported_cmds.front() == 0x9b,
            "Stage 1 missile reload must emit ported reload sound 0x9B through compare_timer latch");
    require(engine.arcade_bus().read16(0xffff8d66) > 10, "Missile count must increase from reload");

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

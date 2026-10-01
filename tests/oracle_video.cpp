// Differential test: the video hardware layer (hwvideo: road, tilemaps,
// sprites, palette, compositor) against frames rendered by MAME.
//
// Input is a capture from tools/oracle/capture_frames.lua: per frame, MAME's
// rendered pixels plus the video state that frame was drawn from. Each frame
// is rendered through Screen and compared pixel-for-pixel. Side-by-side
// images (MAME | port | differences in red) of the worst frames are written
// to build/oracle/video_diff_*.ppm.
//
// Usage: oracle_video [capture-file] [max-mismatched-pixels-per-frame]
// Exits non-zero if any frame has more mismatched pixels than allowed
// (default 0). Run from the repository root.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "roms.hpp"
#include "engine/omainsprites.hpp"
#include "engine/osubsprites.hpp"
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/screen.hpp"

namespace
{
    constexpr int W = 320, H = 224;

    uint16_t r16(const uint8_t* m, size_t o) { return static_cast<uint16_t>((m[o] << 8) | m[o + 1]); }

    bool pipeline_capture=false;
    struct Frame
    {
        uint32_t number = 0,entry=0;
        std::vector<uint8_t> sub_before=std::vector<uint8_t>(0x8000),blit_before=std::vector<uint8_t>(0x9000);
        std::vector<uint8_t> pixels = std::vector<uint8_t>(W * H * 4);
        std::vector<uint8_t> sprites = std::vector<uint8_t>(0x1000);
        std::vector<uint8_t> road = std::vector<uint8_t>(0x1000);
        uint16_t road_control = 0;
        std::vector<uint8_t> latched = std::vector<uint8_t>(32);
        std::vector<uint8_t> palette = std::vector<uint8_t>(0x4000);
        std::vector<uint8_t> tile_ram = std::vector<uint8_t>(0x10000);
        std::vector<uint8_t> text_ram = std::vector<uint8_t>(0x1000);
    };

    bool read(std::ifstream& in, Frame& f)
    {
        uint8_t header[4], control[2];
        in.read(reinterpret_cast<char*>(header), 4);
        in.read(reinterpret_cast<char*>(f.pixels.data()), f.pixels.size());
        in.read(reinterpret_cast<char*>(f.sprites.data()), f.sprites.size());
        in.read(reinterpret_cast<char*>(f.road.data()), f.road.size());
        in.read(reinterpret_cast<char*>(control), 2);
        in.read(reinterpret_cast<char*>(f.latched.data()), f.latched.size());
        in.read(reinterpret_cast<char*>(f.palette.data()), f.palette.size());
        in.read(reinterpret_cast<char*>(f.tile_ram.data()), f.tile_ram.size());
        in.read(reinterpret_cast<char*>(f.text_ram.data()), f.text_ram.size());
        if(pipeline_capture) {
            uint8_t entry[4];in.read(reinterpret_cast<char*>(entry),4);
            f.entry=(uint32_t(r16(entry,0))<<16)|r16(entry,2);
            in.read(reinterpret_cast<char*>(f.sub_before.data()),f.sub_before.size());
            in.read(reinterpret_cast<char*>(f.blit_before.data()),f.blit_before.size());
        }
        f.number = (static_cast<uint32_t>(header[0]) << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
        f.road_control = r16(control, 0);
        return static_cast<bool>(in);
    }

    void write_diff(const std::string& path, const Frame& f, const std::vector<RGB888>& port)
    {
        std::FILE* out = std::fopen(path.c_str(), "wb");
        if (!out) return;
        std::fprintf(out, "P6\n%d %d\n255\n", W * 3, H);
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const uint8_t* p = &f.pixels[(y * W + x) * 4];
                std::fputc(p[2], out); std::fputc(p[1], out); std::fputc(p[0], out);
            }
            for (int x = 0; x < W; ++x)
            {
                const RGB888& p = port[y * W + x];
                std::fputc(p.r, out); std::fputc(p.g, out); std::fputc(p.b, out);
            }
            for (int x = 0; x < W; ++x)
            {
                const uint8_t* m = &f.pixels[(y * W + x) * 4];
                const RGB888& p = port[y * W + x];
                const bool same = m[2] == p.r && m[1] == p.g && m[0] == p.b;
                std::fputc(same ? m[2] / 3 : 255, out);
                std::fputc(same ? m[1] / 3 : 0, out);
                std::fputc(same ? m[0] / 3 : 0, out);
            }
        }
        std::fclose(out);
    }
}

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "build/oracle/frames.bin";
    const long allowed = argc > 2 ? std::stol(argv[2]) : 0;
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::cerr << "No capture at " << path << " — run `make oracle-capture` first.\n";
        return 1;
    }
    char magic[8];
    in.read(magic, 8);
    pipeline_capture=std::memcmp(magic,"AB2PIPE1",8)==0;
    if (!in || (!pipeline_capture && std::memcmp(magic, "AB2VID01", 8) != 0))
    {
        std::cerr << path << " is not a frame capture\n";
        return 1;
    }
    if (!roms.load_aburner2())
        return 1;

    HWPalette palette(8192);
    HWRoad road(0x1700, 0x1720, 0x1780, -166);
    road.init(roms.road.rom, roms.road.length);
    HWSprites sprites;
    sprites.init(roms.sprites.rom, roms.sprites.length);
    HWTiles tiles;
    tiles.init_gfx(roms.tiles.rom, roms.tiles.length);
    Screen screen(W, H, palette, road, sprites, tiles);
    std::vector<RGB888> port(W * H);

    struct Result { uint32_t frame; long mismatched; };
    std::vector<Result> results;
    Frame f;
    long total_mismatched = 0;
    int images = 0;

    unsigned normal=0,interpolated=0;
    while (read(in, f))
    {
        if(pipeline_capture) {
            auto shared=f.sub_before;
            SubBus sub(roms.subcpu.rom,roms.subcpu.length,shared.data());
            OSubSprites{}.pass(sub);
            uint32_t sorted=sub.read32(SubBus::A6+0x7c18),count_ptr=sub.read32(SubBus::A6+0x7c20);
            unsigned count=sub.read16(count_ptr);
            auto off=[](uint32_t p){p&=0xfffff;return (p<0xa0000?0u:0x4000u)+(p&0x3fff);};
            MainBus::Memory memory;
            std::copy(f.blit_before.begin(),f.blit_before.begin()+0x8000,memory.bytes.begin()+MainBus::SHARED);
            std::copy(f.blit_before.begin()+0x8000,f.blit_before.end(),memory.bytes.begin()+MainBus::SPRITE);
            // The sub output is the tested computation; other-CPU state at
            // the later blit boundary remains its own captured input.
            for(unsigned list=0;list<2;++list) {
                uint32_t start=off(sorted)+list*0xc00;
                std::copy(shared.begin()+start,shared.begin()+start+(count+1)*16,memory.bytes.begin()+MainBus::SHARED+start);
            }
            MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
            unsigned triggers=0;
            bus.endpoints.sprite_draw=[&]{++triggers;};
            OMainSprites{}.blit(bus,f.entry==0xb7d2);bus.write8(0x110001,0xff);
            if(triggers!=1 || !std::equal(memory.sprite_buffer.begin(),memory.sprite_buffer.end(),f.sprites.begin())) {
                std::cerr << "Paired pipeline sprite RAM/draw differs on frame " << f.number << '\n';return 1;
            }
            f.entry==0xb7d2?++interpolated:++normal;
            std::copy(memory.sprite_buffer.begin(),memory.sprite_buffer.end(),f.sprites.begin());
        }
        for (uint32_t i = 0; i < 8192; ++i) palette.write(i, r16(f.palette.data(), i * 2));
        for (uint32_t i = 0; i < 0x800; ++i) road.write16(i * 2, r16(f.road.data(), i * 2));
        road.read_road_control();   // the captured half becomes the displayed one
        road.write_road_control(static_cast<uint8_t>(f.road_control));
        for (uint32_t i = 0; i < 0x800; ++i) sprites.write16(i * 2, r16(f.sprites.data(), i * 2));
        sprites.swap();
        for (uint32_t i = 0; i < 0x8000; ++i) tiles.write_tile(i, r16(f.tile_ram.data(), i * 2));
        for (uint32_t i = 0; i < 0x800; ++i) tiles.write_text(i, r16(f.text_ram.data(), i * 2));
        for (uint32_t i = 0; i < 16; ++i) tiles.write_text(0x740 + i, r16(f.latched.data(), i * 2));
        tiles.latch_frame_registers();

        screen.render_frame(port.data());

        long mismatched = 0;
        for (int i = 0; i < W * H; ++i)
        {
            const uint8_t* m = &f.pixels[i * 4];
            if (m[2] != port[i].r || m[1] != port[i].g || m[0] != port[i].b) ++mismatched;
        }
        total_mismatched += mismatched;
        results.push_back({f.number, mismatched});
        if (mismatched > allowed && images < 3)
            write_diff("build/oracle/video_diff_" + std::to_string(f.number) + ".ppm", f, port), ++images;
    }

    if(pipeline_capture) std::cout << "Paired pipeline: " << normal << " normal, " << interpolated << " interpolated sprite RAM/draw triggers identical\n";
    if (results.empty())
    {
        std::cerr << "No frames in " << path << '\n';
        return 1;
    }
    const auto identical = std::count_if(results.begin(), results.end(), [](const Result& r) { return r.mismatched == 0; });
    const auto worst = *std::max_element(results.begin(), results.end(),
                                         [](const Result& a, const Result& b) { return a.mismatched < b.mismatched; });
    const auto over = std::count_if(results.begin(), results.end(), [&](const Result& r) { return r.mismatched > allowed; });
    std::cout << "Video layer vs arcade: " << results.size() << " frames, " << identical << " pixel-identical, "
              << over << " over the limit of " << allowed << "; average "
              << total_mismatched / static_cast<long>(results.size()) << " mismatched pixels/frame (of " << W * H
              << "), worst frame " << worst.frame << " with " << worst.mismatched << '\n';
    return over == 0 ? 0 : 1;
}

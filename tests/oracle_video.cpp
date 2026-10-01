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
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"
#include "hwvideo/screen.hpp"

namespace
{
    constexpr int W = 320, H = 224;

    uint16_t r16(const uint8_t* m, size_t o) { return static_cast<uint16_t>((m[o] << 8) | m[o + 1]); }

    struct Frame
    {
        uint32_t number = 0;
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
    if (!in || std::memcmp(magic, "AB2VID01", 8) != 0)
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

    while (read(in, f))
    {
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

#include <algorithm>
#include <cmath>
#include "engine/osprites.hpp"
#include "hwvideo/screen.hpp"

Screen::Screen(int w, int h, HWPalette& pal, HWRoad& rd, HWSprites& spr, HWTiles& tm)
    : width(w), height(h), palette(pal), road(rd), sprites(spr), tiles(tm),
      pixels(static_cast<size_t>(w) * h, 0), priority(static_cast<size_t>(w) * h, 0),
      sprite_pixels(static_cast<size_t>(w) * h, 0xffff)
{
}

void Screen::render_frame(RGB888* output)
{
    std::fill(priority.begin(), priority.end(), uint8_t{0});
    std::fill(sprite_pixels.begin(), sprite_pixels.end(), uint16_t{0xffff});

    sprites.render(sprite_pixels.data(), width, height, 0, 0, stage1_view ? 0xc : 0xf);

    if (stage1_view) draw_stage1();
    else {
    road.render_background(pixels.data(), width, height);
    // After Burner II forces road_priority = 0: the road's textured
    // foreground layer is drawn low-priority, before any tilemap.
    road.render_foreground(pixels.data(), width, height);
    }

    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/1, /*category=*/0, 0x01); // BG cat0
    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/1, /*category=*/1, 0x02); // BG cat1
    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/0, /*category=*/0, 0x02); // FG cat0
    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/0, /*category=*/1, 0x04); // FG cat1
    tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/0, 0x04);
    tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/1, 0x08);

    if (stage1_view)
    {
        unsigned speed = 0;
        for (int i = 0; i < 3; ++i) speed += tiles.read_text(26 * 64 + 26 + i) == 0x8bf8;
        // Colored gauge behind the ROM scale digits, with the text layer kept on top.
        for (int y = 203; y < std::min(height, 207); ++y)
            for (int x = 57; x < std::min(width, 57 + static_cast<int>(speed) * 22); ++x)
                if (priority[y * width + x] == 0)
                {
                    pixels[y * width + x] = static_cast<uint16_t>(0x1c40 + (x - 57) / 22);
                    priority[y * width + x] = 8;
                }
    }

    const uint32_t num_entries = palette.num_entries();

    for (int i = 0; i < width * height; i++)
    {
        const uint16_t spix = sprite_pixels[i];
        if (spix != 0xffff)
        {
            const int spr_priority = (spix >> 12) & 3;
            if ((1 << spr_priority) > priority[i])
            {
                if ((spix & 0x400f) == 0x400a)
                    pixels[i] = static_cast<uint16_t>(pixels[i] + num_entries); // shadow/hilight
                else
                    pixels[i] = spix & 0xfff; // sprite's own color
            }
        }

        output[i] = palette.pen(pixels[i]);
    }
}

void Screen::init_stage1(const uint8_t* sprite_rom, uint32_t length)
{
    HWSprites texture_hw;
    texture_hw.init(sprite_rom, length);
    OSprites descriptors;
    OEntry tile;
    tile.control = 0x2000; // Native bank-3 sea patch, read from row end.
    descriptors.append_sprite(tile, 3, 0x201d, 16, 0x200, 0x200, 60, 0x23, 1);
    descriptors.flush_to_hardware(texture_hw);
    sea_texture.assign(128 * 60, 0xffff);
    texture_hw.render(sea_texture.data(), 128, 60);
    terrain_pixels.resize(512 * 512);
    terrain_sprites.resize(512 * 512);
    stage1_view = true;
}

void Screen::set_camera(int roll, int pitch, uint32_t distance)
{
    camera_roll = roll;
    camera_pitch = pitch;
    travel = distance;
}

void Screen::draw_stage1()
{
    // Extra area around the viewport prevents empty corners at maximum bank.
    constexpr int side = 512, ox = 96, oy = 144;
    constexpr int horizon = 124;
    for (int y = 0; y < side; ++y)
    {
        const int depth = y - oy - horizon;
        for (int x = 0; x < side; ++x)
        {
            uint16_t color = 0x1720; // Original Stage 1 sky blue.
            if (depth >= -30 && depth < 0)
                color = static_cast<uint16_t>(0x1780 + (depth + 30) * 31 / 30);
            else if (depth >= 0)
            {
                const double scale = 2.4 / (1.0 + depth / 80.0);
                const int tx = static_cast<int>((x - ox) * scale + travel * 0.025) & 127;
                const int ty = (static_cast<int>(depth * scale * 0.7 + travel * 0.04) % 60 + 60) % 60;
                const uint16_t texel = sea_texture[ty * 128 + tx];
                color = texel == 0xffff ? 0x1700 : texel & 0xfff;
            }
            terrain_pixels[y * side + x] = color;
        }
    }
    std::fill(terrain_sprites.begin(), terrain_sprites.end(), uint16_t{0xffff});
    sprites.render(terrain_sprites.data(), side, side, ox, oy, 0x2);
    for (size_t i = 0; i < terrain_pixels.size(); ++i)
        if (terrain_sprites[i] != 0xffff) terrain_pixels[i] = terrain_sprites[i] & 0xfff;
    const double angle = camera_roll * 3.14159265358979323846 / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const double dx = x - 160.0, dy = y - 112.0 - camera_pitch * 0.7;
            const int sx = std::clamp(static_cast<int>(256 + c * dx - s * dy), 0, side - 1);
            const int sy = std::clamp(static_cast<int>(256 + s * dx + c * dy), 0, side - 1);
            const uint16_t value = terrain_pixels[sy * side + sx];
            pixels[y * width + x] = value;
        }
}

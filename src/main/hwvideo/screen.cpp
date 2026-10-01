#include <algorithm>
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

    sprites.render(sprite_pixels.data(), width, height, 0, 0, 0xf);

    road.render_background(pixels.data(), width, height);
    // After Burner II forces road_priority = 0: the road's textured
    // foreground layer is drawn low-priority, before any tilemap.
    road.render_foreground(pixels.data(), width, height);

    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/1, /*category=*/0, 0x01); // BG cat0
    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/1, /*category=*/1, 0x02); // BG cat1
    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/0, /*category=*/0, 0x02); // FG cat0
    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/0, /*category=*/1, 0x04); // FG cat1
    tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/0, 0x04);
    tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/1, 0x08);

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

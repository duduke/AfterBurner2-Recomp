#include <algorithm>
#include <stdexcept>
#include "hwvideo/screen.hpp"

Screen::Screen(int w, int h, HWPalette& pal, HWRoad& rd, HWSprites& spr, HWTiles& tm)
    : width(w), height(h), palette(pal), road(rd), sprites(spr), tiles(tm),
      pixels(static_cast<size_t>(w) * h, 0), priority(static_cast<size_t>(w) * h, 0),
      sprite_pixels(static_cast<size_t>(w) * h, 0xffff)
{
}

void Screen::render_frame(RGB888* output)
{
    render_partial(output, 0, height-1, true);
}

void Screen::render_partial(RGB888* output, int top, int bottom, bool display, uint8_t* sprite_bank)
{
    if(top<0 || bottom>=height || top>bottom) throw std::runtime_error("invalid video clip");
    if(!display) {
        std::fill(output+top*width, output+(bottom+1)*width, RGB888{0,0,0});
        return;
    }
    std::fill(priority.begin()+top*width, priority.begin()+(bottom+1)*width, uint8_t{0});
    std::fill(sprite_pixels.begin()+top*width, sprite_pixels.begin()+(bottom+1)*width, uint16_t{0xffff});

    if(sprite_bank) sprites.render_bank(sprite_bank, sprite_pixels.data(), width, height, top, bottom);
    else sprites.render(sprite_pixels.data(), width, height, 0, 0, 0xf, top, bottom);

    road.render_background(pixels.data(), width, height, top, bottom);
    // After Burner II forces road_priority = 0: the road's textured
    // foreground layer is drawn low-priority, before any tilemap.
    road.render_foreground(pixels.data(), width, height, top, bottom);

    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/1, /*category=*/-1, 0x01, top, bottom);
    tiles.draw_layer(pixels.data(), priority.data(), width, height, /*which=*/0, /*category=*/-1, 0x02, top, bottom);
    tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/-1, 0x04, top, bottom);

    if(layers_rendered)layers_rendered(top,bottom,pixels,priority,palette);

    const uint32_t num_entries = palette.num_entries();

    for (int i = top * width; i < (bottom+1) * width; i++)
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

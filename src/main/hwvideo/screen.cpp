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
    render_partial_impl(output,top,bottom,display,sprite_bank,nullptr);
}

Screen::ViewportLayers::ViewportLayers(unsigned logical_width):logical_width(logical_width)
{
    // Same bounded, symmetric logical-width policy as the existing preview.
    if(logical_width<320 || logical_width>640 || logical_width%2)
        throw std::invalid_argument("Unsupported integrated viewport width");
    pixels.assign(size_t(logical_width)*224,0);
    priority.assign(pixels.size(),0);
}

void Screen::render_integrated_partial(RGB888* output, ViewportLayers& viewport,
                                      int top, int bottom, bool display, uint8_t* sprite_bank)
{
    if(width!=320 || height!=224)
        throw std::invalid_argument("Integrated viewport requires a native 320x224 screen");
    render_partial_impl(output,top,bottom,display,sprite_bank,&viewport);
}

void Screen::render_partial_impl(RGB888* output, int top, int bottom, bool display,
                                uint8_t* sprite_bank, ViewportLayers* viewport)
{
    if(top<0 || bottom>=height || top>bottom) throw std::runtime_error("invalid video clip");
    if(!display) {
        std::fill(output+top*width, output+(bottom+1)*width, RGB888{0,0,0});
        if(viewport) {
            const auto w=viewport->width();
            std::fill(viewport->pixels.begin()+top*w,viewport->pixels.begin()+(bottom+1)*w,0);
            std::fill(viewport->priority.begin()+top*w,viewport->priority.begin()+(bottom+1)*w,0);
        }
        return;
    }
    std::fill(priority.begin()+top*width, priority.begin()+(bottom+1)*width, uint8_t{0});
    std::fill(sprite_pixels.begin()+top*width, sprite_pixels.begin()+(bottom+1)*width, uint16_t{0xffff});

    if(sprite_bank) sprites.render_bank(sprite_bank, sprite_pixels.data(), width, height, top, bottom);
    else sprites.render(sprite_pixels.data(), width, height, 0, 0, 0xf, top, bottom);

    auto* layer_pixels=viewport?viewport->pixels.data():pixels.data();
    auto* layer_priority=viewport?viewport->priority.data():priority.data();
    const int layer_width=viewport?int(viewport->width()):width;
    const int native_left=viewport?-int(viewport->origin()):0;
    if(viewport)std::fill(layer_priority+top*layer_width,layer_priority+(bottom+1)*layer_width,uint8_t{0});
    road.render_background(layer_pixels, layer_width, height, top, bottom);
    // After Burner II forces road_priority = 0: the road's textured
    // foreground layer is drawn low-priority, before any tilemap.
    road.render_foreground(layer_pixels, layer_width, height, top, bottom, native_left);

    tiles.draw_layer(layer_pixels, layer_priority, layer_width, height, /*which=*/1, /*category=*/-1, 0x01, top, bottom, native_left);
    tiles.draw_layer(layer_pixels, layer_priority, layer_width, height, /*which=*/0, /*category=*/-1, 0x02, top, bottom, native_left);
    if(viewport)for(int y=top;y<=bottom;++y) {
        const auto source=y*layer_width+viewport->origin();
        std::copy_n(layer_pixels+source,width,pixels.data()+y*width);
        std::copy_n(layer_priority+source,width,priority.data()+y*width);
    }
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

/***************************************************************************
    Video Emulation: X-Board Final Screen Compositing.

    Reimplements segaxbd_state::screen_update() (src/mame/sega/segaxbd.cpp,
    Aaron Giles, MAME, BSD-3-Clause) — algorithm only, no code copied. Ties
    together the road, tilemap and sprite hardware layers in the exact
    draw order and priority-comparison scheme real hardware uses, with
    After Burner II's own `m_road_priority = 0` baked in (install_aburner2()
    forces this — see docs/HARDWARE.md section 3.2/7).

    Draw order for aburner2:
      1. road background
      2. road foreground        (drawn early: m_road_priority == 0)
      3. BG tilemap, category 0 -> priority mark 0x01
      4. BG tilemap, category 1 -> priority mark 0x02
      5. FG tilemap, category 0 -> priority mark 0x02
      6. FG tilemap, category 1 -> priority mark 0x04
      7. text layer, category 0 -> priority mark 0x04
      8. text layer, category 1 -> priority mark 0x08
      9. sprites, composited against the accumulated priority buffer

    Sprite compositing rule per pixel (sprite word `pix`, priority levels
    0-3 in bits 12-13, shadow flag in bit 14):
      if (1 << priority) > accumulated_priority_mark_at_that_pixel:
          if pix's shadow bit is set AND its raw 4-bit pixel index == 0xa:
              leave whatever's already there, but resolve it through the
              palette's "effects" (shadow/hilight) pen bank instead
          else:
              draw the sprite's own color (bits 4-11) at that pixel
      (a sprite pixel with lower priority than what's already accumulated
      there is simply not drawn — earlier/road layers show through)
***************************************************************************/

#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <span>
#include "hwvideo/hwpalette.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwtiles.hpp"

class Screen
{
public:
    // Passive indexed road/tile planes before sprite composition. Only rows
    // top..bottom belong to this frontier; other rows retain previous clips.
    // Consumers must copy data synchronously before the next native render.
    using LayerObserver=std::function<void(unsigned,unsigned,std::span<const uint16_t>,std::span<const uint8_t>,const HWPalette&)>;
    LayerObserver layers_rendered;
    Screen(int width, int height, HWPalette& palette, HWRoad& road, HWSprites& sprites, HWTiles& tiles);

    // Renders one full frame into `output` (width*height RGB888 pixels,
    // row-major, matching the dimensions passed to the constructor).
    void render_frame(RGB888* output);
    // Compose only this inclusive visible clip. External bank retains the
    // sprite scaler feedback; disabled display neither draws nor changes it.
    void render_partial(RGB888* output, int top, int bottom, bool display,
                        uint8_t* sprite_bank = nullptr);
    // Passive raw sprite raster from the last clip; reading it never reruns
    // the hardware scaler or its RAM feedback.
    std::span<const uint16_t> sprite_words() const { return sprite_pixels; }
    const std::vector<uint16_t>& indices() const { return pixels; }
    const std::vector<uint8_t>& priorities() const { return priority; }

private:
    int width, height;
    HWPalette& palette;
    HWRoad& road;
    HWSprites& sprites;
    HWTiles& tiles;

    std::vector<uint16_t> pixels;    // accumulated palette-index pixels (road + tiles)
    std::vector<uint8_t> priority;   // accumulated priority marks from tile layers
    std::vector<uint16_t> sprite_pixels; // raw composited sprite words, 0xffff = none
};

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace Presentation {

// Text-plane inputs at one completed native partial-render frontier. Entries
// are the visible 40 columns of the 64-column hardware map, in row-major order.
// Their code, palette and priority bits retain the HWTiles interpretation.
// top..bottom is inclusive: a palette/text change may split an 8-pixel glyph.
struct HudClip {
    unsigned top{}, bottom{};
    std::array<uint16_t,40*28> entries{};
    // ARGB8888 pens for all eight 8-color text palettes. 0..63 are normal;
    // 64..127 are the corresponding hardware shadow-or-hilight effects pens.
    std::array<uint32_t,128> colors{};
};

// Borrowed presentation data, valid until WideScene::render/reset. Occlusion
// uses native 320x224 coordinates regardless of the enhanced viewport width:
// 0 = normal pen, 1 = effects pen, 2 = hidden. Display-disabled rows are hidden
// and have no HudClip. An empty scene means the original text remains baked
// into the presentation raster, or no replacement frame has been rendered.
struct HudScene {
    std::span<const HudClip> clips;
    std::span<const uint8_t> occlusion;
};

// Category 1 text has hardware mark 8, so no sprite priority can cover it.
// Category 0 has mark 4, and only the top native priority-3 pixel can win.
// Tile priority marks cannot change either result (their union is below 4).
inline uint8_t hud_occlusion(uint16_t entry,uint16_t native_sprite) {
    if((entry&0x8000) || native_sprite==0xffff || ((native_sprite>>12)&3)!=3)return 0;
    return (native_sprite&0x400f)==0x400a ? 1 : 2;
}

}

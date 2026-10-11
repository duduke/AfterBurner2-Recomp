/***************************************************************************
    Video Emulation: X-Board Palette Hardware.

    Each palette RAM entry is a 16-bit word: sBGR BBBB GGGG RRRR (5 bits per
    channel, weighted through a resistor DAC — not a linear ramp). Bit 15
    additionally selects, for that specific color, whether its "effects"
    variant (used when a sprite/road pixel marked shadowed overlaps it) is
    a darkened shadow or a brightened hilight. Every color write therefore
    produces two resolved RGB entries: a normal pen and an effects pen, the
    second stored `num_entries` slots after the first. See
    docs/HARDWARE.md section 3.4.

    Resistor values and the weighted-DAC formula are reimplemented from
    segaxbd_state::palette_init() (src/mame/sega/segaxbd.cpp, Aaron Giles,
    MAME, BSD-3-Clause) and MAME's generic compute_resistor_weights()
    (src/emu/video/resnet.cpp) — algorithm only, no code copied. For the
    no-pulldown/no-pullup case this hardware uses, the general resistor-
    network formula algebraically reduces to a simple conductance-ratio
    weighted sum, which is what's implemented here.
***************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

struct RGB888
{
    uint8_t r, g, b;
};

class HWPalette
{
public:
    explicit HWPalette(uint32_t num_entries);

    // Handle a CPU write to palette RAM at word `offset`.
    void write(uint32_t offset, uint16_t data);

    // Resolved output pens: index [0, num_entries) = normal brightness,
    // [num_entries, 2*num_entries) = shadow-or-hilight effects pen.
    inline RGB888 pen(uint32_t index) const { return pens[index]; }

    uint32_t num_entries() const { return entries; }

private:
    uint32_t entries;
    std::vector<RGB888> pens;
    // Sentinel keeps the first zero write distinct from an uninitialized pen.
    std::vector<uint32_t> last_words;

    // Per-channel weight tables (0-31 raw 5-bit value -> weighted 0-255 brightness).
    uint8_t normal_weighted[32];
    uint8_t shadow_weighted[32];
    uint8_t hilight_weighted[32];

    void build_weight_tables();
};

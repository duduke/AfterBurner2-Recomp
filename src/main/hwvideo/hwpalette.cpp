#include "hwvideo/hwpalette.hpp"

namespace
{
    // Weighted-DAC combine: MAME's combine_weights() rounds the summed
    // contribution with a "+0.5, truncate" (round-half-up).
    inline uint8_t combine(const double* w, int i0, int i1, int i2, int i3, int i4, int i5)
    {
        const double sum = w[0] * i0 + w[1] * i1 + w[2] * i2 + w[3] * i3 + w[4] * i4 + w[5] * i5;
        return static_cast<uint8_t>(sum + 0.5);
    }
}

HWPalette::HWPalette(uint32_t num_entries) : entries(num_entries), pens(num_entries * 2, RGB888{0, 0, 0})
{
    build_weight_tables();
}

// Bit 0 = 3.9K, bit 1 = 2.0K, bit 2 = 1.0K, bit 3 = 2x1.0K (500R), bit 4 =
// 4x1.0K (250R): the 5 real color bits. A 6th, 470R resistor is switched in
// via a tristate buffer driven by bit 15 of the palette word (the "effects"
// select) — pulled low for the shadow variant, high for the hilight
// variant, absent entirely for the normal variant. See docs/HARDWARE.md
// section 3.4.
void HWPalette::build_weight_tables()
{
    static constexpr double R[5] = {3900.0, 2000.0, 1000.0, 500.0, 250.0};
    static constexpr double R_EFFECTS = 470.0;

    double g[5];
    double g_total_normal = 0.0;
    for (int i = 0; i < 5; i++)
    {
        g[i] = 1.0 / R[i];
        g_total_normal += g[i];
    }
    const double g_effects = 1.0 / R_EFFECTS;
    const double g_total_sh = g_total_normal + g_effects;

    double w_normal[6] = {};
    double w_sh[6] = {};
    for (int i = 0; i < 5; i++)
    {
        w_normal[i] = 255.0 * g[i] / g_total_normal;
        w_sh[i] = 255.0 * g[i] / g_total_sh;
    }
    w_sh[5] = 255.0 * g_effects / g_total_sh;

    for (int value = 0; value < 32; value++)
    {
        const int i0 = (value >> 0) & 1;
        const int i1 = (value >> 1) & 1;
        const int i2 = (value >> 2) & 1;
        const int i3 = (value >> 3) & 1;
        const int i4 = (value >> 4) & 1;

        normal_weighted[value] = combine(w_normal, i0, i1, i2, i3, i4, 0);
        shadow_weighted[value] = combine(w_sh, i0, i1, i2, i3, i4, 0);
        hilight_weighted[value] = combine(w_sh, i0, i1, i2, i3, i4, 1);
    }
}

void HWPalette::write(uint32_t offset, uint16_t data)
{
    // sBGR BBBB GGGG RRRR: each channel is its "5th" (doubling) bit from the
    // top nibble plus its 4 weighted bits from its own nibble.
    const int r = ((data >> 12) & 0x01) | ((data << 1) & 0x1e);
    const int g = ((data >> 13) & 0x01) | ((data >> 3) & 0x1e);
    const int b = ((data >> 14) & 0x01) | ((data >> 7) & 0x1e);

    pens[offset] = RGB888{normal_weighted[r], normal_weighted[g], normal_weighted[b]};

    const bool hilight = (data & 0x8000) != 0;
    pens[entries + offset] = hilight
        ? RGB888{hilight_weighted[r], hilight_weighted[g], hilight_weighted[b]}
        : RGB888{shadow_weighted[r], shadow_weighted[g], shadow_weighted[b]};
}

#include "engine/osubroad.hpp"

// Literal port of sub CPU ROM $00046A-$00052C and callees. Comments give the
// ROM address of each block, to check against the disassembly
// (tools/m68k_disasm.py on a subcpu image from tools/build_rom_image.py).

namespace
{
    constexpr uint32_t A6 = SubBus::A6;
    constexpr uint32_t DIV = 0x0e4000;
    constexpr uint32_t MUL = DIV - 0x4000;
    constexpr uint32_t MUL_RESULT = MUL + 4;

    // Shared-RAM variables (A6-relative), named where their role is clear.
    constexpr uint32_t HORIZON = 0x82;          // horizon line offset from screen center, +-111
    constexpr uint32_t ROAD_BASE = 0x86;        // subtracted from the raster's base offset
    constexpr uint32_t SIN = 0x90, COS = 0x92;  // of the roll angle, from the table at $15D4
    constexpr uint32_t ROLL = 0x9f;             // roll angle byte (256 per turn)
    constexpr uint32_t SKIP_GROUND = 0x9a;
    constexpr uint32_t STAGE = 0x9c;
    constexpr uint32_t BANK_TARGET = 0xce;
    constexpr uint32_t BANK_RING = 0x620;       // 8-word history of bank values
    constexpr uint32_t BANK_RING_POS = 0x630;
    constexpr uint32_t ROAD_MODE = 0x320;       // byte written to the road control register
    constexpr uint32_t SEQUENCE = 0x321;        // special-sequence state, 0 = normal

    constexpr uint32_t SIN_TABLE = 0x0015d4;
    constexpr uint32_t RAMP_SLOPE = 0x0011d4;   // 128 x 16.16 per-line step
    constexpr uint32_t RAMP_LENGTH = 0x0013d4;  // 128 x lines of ramp before it flattens
    constexpr uint32_t ROAD0_SCROLL_CENTER = 0x0ec4e0;   // road 0 scroll table, line 112

    inline uint32_t sext(uint16_t w) { return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(w))); }
    inline uint16_t sext8(uint8_t b) { return static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(b))); }
    inline int16_t s(uint16_t w) { return static_cast<int16_t>(w); }
    inline uint32_t asr32(uint32_t v, int n) { return static_cast<uint32_t>(static_cast<int32_t>(v) >> n); }
    inline uint16_t asr16(uint16_t v, int n) { return static_cast<uint16_t>(s(v) >> n); }
}

bool OSubRoad::frame(SubBus& bus)
{
    // $00046A: ground generators by stage, unless suppressed.
    if (bus.read16(A6 + SKIP_GROUND) == 0)
    {
        const uint16_t stage = bus.read16(A6 + STAGE);
        if (stage == 0x10 || stage == 1 || stage == 7)
        {
            slew_bank(bus);
            slew_pitch(bus);
        }
        else if (stage == 3 || stage == 0x18)
        {
            bank_history(bus);
            slew_pitch(bus);
        }
    }

    rotation(bus);                                           // $0004B2

    if (static_cast<int8_t>(bus.read8(A6 + SEQUENCE)) <= 0)  // $0004B6: BGT $0004EC
    {
        raster(bus);
        if (static_cast<int8_t>(bus.read8(A6 + SEQUENCE)) >= 0)  // BPL $000530
            return true;

        // $0004C6: start a special sequence (sets SEQUENCE to 1).
        bus.write16(A6 + ROAD_MODE, 0x0301);
        bus.write32(A6 + 0x322, 0xffc0);
        bus.write16(A6 + 0x326, 0);
        bus.write16(A6 + 0x338, 0);
        bus.write32(A6 + 0x328, 0xff);
        bus.write32(A6 + 0x32c, 0x010001ff);
    }

    // $0004EC
    if (bus.read8(A6 + SEQUENCE) == 1)
        { special(bus); return true; } // $0016D4

    bus.write8(A6 + SEQUENCE, static_cast<uint8_t>(bus.read8(A6 + SEQUENCE) + 1));   // $0004FC
    if (bus.read8(A6 + SEQUENCE) >= 5)                       // BCC $00052C
    {
        bus.write8(A6 + SEQUENCE, 0);
        return true;
    }
    for (uint32_t a = 0x0ec800; a < 0x0ec800 + 28 * 16; a += 4)   // $000508
        bus.write32(a, 0);
    bus.write8(A6 + ROAD_MODE, 4);
    raster(bus);
    return true;
}

// $000C0A: ease toward the bank target (zeroed while subram0[$201] bit 0 is
// set). $AE is a recentering offset that decays by 6 per frame; $B2
// accumulates the change in target and kicks $AE to +-$FF when it passes
// +-$B0; $B4 follows 3/8 of the target at up to 2 per frame, $B6 follows
// $B4 at up to 1 per frame.
void OSubRoad::slew_bank(SubBus& bus)
{
    uint16_t d7 = bus.read16(A6 + BANK_TARGET);
    if (bus.read8(A6 + 0x201) & 1)
        d7 = 0;

    const uint16_t offset = bus.read16(A6 + 0xae);
    if (offset != 0)
    {
        if (s(offset) < 0)                                   // $000C36
        {
            const uint16_t v = static_cast<uint16_t>(offset + 6);
            bus.write16(A6 + 0xae, v);
            if (!(s(v) < 0)) bus.write16(A6 + 0xae, 0);
        }
        else
        {
            const uint16_t v = static_cast<uint16_t>(offset - 6);
            bus.write16(A6 + 0xae, v);
            if (!(s(v) > 0)) bus.write16(A6 + 0xae, 0);
        }
    }
    else
    {
        const uint16_t d0 = static_cast<uint16_t>(d7 - bus.read16(A6 + 0xb0));   // $000C46
        const uint16_t v = static_cast<uint16_t>(bus.read16(A6 + 0xb2) + d0);
        bus.write16(A6 + 0xb2, v);
        if (v != 0)
        {
            if (s(v) > 0)                                    // $000C7C
            {
                bus.write16(A6 + 0xb2, static_cast<uint16_t>(bus.read16(A6 + 0xb2) - 0x10));
                if (!(0xb0 >= s(bus.read16(A6 + 0xb2))))
                {
                    bus.write16(A6 + 0xae, 0xff01);
                    bus.write16(A6 + 0xb2, 0);
                }
            }
            else
            {
                bus.write16(A6 + 0xb2, static_cast<uint16_t>(bus.read16(A6 + 0xb2) + 0x10));
                if (!(-0xb0 <= s(bus.read16(A6 + 0xb2))))
                {
                    bus.write16(A6 + 0xae, 0x00ff);
                    bus.write16(A6 + 0xb2, 0);
                }
            }
        }
    }

    bus.write16(A6 + 0xb0, d7);                              // $000C92
    uint16_t d0 = asr16(d7, 2);
    d0 = static_cast<uint16_t>(d0 + asr16(d0, 1));
    d0 = static_cast<uint16_t>(d0 - bus.read16(A6 + 0xb4));
    if (s(d0) > 2) d0 = 2;
    else if (!(s(d0) > -2)) d0 = static_cast<uint16_t>(-2);
    bus.write16(A6 + 0xb4, static_cast<uint16_t>(bus.read16(A6 + 0xb4) + d0));

    d0 = static_cast<uint16_t>(bus.read16(A6 + 0xb4) - bus.read16(A6 + 0xb6));
    if (s(d0) > 1) d0 = 1;
    else if (!(s(d0) > -1)) d0 = static_cast<uint16_t>(-1);
    bus.write16(A6 + 0xb6, static_cast<uint16_t>(bus.read16(A6 + 0xb6) + d0));

    bank_history(bus);
}

// $000CEA
void OSubRoad::bank_history(SubBus& bus)
{
    uint16_t d0 = static_cast<uint16_t>(bus.read16(A6 + 0xb4) + bus.read16(A6 + 0xae));
    bus.write16(A6 + 0x9e, d0);
    d0 = static_cast<uint16_t>(d0 + d0);
    bus.write16(A6 + BANK_RING_POS, static_cast<uint16_t>(bus.read16(A6 + BANK_RING_POS) + 2));
    const uint16_t pos = bus.read16(A6 + BANK_RING_POS) & 0xe;
    bus.write16(A6 + BANK_RING + sext(pos), d0);
}

// $000D0E: clamp the pitch target to [$EA, $EC], then move $96 toward it by
// at most $EE per frame ($98 = the requested change).
void OSubRoad::slew_pitch(SubBus& bus)
{
    uint16_t d0 = static_cast<uint16_t>(-bus.read16(A6 + 0xd0));
    uint16_t d1 = bus.read16(A6 + 0xea);
    if (!(s(d0) >= s(d1)))
        d0 = d1;
    else
    {
        d1 = bus.read16(A6 + 0xec);
        if (!(s(d0) <= s(d1)))
            d0 = d1;
    }
    d0 = static_cast<uint16_t>(d0 - bus.read16(A6 + 0x96));   // $000D30
    bus.write16(A6 + 0x98, d0);
    d1 = bus.read16(A6 + 0xee);
    if (!(s(d0) <= s(d1)))
        d0 = d1;
    else
    {
        d1 = static_cast<uint16_t>(-d1);
        if (!(s(d0) > s(d1)))
            d0 = d1;
    }
    bus.write16(A6 + 0x96, static_cast<uint16_t>(bus.read16(A6 + 0x96) + d0));
    bus.write16(A6 + 0x94, bus.read16(A6 + 0x96));
}

// $000D64
void OSubRoad::rotation(SubBus& bus)
{
    // Quadrant flags in $A0 bits 4/5 and two step sizes from the angle
    // within the quadrant.
    uint8_t angle = bus.read8(A6 + ROLL);
    const uint32_t flags_at = A6 + 0xa0;
    bus.write8(flags_at, static_cast<uint8_t>(bus.read8(flags_at) & 0xcf));
    if (!(angle & 0x80))
        bus.write8(flags_at, static_cast<uint8_t>(bus.read8(flags_at) | 0x20));
    const uint8_t rotated = static_cast<uint8_t>(angle + 0x40);
    if (rotated & 0x80)
        bus.write8(flags_at, static_cast<uint8_t>(bus.read8(flags_at) | 0x10));

    uint8_t within = bus.read8(A6 + ROLL);                   // $000D8C
    if (!(rotated & 0x40))
        within = static_cast<uint8_t>(~within);
    uint32_t d0 = within & 0x3f;
    uint16_t d1 = static_cast<uint16_t>((asr16(static_cast<uint16_t>(d0), 3) + 2) & 0xfffc);
    bus.write16(A6 + 0xa4, d1);
    d1 = static_cast<uint16_t>((asr16(static_cast<uint16_t>(d0), 2) + 2) & 0xfffc);
    bus.write16(A6 + 0xa6, d1);

    bus.write32(DIV, (d0 << 4) + 0x55);                      // $000DBC
    bus.write16(DIV + 20, 0xaa);
    bus.write16(A6 + 0xa8, static_cast<uint16_t>(bus.read16(DIV + 8) << 2));

    // $000DE0: sine/cosine (signed bytes; cosine is 64 entries earlier).
    const uint16_t index = bus.read8(A6 + ROLL);
    const uint16_t sine = sext8(bus.read8(SIN_TABLE + index));
    const uint16_t cosine = sext8(bus.read8(SIN_TABLE + index - 64));
    bus.write16(A6 + SIN, sine);
    bus.write16(A6 + COS, cosine);

    // $000E00: 32 steps of (sin, cos) * 16, each offset by (sin, cos) * -$80.
    bus.write16(MUL, static_cast<uint16_t>(-bus.read16(A6 + 0x80)));
    bus.write16(MUL + 2, sine);
    const uint32_t offset_x = bus.read32(MUL_RESULT);
    bus.write16(MUL + 2, cosine);
    const uint32_t offset_y = bus.read32(MUL_RESULT);
    const uint32_t step_x = sext(static_cast<uint16_t>(sine << 4));
    const uint32_t step_y = sext(static_cast<uint16_t>(cosine << 4));
    uint32_t x = 0, y = 0, a0 = A6 + 0x100;
    for (int i = 0; i < 32; ++i)
    {
        x += step_x;
        y += step_y;
        bus.write32(a0, x);
        bus.write32(a0, bus.read32(a0) + offset_x);
        a0 += 4;
        bus.write32(a0, y);
        bus.write32(a0, bus.read32(a0) + offset_y);
        a0 += 4;
    }

    // $000E48: horizon line from pitch and altitude.
    const uint16_t c = bus.read16(A6 + COS), sn = bus.read16(A6 + SIN);
    bus.write16(MUL, bus.read16(A6 + 0x96));
    bus.write16(MUL + 2, 0xffe8);
    uint32_t pitch_term = asr32(bus.read32(MUL_RESULT), 7);
    uint16_t alt = static_cast<uint16_t>(bus.read16(A6 + 0xc2) - 0x1fe0);
    alt = asr16(asr16(alt, 5), 5);
    alt = static_cast<uint16_t>(alt + asr16(alt, 1));
    alt = static_cast<uint16_t>(-alt + 0x1c);
    const uint16_t lift = static_cast<uint16_t>(pitch_term + alt);

    bus.write16(MUL, lift);
    bus.write16(MUL + 2, c);
    const uint16_t lift_c = static_cast<uint16_t>(asr32(bus.read32(MUL_RESULT), 7));
    bus.write16(MUL + 2, sn);
    const uint16_t lift_s = static_cast<uint16_t>(asr32(bus.read32(MUL_RESULT), 7));

    bus.write16(MUL, bus.read16(A6 + 0x94));
    bus.write16(MUL + 2, c);
    const uint16_t p_c = static_cast<uint16_t>(asr32(bus.read32(MUL_RESULT), 7));
    bus.write16(MUL + 2, sn);
    const uint16_t p_s = static_cast<uint16_t>(asr32(bus.read32(MUL_RESULT), 7));

    const uint16_t p_c_125 = static_cast<uint16_t>(asr16(p_c, 2) + p_c);
    const uint16_t p_s_125 = static_cast<uint16_t>(asr16(p_s, 2) + p_s);
    bus.write16(A6 + 0x8c, p_c_125);
    bus.write16(A6 + 0x8e, p_s_125);
    bus.write16(A6 + 0x88, p_c);
    bus.write16(A6 + 0x8a, p_s);
    bus.write16(A6 + 0x84, static_cast<uint16_t>(lift_c + p_c_125));
    bus.write16(A6 + 0x86, static_cast<uint16_t>(lift_s + p_s_125));

    uint16_t horizon = bus.read16(A6 + 0x84);                // $000EE4
    if (s(horizon) > 0x6f) horizon = 0x6f;
    if (s(horizon) < -0x6f) horizon = static_cast<uint16_t>(-0x6f);
    bus.write16(A6 + HORIZON, horizon);
}

// $000F02: per-line horizontal offsets for road 0, centered on the horizon.
// The bank value from three frames ago picks one of four variants (its bits
// 7-8: quadrant) and a ramp from 128-entry tables (its low 7 bits, mirrored
// in two quadrants). Lines out from the center step by the ramp's 16.16
// slope for its length, then hold.
void OSubRoad::raster(SubBus& bus)
{
    const uint32_t a4 = A6 + BANK_RING + ((bus.read16(A6 + BANK_RING_POS) - 6) & 0xe);
    const uint16_t bank = bus.read16(a4);

    struct Variant { bool mirrored; uint16_t base; bool up_subtracts; };
    // Jump table at $000F1E: $000F2E, $00107E, $00112A, $000FD2.
    static constexpr Variant VARIANTS[4] = {
        {false, 0x4b0, true}, {true, 0x4b0, false}, {false, 0x6b0, true}, {true, 0x6b0, false}};
    const Variant& v = VARIANTS[(bank & 0x180) >> 7];

    const uint16_t index = v.mirrored ? static_cast<uint16_t>(0x7f - (bus.read16(a4) & 0x7f))
                                      : static_cast<uint16_t>(bank & 0x7f);
    uint16_t d7 = bus.read16(RAMP_LENGTH + index * 2);
    const uint32_t slope = bus.read32(RAMP_SLOPE + index * 4);

    const uint16_t first_horizon = bus.read16(A6 + HORIZON);
    uint32_t a0 = ROAD0_SCROLL_CENTER - sext(static_cast<uint16_t>(first_horizon * 2));
    uint32_t a1 = a0;
    const uint16_t start = static_cast<uint16_t>(v.base - bus.read16(A6 + ROAD_BASE));
    a0 -= 2;
    bus.write16(a0, start);
    uint32_t up = static_cast<uint32_t>(start) << 16;
    uint32_t down = up;

    // Variant A ($000F2E) reuses the horizon it already read; the others re-read it.
    const uint16_t horizon = v.mirrored || v.base != 0x4b0 ? bus.read16(A6 + HORIZON) : first_horizon;
    uint16_t d5 = static_cast<uint16_t>(112 - horizon);
    uint16_t d6 = static_cast<uint16_t>(112 + horizon);

    const auto step_up = [&] { up = v.up_subtracts ? up - slope : up + slope; };
    const auto step_down = [&] { down = v.up_subtracts ? down + slope : down - slope; };

    // Toward the top of the screen.
    if (s(d5) > s(d7))
    {
        d5 = static_cast<uint16_t>(d5 - d7);
        uint16_t n = d7;
        do { step_up(); a0 -= 2; bus.write16(a0, static_cast<uint16_t>(up >> 16)); } while (n-- != 0);
        do { a0 -= 2; bus.write16(a0, static_cast<uint16_t>(up >> 16)); } while (d5-- != 0);
    }
    else
        do { step_up(); a0 -= 2; bus.write16(a0, static_cast<uint16_t>(up >> 16)); } while (d5-- != 0);

    // Toward the bottom.
    if (s(d6) > s(d7))
    {
        d6 = static_cast<uint16_t>(d6 - d7);
        do { step_down(); bus.write16(a1, static_cast<uint16_t>(down >> 16)); a1 += 2; } while (d7-- != 0);
        do { bus.write16(a1, static_cast<uint16_t>(down >> 16)); a1 += 2; } while (d6-- != 0);
    }
    else
        do { step_down(); bus.write16(a1, static_cast<uint16_t>(down >> 16)); a1 += 2; } while (d6-- != 0);
}

// $0016D4-$00174E: moving special road, including the signed comparator wrap.
void OSubRoad::special(SubBus& bus)
{
    if (!bus.read8(A6 + 0x321)) return;
    special_raster(bus); // $001758
    bus.write16(MUL, bus.read16(A6 + 0xaa));
    const uint32_t table = 0x1750 + ((bus.read8(A6 + 0x320) & 8) ? 4 : 0);
    if (bus.read8(A6 + 0x320) & 0x40)
    {
        bus.write16(MUL + 2, bus.read16(table));
        bus.write32(A6 + 0x322, bus.read32(A6 + 0x322) - asr32(bus.read32(MUL_RESULT), 8));
    }
    bus.write16(MUL + 2, bus.read16(table + 2));
    bus.write16(A6 + 0x338, bus.read16(A6 + 0x338) + asr32(bus.read32(MUL_RESULT), 8));
    if (static_cast<int8_t>(bus.read8(A6 + 0x320)) >= 0) return;
    uint16_t d0 = bus.read16(A6 + 0x33a);
    d0 = asr16(static_cast<uint16_t>(d0 * 3), 1);
    bus.write16(A6 + 0x326, bus.read16(A6 + 0x326) - d0);
    bus.write32(DIV + 0x4000, 0x1000f000);
    bus.write16(DIV + 0x4004, bus.read16(A6 + 0x326));
    const uint16_t flags = bus.read16(DIV + 0x4006);
    if (flags) bus.write16(A6 + 0x326, bus.read16(A6 + 0x326) - (s(flags) < 0 ? 0xe000 : 0x2000));
}

// $001758-$0019E6: perspective and texture raster for the special road.
void OSubRoad::special_raster(SubBus& bus)
{
    for (uint32_t n = 0; n < 0x200; n += 4) // $00175E-$00177E
    {
        bus.write32(0xec200 + n, 0x08000800);
        bus.write32(0xec838 + n, 0);
    }
    const auto quotient = [&](uint32_t dividend, uint16_t divisor) {
        bus.write32(DIV, dividend);
        bus.write16(DIV + 20, divisor);
        return bus.read16(DIV + 8);
    };
    const auto product = [&](uint16_t left) {
        bus.write16(MUL, left);
        return bus.read32(MUL_RESULT);
    };
    uint16_t d0 = quotient(0x7fe0, static_cast<uint16_t>(asr16(bus.read16(A6 + 0xc2), 5) + 0x20));
    uint16_t d1 = asr16(d0, 1);
    bus.write16(A6 + 0x334, d0);
    bus.write16(DIV + 20, 0x20);
    uint16_t d4 = bus.read16(DIV + 8), d5 = d4;
    bus.write16(A6 + 0x336, d5);
    d5 = asr16(d5, 1);
    uint16_t d2 = static_cast<uint16_t>(0xf0 - bus.read16(A6 + 0x96) * 2);
    bus.write16(MUL, d0);
    bus.write16(MUL + 2, d2);
    d2 = static_cast<uint16_t>((bus.read32(MUL_RESULT) << 6) >> 16);
    d2 = static_cast<uint16_t>(d2 + 0x77 - bus.read16(A6 + 0x8c));
    bus.write16(A6 + 0x330, d2);
    uint16_t d6 = static_cast<uint16_t>((product(d4) << 6) >> 16);
    d6 = static_cast<uint16_t>(d6 + 0x77 - bus.read16(A6 + 0x8c));
    bus.write16(A6 + 0x332, d6);
    d6 = static_cast<uint16_t>(d6 - d2);
    bus.write16(DIV + 4, d6);
    d5 = static_cast<uint16_t>(d5 - d1);
    bus.write32(DIV + 16, sext(d5) << 8);
    d5 = bus.read16(DIV + 8);
    uint32_t a1 = 0xec200 + sext(static_cast<uint16_t>(d2 * 2));
    uint16_t d3 = bus.read16(A6 + 0x332);
    if (s(d3) > 0xe0) d6 = static_cast<uint16_t>(d6 - static_cast<uint16_t>(d3 - 0xe0));
    d6 = static_cast<uint16_t>(d6 - 1);
    if (s(d6) >= 0) // $00182A-$00183A
    {
        uint32_t ramp = static_cast<uint32_t>(d1) << 16;
        const uint32_t step = sext(d5) << 8;
        do { bus.write16(a1, ramp >> 16); a1 += 2; ramp += step; } while (d6-- != 0);
    }
    bus.write32(DIV, 0x7fe0); // $00183E
    uint32_t distance = bus.read32(A6 + 0x322), far = distance;
    bool edge = false;
    if (static_cast<int32_t>(distance) < 0) // $00184E
    {
        d2 = bus.read16(A6 + 0x336);
        d4 = bus.read16(A6 + 0x332);
        d3 = asr16(d2, 1);
    }
    else if (distance >= bus.read16(A6 + 0xc2)) edge = true; // $001862
    else
    {
        bus.write8(A6 + 0x320, bus.read8(A6 + 0x320) | 0x80); // TAS $001868
        d2 = quotient(0x7fe0, static_cast<uint16_t>(asr16(static_cast<uint16_t>(distance), 5) + 0x20));
        d3 = asr16(d2, 1);
        d4 = static_cast<uint16_t>(((product(d2) << 6) >> 16) + 0x70 - bus.read16(A6 + 0x8c));
    }
    if (!edge)
    {
        far += 0x4fec0; // $00189A
        if (static_cast<int32_t>(far) < 0)
        {
            bus.write8(A6 + 0x320, bus.read8(A6 + 0x320) | 0x10);
            edge = true;
        }
        else
        {
            if (static_cast<int32_t>(far) > static_cast<int32_t>(bus.read16(A6 + 0xc2)))
            {
                d0 = bus.read16(A6 + 0x334);
                d5 = bus.read16(A6 + 0x330);
                d1 = asr16(d0, 1);
            }
            else // $0018C4; BSET tests the previous bit.
            {
                const uint8_t old = bus.read8(A6 + 0x320);
                bus.write8(A6 + 0x320, old | 0x20);
                if (!(old & 0x20)) bus.write8(A6 + 0x320, bus.read8(A6 + 0x320) & 0xbf);
                bus.write16(DIV + 20, static_cast<uint16_t>(asr16(static_cast<uint16_t>(far), 5) + 0x20));
                d0 = bus.read16(DIV + 8);
                d1 = asr16(d0, 1);
                d5 = static_cast<uint16_t>(((product(d0) << 6) >> 16) + 0x70 - bus.read16(A6 + 0x8c));
            }
            // $0018F8: projected lateral endpoints and their fixed-point slope.
            bus.write16(MUL, d2);
            bus.write16(MUL + 2, bus.read16(A6 + 0x326));
            d6 = static_cast<uint16_t>(((bus.read32(MUL_RESULT) << 6) >> 16) + bus.read16(A6 + 0x8e));
            uint16_t d7 = static_cast<uint16_t>(((product(d0) << 6) >> 16) + bus.read16(A6 + 0x8e));
            d3 = static_cast<uint16_t>(d3 - d1);
            d6 = static_cast<uint16_t>(d6 - d7);
            d6 = quotient(sext(d6) << 8, d3);
            const uint32_t step = sext(d6) << 8;
            a1 = 0xec800 + sext(static_cast<uint16_t>(d1 * 2));
            d7 = static_cast<uint16_t>(-d7 + 0x5b0);
            bus.write16(a1, d7);
            d3 = static_cast<uint16_t>(d3 - 1);
            if (s(d3) < 0) edge = false; // BMI $00197E skips the edge strip.
            else
            {
                uint32_t ramp = static_cast<uint32_t>(d7) << 16;
                do { bus.write16(a1, ramp >> 16); a1 += 2; ramp -= step; } while (d3-- != 0);
                edge = true; // falls through $00195A
            }
        }
    }
    if (edge) // $00195A-$00197A
    {
        a1 = 0xec200 + sext(static_cast<uint16_t>(bus.read16(A6 + 0x330) * 2));
        uint32_t stripe = 0x080d080d;
        for (int n = 0; n < 14; ++n) { a1 -= 4; bus.write32(a1, stripe); stripe -= 0x00010001; }
    }
    // $00197E-$0019E2: ROM texture transition bits select four shared colors.
    a1 = 0xecc38;
    d0 = bus.read16(A6 + 0x338);
    d3 = static_cast<uint16_t>((0xc00 - (d0 & 0xc00)) & 0xc00);
    d3 = static_cast<uint16_t>((d3 << 7) | (d3 >> 9));
    uint32_t a0 = 0x19e8 + (d0 & 0x3e0);
    for (int row = 0; row < 16; ++row)
    {
        d0 = bus.read16(a0); a0 += 2;
        for (int bit = 0; bit < 16; ++bit)
        {
            bus.write16(a1, bus.read16(A6 + 0x328 + sext(d3))); a1 += 2;
            const bool carry = d0 & 0x8000;
            d0 = static_cast<uint16_t>(d0 * 2);
            if (carry) d3 = static_cast<uint16_t>((d3 + 2) & 6);
        }
    }
}

// $000436-$00044A: consume the main CPU's shift-register road requests.
void OSubRoad::service_requests(SubBus& bus)
{
    const uint16_t blank=bus.read16(A6+0x300);
    bus.write16(A6+0x300,blank>>1);
    if (blank&1) initialize(bus,0x5c4);
    const uint16_t texture=bus.read16(A6+0x302);
    bus.write16(A6+0x302,texture>>1);
    if (texture&1) initialize(bus,0x5ee);
}
void OSubRoad::initialize(SubBus& bus,uint32_t entry)
{
    if (entry==0x5c4) // $0005C4-$0005DA: blank both road line tables.
        for (uint32_t a=0xec000;a<0xec400;a+=4) bus.write32(a,0x08000800);
    else if (entry==0x5ee) // $0005EE-$000602: road 0's texture line selector.
        for (uint32_t a=0xec000;a<0xec1c0;a+=2) bus.write16(a,0x0100);
}

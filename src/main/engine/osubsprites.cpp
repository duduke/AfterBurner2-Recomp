#include "engine/osubsprites.hpp"

// Literal port of sub CPU ROM $000648-$0009E2. Comments give the ROM address
// of each block; `goto` labels are named after their branch targets so the
// control flow can be checked against the disassembly
// (tools/m68k_disasm.py on a subcpu image from tools/build_rom_image.py).

namespace
{
    constexpr uint32_t A6 = SubBus::A6;
    constexpr uint32_t DIV = 0x0e4000;          // A2 during the transform
    constexpr uint32_t MUL = DIV - 0x4000;      // -16384(A2)
    constexpr uint32_t MUL_RESULT = MUL + 4;    // -16380(A2): 32-bit product
    constexpr uint32_t MUL_LOW = MUL + 6;       // -16378(A2): product low word
    constexpr uint32_t ZOOM_TABLE = 0x0009e4;   // 8-byte entries for zoom < $40

    // Shared-RAM variables, as A6-relative offsets.
    constexpr uint32_t SKIP_SORT = 0x003a;      // bit 0 set: no sort this pass
    constexpr uint32_t SPRITE_PRIORITY = 0x0056;// OR'd into every vertical zoom word
    constexpr uint32_t BUCKETS = 0x5800;        // 256 x 32 bytes: count word + up to 30 indices
    constexpr uint32_t SIZE_TABLE = 0x7800;     // per-object scaled width/height
    constexpr uint32_t REQUESTS = 0x7c08;       // -> unsorted 16-byte requests
    constexpr uint32_t REQUEST_COUNT = 0x7c10;  // -> word: number of requests
    constexpr uint32_t SORTED = 0x7c18;         // -> output descriptors (interpolated copy at +$C00)
    constexpr uint32_t SORTED_COUNT = 0x7c20;   // -> word: number written
    constexpr uint32_t ORDER = 0x7d00;          // sorted list of request indices

    inline uint32_t sext(uint16_t w) { return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(w))); }
    inline int16_t s(uint16_t w) { return static_cast<int16_t>(w); }
    inline uint32_t swap(uint32_t v) { return (v << 16) | (v >> 16); }
    inline uint16_t ror16(uint16_t v, int n) { return static_cast<uint16_t>((v >> n) | (v << (16 - n))); }
}

void OSubSprites::pass(SubBus& bus)
{
    uint32_t a0, a1, a2, a3, a4, a5;
    uint16_t d0, d1, d2, d6, d7;

    // $000648
    if (bus.read8(A6 + SKIP_SORT) & 1)
        goto L706;

    a0 = bus.read32(A6 + REQUEST_COUNT);
    d7 = bus.read16(a0);
    d6 = d7;
    if (s(d6) <= 0) goto L706;           // BLE
    if (s(d7) >= 0xff) goto L706;        // BGE

    // $000666: bucket each request by its depth byte (request +2). A full
    // bucket (30 entries) spills into the next deeper one; past the last,
    // the request is dropped.
    a2 = bus.read32(A6 + REQUESTS) + 2;
    d2 = 0;
    d6 = static_cast<uint16_t>(d6 - 1);
    do
    {
        d0 = bus.read8(a2);                                  // $000670
        d1 = static_cast<uint16_t>(0xff - d0);
        a2 += 0x10;
        d0 = static_cast<uint16_t>(d0 << 5);
        a0 = A6 + BUCKETS + sext(d0);
        for (;;)
        {
            if (s(bus.read16(a0)) < 0x1e)                    // $000686
            {
                bus.write16(a0, static_cast<uint16_t>(bus.read16(a0) + 1));   // $000698
                d0 = bus.read16(a0);
                a0 += 2;
                bus.write8(a0 + sext(d0) - 1, static_cast<uint8_t>(d2));
                break;
            }
            a0 += 32;
            d1 = static_cast<uint16_t>(d1 - 1);              // DBF D1
            if (d1 == 0xffff) break;
        }
        d2 = static_cast<uint16_t>(d2 + 1);                  // $0006A0
        d6 = static_cast<uint16_t>(d6 - 1);                  // DBF D6
    } while (d6 != 0xffff);

    // $0006A6: drain the buckets shallow-to-deep into the order list,
    // leaving every bucket count back at zero.
    a1 = A6 + ORDER;
    a0 = A6 + BUCKETS - 32;
    d6 = 0x100;
    for (;;)
    {
        a0 += 32;                                            // $0006B2
        const bool borrow = d6 == 0;
        d6 = static_cast<uint16_t>(d6 - 1);
        if (borrow) break;                                   // BCS $0006D2
        if (bus.read16(a0) == 0) continue;
        a2 = a0 + 2;
        bool all_emitted = false;
        for (;;)
        {
            bus.write8(a1++, bus.read8(a2++));               // $0006C2
            d7 = static_cast<uint16_t>(d7 - 1);
            if (d7 == 0)
            {
                bus.write16(a0, 0);                          // $0006D0
                all_emitted = true;
                break;
            }
            const uint16_t left = static_cast<uint16_t>(bus.read16(a0) - 1);
            bus.write16(a0, left);
            if (left == 0) break;
        }
        if (all_emitted) break;
    }

    // $0006D2: transform each request in sorted order. The loop runs for
    // the full request count even if some were dropped above.
    a3 = A6 + ORDER;
    a0 = bus.read32(A6 + REQUEST_COUNT);
    a4 = bus.read32(A6 + SORTED);
    {
        uint16_t remaining = bus.read16(a0);                 // MOVE.W (A0),-(A7)
        while (remaining != 0)
        {
            a5 = bus.read32(A6 + REQUESTS);                  // $0006E8
            d0 = bus.read8(a3++);
            d0 = static_cast<uint16_t>(d0 << 4);
            a5 += sext(d0);
            bus.write16(a4 + 14, d0);   // request offset, read back by interpolate()
            transform(bus, a5, a4);
            remaining = static_cast<uint16_t>(remaining - 1);
        }
    }
    bus.write16(a4, 0xffff);                                 // $000700

L706:
    a0 = bus.read32(A6 + SORTED);
    a1 = bus.read32(A6 + REQUEST_COUNT);
    d0 = bus.read16(a1);
    a2 = bus.read32(A6 + SORTED_COUNT);
    bus.write16(a2, d0);
    d0 = static_cast<uint16_t>(d0 << 4);
    bus.write32(a0 + sext(d0), 0xffffffff);
    interpolate(bus);
}

// $000726: copy each descriptor (and the end marker after them) to +$C00,
// moving top line and X position halfway back toward the request's
// previous-frame position, unless the request's flags opt out (bit 3: Y,
// bit 2: X).
void OSubSprites::interpolate(SubBus& bus)
{
    const uint32_t a5 = bus.read32(A6 + REQUESTS);
    uint16_t d0 = bus.read16(bus.read32(A6 + SORTED_COUNT));
    uint32_t a0 = bus.read32(A6 + SORTED);
    uint32_t a1 = a0 + 0xc00;

    const auto halfway = [&](uint32_t field, uint16_t delta)
    {
        uint16_t d3 = bus.read16(field);
        const uint16_t d4 = static_cast<uint16_t>(d3 & 0xfe00);
        d3 = (d3 & 0x100) ? static_cast<uint16_t>(d3 | 0xfe00) : static_cast<uint16_t>(d3 & 0x1ff);
        d3 = static_cast<uint16_t>((d3 - delta) & 0x1ff);
        bus.write16(field, static_cast<uint16_t>(d4 | d3));
    };

    do
    {
        for (int i = 0; i < 3; ++i)                          // $000738
        {
            bus.write32(a1, bus.read32(a0));
            a0 += 4;
            a1 += 4;
        }
        bus.write16(a1, bus.read16(a0));
        a0 += 2;
        const uint32_t a2 = a5 + sext(bus.read16(a0));
        a0 += 2;

        if (!(bus.read8(a2) & 0x08))                         // $000744
        {
            const uint16_t delta = static_cast<uint16_t>(
                s(static_cast<uint16_t>(bus.read16(a2 + 6) - bus.read16(a2 + 10))) >> 1);
            halfway(a1 - 12, delta);
        }
        if (!(bus.read8(a2) & 0x04))                         // $00077A
        {
            const uint16_t delta = static_cast<uint16_t>(
                s(static_cast<uint16_t>(bus.read16(a2 + 4) - bus.read16(a2 + 8))) >> 1);
            halfway(a1 - 8, delta);
        }
        a1 += 4;                                             // $0007B0
        d0 = static_cast<uint16_t>(d0 - 1);                  // DBF D0
    } while (d0 != 0xffff);
}

// $0007B8: turn one request (A5) into a hardware sprite descriptor (A4).
//
// Request: +0 flags/zoom (bits 0-9 zoom, 0 = hidden; 12/13 vertical/
// horizontal flip; 14: Y is the bottom edge instead of the middle), +2
// depth, +3 palette, +4/+6 X/Y,
// +8/+10 previous X/Y, +12 byte: size-table slot, +12 long: frame record.
// Frame record (sub ROM): +0 width, +2 height, +4 descriptor word 0 (bank),
// +6 ROM offset, +8 pitch, +10 offset adjustment when flipped vertically.
// Off-screen sprites get only the hide bit (descriptor byte 0 = $40).
void OSubSprites::transform(SubBus& bus, uint32_t a5, uint32_t& a4)
{
    uint32_t a0, a1, d0l, d1l;
    uint16_t d0, d1, d2, d4, d5, d6, d7, t;
    uint8_t flags;   // D0's low byte: descriptor byte 8 (direction/flip/pitch sign)

    a0 = bus.read32(a5 + 12);
    const uint16_t d3 = bus.read16(a5);
    d1 = static_cast<uint16_t>(d3 & 0x3ff);
    if (d1 == 0) goto L9C8;

    if (d1 < 0x40)                                           // BCS $000806
    {
        // Very small zoom: table gives the hardware zoom, a scale factor and
        // which (lower-detail) frame record to use instead.
        a1 = ZOOM_TABLE + sext(static_cast<uint16_t>(d1 << 3));
        d1 = bus.read16(a1);
        a1 += 2;
        bus.write16(MUL, bus.read16(a1));
        a1 += 2;
        a0 += sext(bus.read16(a1));
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        uint32_t w = bus.read32(MUL_RESULT);
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        uint32_t h = bus.read32(MUL_RESULT);
        w += w; w += w; w = swap(w);                         // $000826
        h += h; h += h; h = swap(h);
        d4 = static_cast<uint16_t>(w);
        d5 = static_cast<uint16_t>(h);
    }
    else
    {
        // $0007CE: scaled size = size * zoom / $7F; hardware zoom = $FE00 / zoom.
        bus.write16(MUL, d1);
        bus.write16(DIV + 4, 0x7f);
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        bus.write32(DIV + 16, bus.read32(MUL_RESULT));
        d4 = bus.read16(DIV + 8);
        bus.write16(MUL + 2, bus.read16(a0));
        a0 += 2;
        bus.write32(DIV + 16, bus.read32(MUL_RESULT));
        d5 = bus.read16(DIV + 8);
        bus.write32(DIV, 0xfe00);
        bus.write16(DIV + 20, d1);
        d1 = bus.read16(DIV + 8);
    }

    // $000832: record scaled size for the object, start the descriptor.
    a1 = A6 + SIZE_TABLE + sext(static_cast<uint16_t>(bus.read8(a5 + 12) * 4));
    bus.write16(a1, d4);
    bus.write16(a1 + 2, d5);
    bus.write16(a4 + 8, d1);
    d1 = static_cast<uint16_t>(d1 | bus.read16(A6 + SPRITE_PRIORITY));
    bus.write16(a4 + 6, d1);

    // $000852: top line (D7) and left edge (D6), with the hardware's
    // +$100 / +$160 origin offsets.
    d7 = bus.read16(a5 + 6);
    if (d3 & 0x1000) d7 = static_cast<uint16_t>(d7 + d5);
    d1 = d5;
    if (!(d3 & 0x4000)) d1 = static_cast<uint16_t>(d1 >> 1);
    d7 = static_cast<uint16_t>(d7 - d1 + 0x100);

    d6 = bus.read16(a5 + 4);
    d1 = d4;
    {
        const uint16_t x_flag = d1 & 1;                      // LSR.W #1 -> X
        d1 = static_cast<uint16_t>(d1 >> 1);
        d6 = static_cast<uint16_t>(d6 - d1 - x_flag);        // SUBX.W D1,D6
    }
    d6 = static_cast<uint16_t>(d6 + 0x160);
    d1 = static_cast<uint16_t>(d6 + d4);                     // right edge
    if (s(d1) < 0xc0) goto L9DA;
    if (s(d6) >= 0x200) goto L9DA;

    bus.write16(a4, bus.read16(a0));                         // $000890
    a0 += 2;
    d2 = bus.read16(a0);
    a0 += 2;
    d4 = bus.read16(a0);                                     // pitch from here on
    a0 += 2;

    if (s(d6) < 0)                                           // TST.W D6 / BPL $0008DE
    {
        // Clipped far left: advance the ROM offset past the hidden columns.
        t = static_cast<uint16_t>(0xc0 - d6);
        t = static_cast<uint16_t>(t >> 3);
        bus.write16(MUL, bus.read16(a4 + 8));
        bus.write16(MUL + 2, t);
        d0l = swap(bus.read32(MUL_RESULT) << 7);
        if (d3 & 0x2000) d2 = static_cast<uint16_t>(d2 - d0l);
        else d2 = static_cast<uint16_t>(d2 + d0l);
        d0l = sext(static_cast<uint16_t>(d0l)) << 8;         // $0008C0
        d0l += d0l;
        bus.write32(DIV, d0l);
        bus.write32(DIV + 20, bus.read32(a4 + 8));
        d6 = static_cast<uint16_t>(d6 + (bus.read16(DIV + 8) << 3));
        flags = 0xe0;
        goto L8FE;
    }

    flags = 0xe0;                                            // $0008DE
    if (s(d6) >= 0xc0) goto L8FE;
    if (s(d1) >= 0x200) goto L8FE;
    flags &= static_cast<uint8_t>(~0x20);   // partly off the left edge: draw right-to-left from the right edge
    t = d1; d1 = d6; d6 = t;                                 // EXG D1,D6
    d6 = static_cast<uint16_t>(d6 - 1);
    if (d3 & 0x2000) goto L90C;
    goto L904;

L8FE:
    if (!(d3 & 0x2000)) goto L90C;
L904:
    flags &= static_cast<uint8_t>(~0x40);
    d2 = static_cast<uint16_t>(d2 + d4 - 1);
L90C:
    d6 &= 0x1ff;

    // $000910: vertical extent and clipping.
    d1 = d7;
    if (d3 & 0x1000) d7 = static_cast<uint16_t>(d7 - d5);
    else d1 = static_cast<uint16_t>(d1 + d5);
    if (s(d1) < 0x100) goto L9DA;
    if (s(d7) >= 0x1e0) goto L9DA;
    if (s(d7) >= 0x100) goto L984;

    // Clipped at the top: draw bottom-to-top from the bottom line.
    flags &= static_cast<uint8_t>(~0x80);
    t = d1; d1 = d7; d7 = t;                                 // EXG D1,D7
    d7 = static_cast<uint16_t>(d7 - 1);
    if (s(d7) < 0x200) goto L97A;

    // Also off the bottom: skip the hidden rows in the ROM offset.
    d1 = static_cast<uint16_t>(d7 - 0x1e0);                  // $000942
    bus.write16(MUL, d1);
    bus.write16(MUL + 2, bus.read16(a4 + 8));
    d1l = swap(bus.read32(MUL_RESULT) << 7);
    bus.write16(MUL, static_cast<uint16_t>(d1l));
    bus.write16(MUL + 2, d4);
    d2 = static_cast<uint16_t>(d2 - bus.read16(MUL_LOW));
    d1l = sext(static_cast<uint16_t>(d1l)) << 9;             // EXT.L / MOVEQ #9,D5 / ASL.L D5,D1
    d5 = 9;
    bus.write32(DIV, d1l);
    bus.write16(DIV + 20, bus.read16(a4 + 8));
    d7 = static_cast<uint16_t>(d7 - bus.read16(DIV + 8));

L97A:
    d5 = static_cast<uint16_t>((d5 & 0xff00) | (d7 & 0xff));  // MOVE.B D7,D5
    if (d3 & 0x1000) goto L99E;
    goto L996;

L984:
    if (s(d5) > 0xff) d5 = static_cast<uint16_t>(0x1e0 - d7);
    if (!(d3 & 0x1000)) goto L99E;
L996:
    d4 = static_cast<uint16_t>(-d4);
    d2 = static_cast<uint16_t>(d2 + bus.read16(a0));
    flags |= 0x10;   // pitch sign (descriptor word 4 bit 12)
L99E:
    d7 &= 0x1ff;
    d4 = ror16(static_cast<uint16_t>(d4 & 0x7f), 7);
    d6 = static_cast<uint16_t>(d6 | d4);
    bus.write16(a4, static_cast<uint16_t>(bus.read16(a4) | d7));
    bus.write16(a4 + 2, d2);
    bus.write16(a4 + 4, d6);
    bus.write8(a4 + 8, static_cast<uint8_t>(bus.read8(a4 + 8) | flags));
    bus.write16(a4 + 10, static_cast<uint16_t>(d5 & 0xff));
    bus.write8(a4 + 13, bus.read8(a5 + 3));
    a4 += 16;
    return;

L9C8:
    d0 = static_cast<uint16_t>(bus.read8(a5 + 12) * 4);
    bus.write32(A6 + SIZE_TABLE + sext(d0), 0);
L9DA:
    bus.write8(a4, 0x40);
    a4 += 16;
}

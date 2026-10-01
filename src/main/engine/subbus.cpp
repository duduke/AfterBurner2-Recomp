#include "engine/subbus.hpp"

namespace
{
    constexpr uint32_t BUS_MASK = 0xfffff;

    // subram0 $080000-$09FFFF and subram1 $0A0000-$0BFFFF, each a 16KB chip
    // mirrored every $4000.
    inline bool is_ram(uint32_t a) { return a >= 0x80000 && a < 0xc0000; }
    inline uint32_t ram_offset(uint32_t a) { return (a < 0xa0000 ? 0 : 0x4000) + (a & 0x3fff); }

    inline bool is_mul(uint32_t a) { return a >= 0xe0000 && a < 0xe4000; }
    inline bool is_div(uint32_t a) { return a >= 0xe4000 && a < 0xe8000; }
}

SubBus::SubBus(const uint8_t* rom_data, uint32_t length, uint8_t* shared_ram)
    : rom(rom_data), rom_length(length), shared(shared_ram)
{
}

uint8_t SubBus::read8(uint32_t addr) const
{
    const uint32_t a = addr & BUS_MASK;
    if (a < 0x80000)
        return a < rom_length ? rom[a] : 0;
    if (is_ram(a))
        return shared[ram_offset(a)];
    if (is_mul(a) || is_div(a))
    {
        const uint16_t word = read16(a & ~1u);
        return static_cast<uint8_t>((a & 1) ? word : word >> 8);
    }
    return 0xff;
}

uint16_t SubBus::read16(uint32_t addr) const
{
    const uint32_t a = addr & BUS_MASK;
    if (a < 0x80000 || is_ram(a))
        return static_cast<uint16_t>((read8(a) << 8) | read8(a + 1));
    if (is_mul(a))
        return mul.read(a >> 1);
    if (is_div(a))
        return div.read(a >> 1);
    return 0xffff;
}

uint32_t SubBus::read32(uint32_t addr) const
{
    return (static_cast<uint32_t>(read16(addr)) << 16) | read16(addr + 2);
}

void SubBus::write8(uint32_t addr, uint8_t data)
{
    const uint32_t a = addr & BUS_MASK;
    if (is_ram(a))
        shared[ram_offset(a)] = data;
}

void SubBus::write16(uint32_t addr, uint16_t data)
{
    const uint32_t a = addr & BUS_MASK;
    if (is_ram(a))
    {
        shared[ram_offset(a)] = static_cast<uint8_t>(data >> 8);
        shared[ram_offset(a + 1)] = static_cast<uint8_t>(data);
    }
    else if (is_mul(a))
        mul.write(a >> 1, data);
    else if (is_div(a))
        div.write(a >> 1, data);
}

// The 68000 splits a long access into two word accesses, high word first —
// which matters for the divider, where each word write can trigger a divide.
void SubBus::write32(uint32_t addr, uint32_t data)
{
    write16(addr, static_cast<uint16_t>(data >> 16));
    write16(addr + 2, static_cast<uint16_t>(data));
}

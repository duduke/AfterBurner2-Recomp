#include "engine/mainbus.hpp"
#include <algorithm>

namespace
{
    constexpr uint32_t MASK = 0x3fffff;
    uint16_t word(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
}

MainBus::MainBus(const uint8_t* main_rom, uint32_t main_length,
                 const uint8_t* sub_rom, uint32_t sub_length, Memory& memory)
    : sub(sub_rom, sub_length, memory.bytes.data() + SHARED, memory.bytes.data() + ROAD),
      rom(main_rom), rom_length(main_length), m(memory)
{
    sub.road_swap = [this] {
        std::swap_ranges(m.bytes.begin() + ROAD, m.bytes.end(), m.road_buffer.begin());
        if (endpoints.road_swap) endpoints.road_swap();
    };
    sub.road_control = [this](uint8_t value) {
        m.road_control = value;
        if (endpoints.road_control) endpoints.road_control(value);
    };
}

uint32_t MainBus::memory_offset(uint32_t addr)
{
    const uint32_t a = addr & MASK;
    if (a >= 0x080000 && a < 0x0c0000)
        return WORK + (a < 0x0a0000 ? 0 : 0x4000) + (a & 0x3fff);
    if (a >= 0x3f8000) return WORK + (a - 0x3f8000);
    if (a >= 0x280000 && a < 0x2c0000)
        return SHARED + (a < 0x2a0000 ? 0 : 0x4000) + (a & 0x3fff);
    if (a >= 0x0c0000 && a < 0x0d0000) return TILE + (a & 0xffff);
    if (a >= 0x0d0000 && a < 0x0e0000) return TEXT + (a & 0xfff);
    if (a >= 0x100000 && a < 0x110000) return SPRITE + (a & 0xfff);
    if (a >= 0x120000 && a < 0x130000) return PALETTE + (a & 0x3fff);
    if (a >= 0x2ec000 && a < 0x2ee000) return ROAD + (a & 0xfff);
    return UNMAPPED;
}

uint16_t MainBus::read_word(uint32_t a, uint16_t mask)
{
    if (trace && trace->before_access) trace->before_access(false, a, mask);
    uint16_t value = 0xffff;
    const uint32_t o = memory_offset(a);
    if (o != UNMAPPED)
    {
        value = word(m.bytes.data() + o);
        if (trace && trace->read)
        {
            if (mask & 0xff00) trace->read[o] = 1;
            if (mask & 0xff) trace->read[o + 1] = 1;
        }
    }
    else if (a < 0x80000)
        value = a + 1 < rom_length ? word(rom + a) : 0;
    else if (a >= 0x200000 && a < 0x280000) value = sub.read16(a);
    else if (a >= 0xe0000 && a < 0xe4000) value = mul.read(a >> 1);
    else if (a >= 0xe4000 && a < 0xe8000) value = div.read(a >> 1);
    else if (a >= 0xe8000 && a < 0xec000) value = compare_timer.read(a >> 1);
    else if (a >= 0x2e0000 && a < 0x2ec000) value = sub.read16(a);
    else if (a >= 0x2ee000 && a < 0x2f0000) value = sub.read16(a);
    else if (a >= 0x130000 && a < 0x140000)
    {
        if ((mask & 0xff) && endpoints.adc_read) value = 0xff00 | endpoints.adc_read();
    }
    else if (a >= 0x140000 && a < 0x160000)
    {
        if (mask & 0xff) value = 0xff00 | io[(a >> 16) - 0x14].read((a >> 1) & 7);
    }
    if (trace && trace->access) trace->access(false, a, value, mask);
    return value;
}

void MainBus::write_word(uint32_t a, uint16_t data, uint16_t mask)
{
    if (trace && trace->before_access) trace->before_access(true, a, mask);
    const uint32_t o = memory_offset(a);
    if (o != UNMAPPED)
    {
        if (mask & 0xff00)
        {
            m.bytes[o] = static_cast<uint8_t>(data >> 8);
            if (trace && trace->written) trace->written[o] = 1;
            if (trace && trace->write_values) trace->write_values[o] = static_cast<uint8_t>(data >> 8);
        }
        if (mask & 0xff)
        {
            m.bytes[o + 1] = static_cast<uint8_t>(data);
            if (trace && trace->written) trace->written[o + 1] = 1;
            if (trace && trace->write_values) trace->write_values[o + 1] = static_cast<uint8_t>(data);
        }
        if (o >= TILE && endpoints.video_write)
        {
            // Canonical hardware addresses, with the merged full word.
            uint32_t base = o < TEXT ? 0xc0000 + o - TILE :
                            o < SPRITE ? 0xd0000 + o - TEXT :
                            o < PALETTE ? 0x100000 + o - SPRITE :
                            o < ROAD ? 0x120000 + o - PALETTE : 0x2ec000 + o - ROAD;
            endpoints.video_write(base, word(m.bytes.data() + o));
        }
    }
    else if (a >= 0xe0000 && a < 0xe4000) mul.write(a >> 1, data, mask);
    else if (a >= 0xe4000 && a < 0xe8000) div.write(a >> 1, data, mask);
    else if (a >= 0xe8000 && a < 0xec000) compare_timer.write(a >> 1, data, mask);
    else if (a >= 0x2e0000 && a < 0x2f0000)
    {
        if (mask == 0xffff) sub.write16(a, data);
        else sub.write8(a + (mask == 0xff ? 1 : 0), static_cast<uint8_t>(mask == 0xff ? data : data >> 8));
    }
    else if (a >= 0x110000 && a < 0x120000)
    {
        std::swap_ranges(m.bytes.begin() + SPRITE, m.bytes.begin() + PALETTE, m.sprite_buffer.begin());
        // sega_16bit_sprite_device::draw_write: after exchanging the RAM
        // halves, the CPU-visible first word is forced to the end marker.
        m.bytes[SPRITE]=0xff;m.bytes[SPRITE+1]=0xff;
        if (endpoints.sprite_draw) endpoints.sprite_draw();
    }
    else if (a >= 0x130000 && a < 0x140000)
    {
        if ((mask & 0xff) && endpoints.adc_write) endpoints.adc_write(static_cast<uint8_t>(data));
    }
    else if (a >= 0x140000 && a < 0x160000)
    {
        if (mask & 0xff) io[(a >> 16) - 0x14].write((a >> 1) & 7, static_cast<uint8_t>(data));
    }
    // $160000 force-input writes are ignored by MAME for aburner2.
    if (trace && trace->access) trace->access(true, a, data, mask);
}

uint8_t MainBus::read8(uint32_t addr)
{
    const uint32_t a = addr & MASK;
    const uint16_t w = read_word(a & ~1u, a & 1 ? 0xff : 0xff00);
    return static_cast<uint8_t>(a & 1 ? w : w >> 8);
}
uint16_t MainBus::read16(uint32_t addr) { return read_word((addr & MASK) & ~1u, 0xffff); }
uint32_t MainBus::read32(uint32_t addr) { const auto high=read16(addr);return (uint32_t(high)<<16)|read16(addr+2); }
void MainBus::write8(uint32_t addr, uint8_t data)
{
    const uint32_t a = addr & MASK;
    write_word(a & ~1u, a & 1 ? data : static_cast<uint16_t>(data << 8), a & 1 ? 0xff : 0xff00);
}
void MainBus::write16(uint32_t addr, uint16_t data) { write_word((addr & MASK) & ~1u, data, 0xffff); }
void MainBus::write32(uint32_t addr, uint32_t data)
{
    write16(addr, static_cast<uint16_t>(data >> 16));
    write16(addr + 2, static_cast<uint16_t>(data));
}

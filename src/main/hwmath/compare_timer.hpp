#pragma once

#include <array>
#include <cstdint>
#include <functional>

// Register-level 315-5250 model, derived from MAME's BSD-3-Clause
// segaic16_m.cpp (Aaron Giles). Offsets are words, masks select active bits.
// The host supplies EXCK edges, and consumes the IRQ/NMI line callbacks.
// Bounded callers deliver sound writes immediately; a scheduled host can
// queue them through schedule_sound. The Z80 read acknowledges NMI, not the 68K.
class CompareTimer5250
{
public:
    struct State
    {
        std::array<uint16_t, 16> regs{};
        uint16_t counter = 0;
        uint8_t history_bit = 0;
        bool exck = false, irq = false, nmi = false;
    };

    std::function<void(bool)> irq_line, nmi_line;
    std::function<void(uint8_t)> sound_command;
    // A scheduled host queues MAME's synchronize(write_to_sound) at the
    // CPU transaction clock, then calls deliver_sound at the global frontier.
    std::function<void(uint8_t)> schedule_sound;
    void deliver_sound(uint8_t data) {
        s.regs[11]=data;s.nmi=true;
        if(sound_command)sound_command(data);
        if(nmi_line)nmi_line(true);
    }

    const State& state() const { return s; }
    void load(const State& value) { s = value; }
    void reset()
    {
        // MAME device_reset leaves the external clock level unchanged.
        const bool clock = s.exck;
        s = {};
        s.exck = clock;
        ack();
        if (nmi_line) nmi_line(false);
    }

    uint16_t read(uint32_t offset, bool side_effects = true)
    {
        switch (offset & 15)
        {
            case 0: case 1: case 2: case 3: case 4: case 7: return s.regs[offset & 15];
            case 5: return s.regs[1];
            case 6: return s.regs[2];
            case 9: case 13: if (side_effects) ack(); break;
        }
        return 0xffff;
    }

    void write(uint32_t offset, uint16_t data, uint16_t mask = 0xffff)
    {
        switch (offset & 15)
        {
            case 0: case 1: merge(offset & 15, data, mask); compare(false); break;
            case 2: merge(2, data, mask); compare(true); break;
            case 4: s.regs[4] = 0; s.history_bit = 0; break;
            case 6: merge(2, data, mask); compare(false); break;
            case 8: case 12: merge(8, data, mask); break;
            case 9: case 13: ack(); break;
            case 10: case 14: merge(10, data, mask); break;
            // MAME sends data & 0xff even on a high-byte-only write.
            case 11: case 15:
                if(schedule_sound)schedule_sound(static_cast<uint8_t>(data));
                else deliver_sound(static_cast<uint8_t>(data));
                break;
        }
    }

    uint8_t sound_read(bool side_effects = true)
    {
        if (side_effects)
        {
            s.nmi = false;
            if (nmi_line) nmi_line(false);
        }
        return static_cast<uint8_t>(s.regs[11]);
    }

    void clock(bool level)
    {
        if (s.exck == level) return;
        s.exck = level;
        if (!level) return;
        const uint16_t old = s.counter;
        if (s.regs[10] & 1) ++s.counter;
        if (old == 0xfff)
        {
            s.counter = s.regs[8] & 0xfff;
            s.irq = true;
            if (irq_line) irq_line(true);
        }
    }

private:
    State s;
    void merge(unsigned r, uint16_t data, uint16_t mask)
    {
        s.regs[r] = static_cast<uint16_t>((s.regs[r] & ~mask) | (data & mask));
    }
    void ack() { s.irq = false; if (irq_line) irq_line(false); }
    void compare(bool history)
    {
        const int16_t a = static_cast<int16_t>(s.regs[0]);
        const int16_t b = static_cast<int16_t>(s.regs[1]);
        const int16_t v = static_cast<int16_t>(s.regs[2]);
        const int16_t lo = a < b ? a : b, hi = a > b ? a : b;
        s.regs[3] = v < lo ? 0x8000 : v > hi ? 0x4000 : 0;
        s.regs[7] = static_cast<uint16_t>(v < lo ? lo : v > hi ? hi : v);
        if (history)
        {
            // The history register has 16 observable bits. Past them,
            // comparisons cannot set another bit until offset 4 clears it.
            if (s.history_bit < 16 && s.regs[3] == 0)
                s.regs[4] |= static_cast<uint16_t>(1u << s.history_bit);
            ++s.history_bit;
        }
    }
};

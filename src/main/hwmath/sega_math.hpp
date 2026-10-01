/***************************************************************************
    Sega 315-5248 hardware multiplier and 315-5249 hardware divider.

    Each X-Board 68000 has its own pair, memory-mapped (docs/HARDWARE.md
    section 2). Reimplemented from src/mame/sega/segaic16_m.cpp (MAME,
    BSD-3-Clause) — algorithm only. Register-level, so ported code that
    writes an operand once and reuses it across several reads behaves
    exactly as on hardware.
***************************************************************************/

#pragma once

#include <cstdint>

class Mul5248
{
public:
    // `offset` is the word offset within the chip's mirrored 4-word window.
    uint16_t read(uint32_t offset) const
    {
        const int32_t product = static_cast<int16_t>(regs[0]) * static_cast<int16_t>(regs[1]);
        switch (offset & 3)
        {
            case 0: return regs[0];
            case 1: return regs[1];
            case 2: return static_cast<uint16_t>(product >> 16);
            default: return static_cast<uint16_t>(product & 0xffff);
        }
    }

    void write(uint32_t offset, uint16_t data) { regs[offset & 1] = data; }

    // Restore captured state (both registers are readable on hardware).
    void load(uint16_t a, uint16_t b) { regs[0] = a; regs[1] = b; }

private:
    uint16_t regs[2] = {};
};

class Div5249
{
public:
    // `offset` is the word offset within the chip's mirrored 16-word window.
    uint16_t read(uint32_t offset) const
    {
        switch (offset & 7)
        {
            case 0: return regs[0]; // dividend high
            case 1: return regs[1]; // dividend low
            case 2: return regs[2]; // divisor
            case 4: return regs[4]; // quotient (mode 0) / quotient high (mode 1)
            case 5: return regs[5]; // remainder (mode 0) / quotient low (mode 1)
            case 6: return regs[6]; // flags
            default: return 0xffff;
        }
    }

    // Every write with address line A4 set (word offset bit 3) triggers a
    // divide; A3 (word offset bit 2) selects the mode.
    void write(uint32_t offset, uint16_t data)
    {
        switch (offset & 3)
        {
            case 0: regs[0] = data; break;
            case 1: regs[1] = data; break;
            case 2: regs[2] = data; break;
            default: break;
        }
        if (offset & 8)
            execute((offset & 4) != 0);
    }

    // Restore captured state: registers 0-2 and 4-6 (register 3 is never
    // read or used by a divide).
    void load(const uint16_t r[6])
    {
        regs[0] = r[0]; regs[1] = r[1]; regs[2] = r[2];
        regs[4] = r[3]; regs[5] = r[4]; regs[6] = r[5];
    }

private:
    uint16_t regs[7] = {};

    void execute(bool unsigned_mode)
    {
        regs[6] = 0;
        if (!unsigned_mode)
        {
            const int32_t dividend = static_cast<int32_t>((static_cast<uint32_t>(regs[0]) << 16) | regs[1]);
            const int32_t divisor = static_cast<int16_t>(regs[2]);
            // 64-bit so INT32_MIN / -1 clamps instead of being undefined.
            int64_t quotient;
            if (divisor == 0)
            {
                quotient = dividend;
                regs[6] |= 0x4000;
            }
            else
                quotient = static_cast<int64_t>(dividend) / divisor;

            if (quotient < -32768) { quotient = -32768; regs[6] |= 0x8000; }
            else if (quotient > 32767) { quotient = 32767; regs[6] |= 0x8000; }

            regs[4] = static_cast<uint16_t>(quotient);
            regs[5] = static_cast<uint16_t>(static_cast<int64_t>(dividend) - quotient * divisor);
        }
        else
        {
            const uint32_t dividend = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];
            const uint32_t divisor = regs[2];
            uint32_t quotient;
            if (divisor == 0)
            {
                quotient = dividend;
                regs[6] |= 0x4000;
            }
            else
                quotient = dividend / divisor;

            regs[4] = static_cast<uint16_t>(quotient >> 16);
            regs[5] = static_cast<uint16_t>(quotient & 0xffff);
        }
    }
};

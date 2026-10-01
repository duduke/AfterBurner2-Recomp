#pragma once
#include <array>
#include <cstdint>
#include <functional>

// Sony CXD1095: nibble directions on A-D, individual bits on four-bit E.
// Register behavior derived from MAME's machine/cxd1095.cpp (AJR, BSD-3-Clause).
class IOExpander
{
public:
    std::array<uint8_t, 5> latch{};
    std::array<uint8_t, 5> direction{0xff, 0xff, 0xff, 0xff, 0xff};
    std::function<uint8_t(unsigned)> input;
    std::function<void(unsigned, uint8_t, uint8_t)> output;

    uint8_t read(unsigned r) const
    {
        if (r >= 5) return 0;
        const uint8_t mask = direction[r] & (r == 4 ? 0x0f : 0xff);
        return static_cast<uint8_t>(((mask && input ? input(r) : 0) & mask) | (latch[r] & ~direction[r]));
    }
    void write(unsigned r, uint8_t data)
    {
        if (r < 5)
        {
            latch[r] = data & (r == 4 ? 0x0f : 0xff);
            if (output) output(r, data & ~direction[r], static_cast<uint8_t>(~direction[r]));
        }
        else if (r == 6)
        {
            for (unsigned p = 0; p < 4; ++p, data >>= 2)
                direction[p] = static_cast<uint8_t>((data & 1 ? 0x0f : 0) | (data & 2 ? 0xf0 : 0));
        }
        else if (r == 7) direction[4] = (data & 0x0f) | 0xf0;
    }
};

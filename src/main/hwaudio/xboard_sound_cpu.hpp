#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

// Cycle-stepped ROM execution, separate from audio rendering. Map follows
// MAME segaxbd.cpp sound_map/sound_portmap. Device callbacks must be installed;
// an absent endpoint fails rather than supplying a guessed read value.
class XBoardSoundCpu
{
public:
    enum class Kind : uint8_t { MemoryRead, MemoryWrite, IoRead, IoWrite, InterruptAck };
    struct Event { Kind kind; uint64_t clock; uint16_t pc, address; uint8_t data; };
    struct Registers {
        uint16_t pc, sp, af, bc, de, hl, ix, iy, af2, bc2, de2, hl2;
        uint8_t i, r, im;
        bool iff1, iff2;
    };
    struct Coverage {uint64_t instructions{},nmi_entries{},irq_entries{},retn{},reti{};};
    // PCM offsets are 8 bits; YM offsets are A0. The latch callback owns
    // the 315-5250 acknowledgement and synchronized NMI clearing request.
    std::function<uint8_t(uint8_t)> pcm_read, ym_read;
    std::function<void(uint8_t, uint8_t)> pcm_write, ym_write;
    std::function<uint8_t()> latch_read;
    std::function<void(const Event&)> trace;
    std::array<uint8_t, 0x800> ram{};

    XBoardSoundCpu(const uint8_t* program, size_t length);
    ~XBoardSoundCpu();
    XBoardSoundCpu(const XBoardSoundCpu&) = delete;
    XBoardSoundCpu& operator=(const XBoardSoundCpu&) = delete;
    void reset_line(bool asserted);
    void nmi_line(bool asserted);
    void irq_line(bool asserted);
    void tick(); // one 4 MHz input clock; host owns when it is granted
    unsigned run(unsigned budget); // MAME machine-cycle completion/overrun
    void abort_grant(); // synchronization keeps pending cycle debt
    Registers registers() const;
    uint64_t clocks() const;
    Coverage coverage() const;
private:
    struct State;
    std::unique_ptr<State> state;
};

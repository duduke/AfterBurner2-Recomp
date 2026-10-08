#include "hwaudio/arcade_audio.hpp"
#include "hwaudio/ym2151.hpp"
#include "hwaudio/segapcm.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include "../../third_party/z80.h"

struct ArcadeAudio::State
{
    z80_t cpu{};
    uint64_t pins{};
    std::array<uint8_t, 0x800> ram{};
    std::array<uint8_t, 0x100> pcm_ram{};
    const uint8_t* program;
    uint32_t program_length;
    YM2151 ym{0.35f, 4000000};
    SegaPCM pcm;
    std::deque<uint8_t> commands;
    uint8_t latch{}, ym_address{};
    bool pending{};
    int command_gap{};
    std::array<int16_t, 768> samples{};
    unsigned sample_position{768};

    explicit State(Roms& roms) : program(roms.soundcpu.rom), program_length(roms.soundcpu.length),
        pcm(4000000, &roms.pcm, pcm_ram.data(), SegaPCM::BANK_512)
    {
        pins = z80_init(&cpu);
        ym.init(SAMPLE_RATE);
        pcm.init(SAMPLE_RATE);
    }

    void block()
    {
        // 4 MHz / 125 Hz = 32000 CPU clocks; 48 kHz / 125 = 384 stereo samples.
        for (int tick = 0; tick < 32000; ++tick)
        {
            if (!pending && command_gap == 0 && !commands.empty())
            {
                latch = commands.front(); commands.pop_front(); pending = true;
            }
            if (command_gap > 0) --command_gap;
            pins = (pins & ~(Z80_INT | Z80_NMI)) |
                   ((ym.read_status() & 3) ? Z80_INT : 0) | (pending ? Z80_NMI : 0);
            pins = z80_tick(&cpu, pins);
            const uint16_t address = Z80_GET_ADDR(pins);
            if (pins & Z80_MREQ)
            {
                if (pins & Z80_RD)
                {
                    const uint8_t data = address < 0xf000 ?
                        (address < program_length ? program[address] : 0xff) :
                        address < 0xf800 ? pcm_ram[address & 255] : ram[address & 0x7ff];
                    Z80_SET_DATA(pins, data);
                }
                else if (pins & Z80_WR)
                {
                    if (address >= 0xf800) ram[address & 0x7ff] = Z80_GET_DATA(pins);
                    else if (address >= 0xf000) pcm_ram[address & 255] = Z80_GET_DATA(pins);
                }
            }
            else if (pins & Z80_IORQ)
            {
                const unsigned port = address & 0xff;
                if (pins & Z80_M1) { Z80_SET_DATA(pins, 0xff); }
                else if (pins & Z80_RD)
                {
                    uint8_t data = 0xff;
                    if (port < 0x40) data = ym.read_status();
                    else if (port < 0x80)
                    {
                        data = latch; pending = false; command_gap = 32;
                    }
                    Z80_SET_DATA(pins, data);
                }
                else if ((pins & Z80_WR) && port < 0x40)
                {
                    if (port & 1) ym.write_reg(ym_address, Z80_GET_DATA(pins));
                    else ym_address = Z80_GET_DATA(pins);
                }
            }
        }
        ym.stream_update(); pcm.stream_update();
        for (unsigned i = 0; i < samples.size(); ++i)
        {
            // Board FM channels are swapped; PCM channels retain their orientation.
            const int mixed = ym.get_buffer()[i ^ 1] / 2 + pcm.get_buffer()[i] / 3;
            samples[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
        }
        sample_position = 0;
    }
};

ArcadeAudio::ArcadeAudio(Roms& roms) : state(std::make_unique<State>(roms)) {}
ArcadeAudio::~ArcadeAudio() = default;
void ArcadeAudio::command(uint8_t value)
{
    if (state->commands.size() < 256) state->commands.push_back(value);
}
std::vector<int16_t> ArcadeAudio::render(unsigned stereo_frames)
{
    std::vector<int16_t> result(stereo_frames * 2);
    for (auto& sample : result)
    {
        if (state->sample_position == state->samples.size()) state->block();
        sample = state->samples[state->sample_position++];
    }
    return result;
}

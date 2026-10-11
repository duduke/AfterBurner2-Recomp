#pragma once

#include "engine/xboard_game.hpp"
#include <stdexcept>
#include <utility>

// Explicit debug startup only. These directed operands let the original ROM
// stage-advance event initialize a selected slot; they are not natural play.
namespace NativeStageSkip {
using Host = XBoardGame::Host;
inline uint16_t peek16(const Host& host, uint32_t address) {
    const auto offset = MainBus::memory_offset(address);
    if (offset == MainBus::UNMAPPED) throw std::runtime_error("Unmapped stage-skip observation");
    return (uint16_t(host.memory.bytes.at(offset)) << 8) | host.memory.bytes.at(offset + 1);
}
inline uint32_t peek32(const Host& host, uint32_t address) {
    return (uint32_t(peek16(host, address)) << 16) | peek16(host, address + 2);
}
inline uint16_t rom16(const Roms& rom, uint32_t address) {
    if (address + 1 >= rom.maincpu.length) throw std::runtime_error("Stage-skip ROM address out of range");
    return (uint16_t(rom.maincpu.rom[address]) << 8) | rom.maincpu.rom[address + 1];
}
inline uint32_t rom32(const Roms& rom, uint32_t address) {
    return (uint32_t(rom16(rom, address)) << 16) | rom16(rom, address + 2);
}
inline unsigned stage_count(const Roms& rom) {
    const unsigned count = rom16(rom, 0x32480);
    if (!count || count > 64) throw std::runtime_error("Invalid runtime ROM stage count");
    return count;
}
inline void seed16(Host& host, uint32_t address, uint16_t value) {
    const auto offset = MainBus::memory_offset(address);
    if (offset == MainBus::UNMAPPED) throw std::runtime_error("Unmapped stage-skip operand");
    host.memory.bytes.at(offset) = value >> 8;
    host.memory.bytes.at(offset + 1) = value;
}
inline void seed32(Host& host, uint32_t address, uint32_t value) {
    seed16(host, address, value >> 16); seed16(host, address + 2, value);
}
template<class Tick>
void wait_for_flight(Host& host, Tick tick) {
    for (unsigned f = 0; f < 1800 && peek16(host, 0x29c09c) != 16; ++f) tick();
    if (peek16(host, 0x29c09c) != 16)
        throw std::runtime_error("Active flight not reached after cold start");
}
template<class Tick>
void enter(Host& host, const Roms& rom, unsigned stage, Tick tick) {
    if (!stage || stage > stage_count(rom)) throw std::runtime_error("Stage number out of range");
    wait_for_flight(host, tick);
    if (peek16(host, 0x29c060) != 0) throw std::runtime_error("Stage skip requires fresh first-stage startup");
    if (stage == 1) return;
    const unsigned index = stage - 1;
    // $13DBA/$13E06 resolve the ordered selector through $3249A;
    // $142D0 reads the descriptor's event program at byte offset 16.
    const auto selector_offset = MainBus::memory_offset(0x29c010 + index - 1);
    const auto selector = host.memory.bytes.at(selector_offset);
    const auto descriptor = rom32(rom, 0x3249a + uint32_t(selector) * 4);
    uint32_t event = rom32(rom, descriptor + 16), transition = 0, alternate = 0;
    // $DE68..$DE8C: eight-byte records; command 12 at $DEC0 -> $DFC4.
    for (unsigned n = 0; n < 8192 && event + 8 <= rom.maincpu.length; ++n, event += 8) {
        if (rom.maincpu.rom[event + 2] == 18) alternate = event;
        if (rom.maincpu.rom[event + 2] == 12) { transition = event; break; }
        if (rom16(rom, event) == 0xffff) break;
    }
    if (!transition) throw std::runtime_error("No ROM stage-advance event found");
    seed16(host, 0x29c060, index - 1); seed16(host, 0xffff8d30, 4 * (index - 1));
    if (alternate) {
        // Command 18 ($E086) prepares the next stage's alternate road colors
        // through $13DF0/$142DC. Let its original IRQ palette blend finish;
        // jumping straight to command 12 leaves the canyon gradient black.
        seed32(host, 0xffff8a08, alternate);
        seed32(host, 0x29c006, uint32_t(rom16(rom, alternate)) << 4);
        bool blending = false, settled = false;
        for (unsigned f = 0; f < 600 && !settled; ++f) {
            tick();
            const auto flags = host.memory.bytes.at(MainBus::memory_offset(0x29c54c));
            blending |= (flags & 2) != 0;
            settled = blending && !(flags & 2);
        }
        if (!settled) throw std::runtime_error("Stage-skip road palette did not settle");
    }
    seed32(host, 0xffff8a08, transition);
    seed32(host, 0x29c006, uint32_t(rom16(rom, transition)) << 4);
    bool initialized = false;
    struct RestorePhase {
        Host& host;
        decltype(host.phase) previous;
        ~RestorePhase() { host.phase = std::move(previous); }
    } restore{host, std::move(host.phase)};
    host.phase = [&](const Host::Phase& phase) {
        if (restore.previous) restore.previous(phase);
        if (phase.cpu == 0 && phase.entry && phase.entry->state.pc == 0x13ce8
            && peek16(host, 0x29c060) == index) initialized = true;
    };
    for (unsigned f = 0; f < 180 && !initialized; ++f) tick();
    if (!initialized || peek16(host, 0x29c060) != index)
        throw std::runtime_error("Directed ROM stage setup did not complete");
}
}

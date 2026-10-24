#pragma once

#include "engine/mainbus.hpp"
#include "engine/stage_catalog.hpp"
#include <algorithm>
#include <array>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

// Optional run policy. The literal CPUs keep their original progress counters,
// lives, score and position-based branches; this owned data view moves complete
// stage content between those positions. No canonical ROM region is modified.
namespace Shuffle {
struct Plan {
    uint32_t seed{};
    std::vector<unsigned> stages;
};

inline void validate(const Plan& plan, unsigned count) {
    if (!count || count > 64 || plan.stages.size() != count)
        throw std::runtime_error("Invalid Shuffle stage count");
    std::vector<bool> seen(count);
    for (const auto stage : plan.stages) {
        if (!stage || stage > count || seen[stage - 1])
            throw std::runtime_error("Shuffle requires each stage exactly once");
        seen[stage - 1] = true;
    }
    // The final ROM slot is the ending aircraft sequence, whose own callback
    // enters the ending independently of the ordinary stage-count check.
    if (plan.stages.back() != count)
        throw std::runtime_error("Shuffle keeps the final stage last");
}

inline Plan generate(unsigned count, uint32_t seed) {
    if (!count || count > 64) throw std::runtime_error("Invalid Shuffle stage count");
    Plan result{seed, std::vector<unsigned>(count)};
    std::iota(result.stages.begin(), result.stages.end(), 1u);
    std::mt19937 random(seed);
    // Explicit rejection sampling makes the seeded order independent of the
    // standard library's distribution/shuffle implementation.
    for (unsigned size = count - 1; size > 1; --size) {
        const uint32_t threshold = uint32_t(0u - size) % size;
        uint32_t value;
        do { value = uint32_t(random()); } while (value < threshold);
        std::swap(result.stages[size - 1], result.stages[value % size]);
    }
    return result;
}

namespace Detail {
inline void write16(Roms& rom, uint32_t address, uint16_t value) {
    StageCatalog::Detail::range(rom, address, 2);
    rom.maincpu.rom[address] = uint8_t(value >> 8);
    rom.maincpu.rom[address + 1] = uint8_t(value);
}
inline void write32(Roms& rom, uint32_t address, uint32_t value) {
    write16(rom, address, uint16_t(value >> 16));
    write16(rom, address + 2, uint16_t(value));
}
inline void clone(const RomLoader& source, RomLoader& destination, uint32_t length = 0) {
    if (source.length && !source.rom) throw std::runtime_error("Missing Shuffle ROM region");
    if (length && source.length > length) throw std::runtime_error("Oversized Shuffle ROM region");
    const auto capacity = length ? length : source.length;
    if (capacity) destination.init(capacity);
    if (source.length) std::copy_n(source.rom, source.length, destination.rom);
    destination.loaded = source.loaded;
}
}

class RomView {
public:
    explicit RomView(const Roms& canonical, const Plan& plan) {
        const unsigned count = StageCatalog::count(canonical);
        validate(plan, count);
        if (canonical.maincpu.length > 0x40000)
            throw std::runtime_error("Shuffle requires an unmodified canonical main ROM");
        Detail::clone(canonical.maincpu, owned.maincpu, 0x80000);
        for (auto member : {&Roms::subcpu, &Roms::tiles, &Roms::sprites,
                            &Roms::road, &Roms::soundcpu, &Roms::pcm})
            Detail::clone(canonical.*member, owned.*member);

        // $13C92/$139D4/$13BFA select stage setup/road words. DIP banks use
        // the ROM's original $40-byte stride ($139F4..$13A04).
        for (const uint32_t base : {0x32196u, 0x32324u, 0x32452u, 0x30d54u})
            reorder(canonical, plan, base, 2);
        for (const uint32_t base : {0x32352u, 0x32224u})
            for (unsigned bank = 0; bank < 4; ++bank)
                reorder(canonical, plan, base + bank * 0x40, 2);
        // $DF54: music. $80 dispatches to the sound ROM's $0C91 RET;
        // its song comes from an earlier original stage, not this row. Carry
        // that original history into a new adjacency (including cold entry),
        // while leaving continuous same-song stages and explicit controls alone.
        uint8_t previous_music = 0x80;
        for (unsigned position = 0; position < count; ++position) {
            const auto source = plan.stages[position] - 1;
            auto inherited = source;
            while (inherited && canonical.maincpu.rom[0x2b604 + inherited] == 0x80)
                --inherited;
            const auto music = canonical.maincpu.rom[0x2b604 + inherited];
            auto command = canonical.maincpu.rom[0x2b604 + source];
            if (command == 0x80 && (!position || music != previous_music)) command = music;
            owned.maincpu.rom[0x2b604 + position] = command;
            previous_music = music;
        }
        // $D320: eight-byte stage text.
        reorder(canonical, plan, 0x2b6ac, 8);
        reorder(canonical, plan, 0x2ea16, 32);

        // Fixed cold-start sources alias the first original stage's palette
        // blocks. Clone every indexed palette before replacing those sources;
        // later positions may still need the original first-stage colors.
        uint32_t cursor = 0x40000;
        for (const auto [base, bytes] : std::array<std::pair<uint32_t, unsigned>, 5>{{
                 {0x140da,64}, {0x1413a,256}, {0x14196,256}, {0x141f2,256}, {0x1424e,256}}}) {
            for (unsigned position = 0; position < count; ++position) {
                const auto source = StageCatalog::Detail::pointer(canonical,
                    base + (plan.stages[position] - 1) * 4);
                Detail::write32(owned, base + position * 4,
                                append(canonical, source, bytes, cursor));
            }
        }
        const auto first = plan.stages.front() - 1;
        copy_startup(canonical, 0x2bc60,
            StageCatalog::Detail::pointer(canonical, 0x140da + first * 4) + 34, 28);
        copy_startup(canonical, 0x2cff4,
            StageCatalog::Detail::pointer(canonical, 0x1413a + first * 4), 256);
        copy_startup(canonical, 0x2dbf4,
            StageCatalog::Detail::pointer(canonical, 0x14196 + first * 4), 256);
        copy_startup(canonical, 0x2bf60,
            StageCatalog::Detail::pointer(canonical, 0x141f2 + first * 4) + 7 * 32 + 2, 28);

        std::array<bool, 256> selectors{};
        alternate.resize(count);
        for (unsigned position = 0; position < count; ++position) {
            const auto source = StageCatalog::get(canonical, plan.stages[position]);
            // Discover alternate-entry requirements from the actual original
            // predecessor's command18, rather than naming particular stages.
            if (plan.stages[position] > 1) {
                auto previous = StageCatalog::get(canonical, plan.stages[position] - 1).events;
                for (unsigned record = 0; record < 8192; ++record, previous += 8) {
                    StageCatalog::Detail::range(canonical, previous, 8);
                    const auto command = canonical.maincpu.rom[previous + 2];
                    if (StageCatalog::Detail::word(canonical, previous) == 0xffff || command == 12) break;
                    alternate[position] = alternate[position] || command == 18;
                }
            }
            if (selectors[source.selector])
                throw std::runtime_error("Shuffle stage selectors must be unique");
            selectors[source.selector] = true;
            const auto descriptor = append(canonical, source.descriptor, 20, cursor);
            const auto events = cursor;
            uint32_t event = source.events;
            bool terminal = false;
            for (unsigned record = 0; record < 8192; ++record, event += 8) {
                StageCatalog::Detail::range(canonical, event, 8);
                if (StageCatalog::Detail::word(canonical, event) == 0xffff) break;
                const auto command = canonical.maincpu.rom[event + 2];
                // $E086 prepares the original next stage's alternate palette.
                // Shuffle installs the destination's settled colors at entry
                // instead, so old adjacency records must not execute.
                if (command != 18) append(canonical, event, 8, cursor);
                if (command == 12) {
                    if (StageCatalog::Detail::word(canonical, event + 8) != 0xffff)
                        throw std::runtime_error("Shuffle terminal event lacks its sentinel");
                    append(canonical, event + 8, 8, cursor);
                    terminal = true;
                    break;
                }
            }
            if (!terminal) throw std::runtime_error("Shuffle stage has no terminal event");
            owned.maincpu.rom[0x32482 + position] = source.selector;
            Detail::write32(owned, 0x3249a + uint32_t(source.selector) * 4, descriptor);
            Detail::write32(owned, descriptor + 16, events);
        }
    }
    RomView(const RomView&) = delete;
    RomView& operator=(const RomView&) = delete;
    RomView(RomView&&) = delete;
    RomView& operator=(RomView&&) = delete;
    Roms& rom() { return owned; }
    const Roms& rom() const { return owned; }
    bool needs_alternate_palette(unsigned position) const {
        if (position >= alternate.size()) throw std::out_of_range("Shuffle position out of range");
        return alternate[position];
    }

private:
    Roms owned;
    std::vector<bool> alternate;
    void copy_startup(const Roms& canonical, uint32_t destination, uint32_t source, unsigned bytes) {
        StageCatalog::Detail::range(canonical, source, bytes);
        StageCatalog::Detail::range(owned, destination, bytes);
        std::copy_n(canonical.maincpu.rom + source, bytes, owned.maincpu.rom + destination);
    }
    void reorder(const Roms& canonical, const Plan& plan, uint32_t base, unsigned stride) {
        StageCatalog::Detail::range(canonical, base, unsigned(plan.stages.size()) * stride);
        for (unsigned position = 0; position < plan.stages.size(); ++position)
            std::copy_n(canonical.maincpu.rom + base + (plan.stages[position] - 1) * stride,
                        stride, owned.maincpu.rom + base + position * stride);
    }
    uint32_t append(const Roms& canonical, uint32_t address, unsigned bytes, uint32_t& cursor) {
        StageCatalog::Detail::range(canonical, address, bytes);
        StageCatalog::Detail::range(owned, cursor, bytes);
        const auto result = cursor;
        std::copy_n(canonical.maincpu.rom + address, bytes, owned.maincpu.rom + cursor);
        cursor += bytes;
        return result;
    }
};

// Enhanced transition policy, called at the original $13CE8 initialization
// boundary. $13DF0 supplies the destination's auxiliary colors, and the
// $142DC blend settles to the second fourteen-word gradient at source+34.
// Installing those settled values preserves the departing stage's colors and
// does not add hidden CPU frames or modify score, lives or run progression.
inline void prepare_alternate_palette(MainBus& bus, const Roms& active, unsigned position) {
    const auto stage = StageCatalog::get(active, position + 1);
    const auto alternate = StageCatalog::Detail::pointer(active, stage.descriptor + 4);
    const auto gradient = StageCatalog::Detail::pointer(active, 0x140da + position * 4);
    StageCatalog::Detail::range(active, alternate, 24);
    StageCatalog::Detail::range(active, gradient, 62);
    bus.write16(0x122f00, StageCatalog::Detail::word(active, alternate));
    bus.write32(0x122e60, StageCatalog::Detail::pointer(active, alternate + 4));
    for (unsigned offset = 8; offset <= 20; offset += 4)
        bus.write32(0x122e10 + offset - 8, StageCatalog::Detail::pointer(active, alternate + offset));
    for (unsigned word = 0; word < 14; ++word)
        bus.write16(0x122f02 + word * 2,
                    StageCatalog::Detail::word(active, gradient + 34 + word * 2));
}
}

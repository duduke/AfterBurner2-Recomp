#include "engine/shuffle_rom.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid Shuffle input was accepted");
}
constexpr auto regions = std::array{&Roms::maincpu, &Roms::subcpu, &Roms::tiles,
    &Roms::sprites, &Roms::road, &Roms::soundcpu, &Roms::pcm};
constexpr std::array<std::pair<uint32_t, unsigned>, 12> tables{{
    {0x32196,2}, {0x32324,2}, {0x32452,2}, {0x30d54,2}, {0x2b604,1},
    {0x2b6ac,8}, {0x140da,4}, {0x1413a,4}, {0x14196,4}, {0x141f2,4},
    {0x1424e,4}, {0x2ea16,32}
}};
constexpr std::array<std::pair<uint32_t, unsigned>, 5> palettes{{
    {0x140da,64}, {0x1413a,256}, {0x14196,256}, {0x141f2,256}, {0x1424e,256}
}};

void synthetic(Roms& rom) {
    // Generated marker records exercise ownership and bounds without ROM data.
    rom.maincpu.init(0x40000);
    for (unsigned i = 1; i < regions.size(); ++i) {
        auto& region = rom.*regions[i];
        region.init(32 + i);
        for (unsigned byte = 0; byte < region.length; ++byte)
            region.rom[byte] = uint8_t(i * 17 + byte);
        region.loaded = i & 1;
    }
    auto fill = [&](uint32_t address, unsigned stride, unsigned bank = 0) {
        for (unsigned stage = 0; stage < 23; ++stage)
            for (unsigned byte = 0; byte < stride; ++byte)
                rom.maincpu.rom[address + stage * stride + byte] =
                    uint8_t(stage * 7 + bank * 29 + byte * 3);
    };
    for (const auto [address, stride] : tables) fill(address, stride);
    for (const uint32_t base : {0x32352u, 0x32224u})
        for (unsigned bank = 0; bank < 4; ++bank) fill(base + bank * 0x40, 2, bank);
    Shuffle::Detail::write16(rom, 0x32480, 23);
    uint32_t palette_cursor = 0x18000;
    for (const auto [base, size] : palettes)
        for (unsigned stage = 0; stage < 23; ++stage) {
            Shuffle::Detail::write32(rom, base + stage * 4, palette_cursor);
            for (unsigned byte = 0; byte < size; ++byte)
                rom.maincpu.rom[palette_cursor + byte] = uint8_t(stage * 13 + byte * 7);
            palette_cursor += size;
        }
    for (unsigned stage = 0; stage < 23; ++stage) {
        const auto descriptor = 0x35000 + stage * 20;
        const auto events = 0x36000 + stage * 32;
        const auto alternate = 0x37000 + stage * 24;
        rom.maincpu.rom[0x32482 + stage] = uint8_t(stage);
        for (unsigned byte = 0; byte < 20; ++byte)
            rom.maincpu.rom[descriptor + byte] = uint8_t(stage * 11 + byte);
        Shuffle::Detail::write32(rom, 0x3249a + stage * 4, descriptor);
        Shuffle::Detail::write32(rom, descriptor + 4, alternate);
        Shuffle::Detail::write32(rom, descriptor + 16, events);
        for (unsigned byte = 0; byte < 24; ++byte)
            rom.maincpu.rom[alternate + byte] = uint8_t(stage * 5 + byte * 3);
        for (unsigned record = 0; record < 4; ++record) {
            for (unsigned byte = 0; byte < 8; ++byte)
                rom.maincpu.rom[events + record * 8 + byte] = uint8_t(stage + record + byte);
            Shuffle::Detail::write16(rom, events + record * 8, uint16_t(stage * 16 + record));
        }
        rom.maincpu.rom[events + 2] = 2;
        rom.maincpu.rom[events + 10] = stage & 1 ? 18 : 3;
        rom.maincpu.rom[events + 18] = 12;
        Shuffle::Detail::write16(rom, events + 24, 0xffff);
    }
    rom.maincpu.loaded = true;
}

void compare_tables(const Roms& canonical, const Roms& active, const Shuffle::Plan& plan) {
    auto compare = [&](uint32_t base, unsigned stride) {
        for (unsigned position = 0; position < plan.stages.size(); ++position)
            require(!std::memcmp(active.maincpu.rom + base + position * stride,
                        canonical.maincpu.rom + base + (plan.stages[position] - 1) * stride, stride),
                    "Shuffle ordinal table differs from canonical source row");
    };
    for (const auto [base, stride] : tables)
        if (stride != 4 && base != 0x2b604) compare(base, stride);
    // Continue commands depend on the original predecessor. Resolve that
    // history independently, then check the music requested in shuffled order.
    std::vector<uint8_t> inherited(plan.stages.size());
    uint8_t command = 0x80;
    for (unsigned stage = 0; stage < inherited.size(); ++stage) {
        const auto next = canonical.maincpu.rom[0x2b604 + stage];
        if (next != 0x80) command = next;
        inherited[stage] = command;
    }
    for (unsigned position = 0; position < plan.stages.size(); ++position) {
        const auto source = plan.stages[position] - 1;
        auto expected = canonical.maincpu.rom[0x2b604 + source];
        if (expected == 0x80 && (!position ||
                inherited[source] != inherited[plan.stages[position - 1] - 1]))
            expected = inherited[source];
        require(active.maincpu.rom[0x2b604 + position] == expected,
                "Shuffle lost the source stage's inherited music command");
    }
    for (const uint32_t base : {0x32352u, 0x32224u})
        for (unsigned bank = 0; bank < 4; ++bank) compare(base + bank * 0x40, 2);
    for (const auto [base, bytes] : palettes)
        for (unsigned position = 0; position < plan.stages.size(); ++position) {
            const auto moved = StageCatalog::Detail::pointer(active, base + position * 4);
            const auto original = StageCatalog::Detail::pointer(canonical,
                base + (plan.stages[position] - 1) * 4);
            require(moved >= 0x40000 && !std::memcmp(active.maincpu.rom + moved,
                        canonical.maincpu.rom + original, bytes),
                    "Shuffle palette pointer does not name an isolated original record");
        }
    const auto first = plan.stages.front() - 1;
    auto startup = [&](uint32_t destination, uint32_t source, unsigned bytes) {
        require(!std::memcmp(active.maincpu.rom + destination,
                    canonical.maincpu.rom + source, bytes), "Shuffle cold palette source differs from first stage");
    };
    startup(0x2bc60, StageCatalog::Detail::pointer(canonical, 0x140da + first * 4) + 34, 28);
    startup(0x2cff4, StageCatalog::Detail::pointer(canonical, 0x1413a + first * 4), 256);
    startup(0x2dbf4, StageCatalog::Detail::pointer(canonical, 0x14196 + first * 4), 256);
    startup(0x2bf60, StageCatalog::Detail::pointer(canonical, 0x141f2 + first * 4) + 7 * 32 + 2, 28);
}

void compare_events(const Roms& canonical, const Roms& active, const Shuffle::Plan& plan) {
    std::vector<uint32_t> descriptors;
    for (unsigned position = 0; position < plan.stages.size(); ++position) {
        const auto original = StageCatalog::get(canonical, plan.stages[position]);
        const auto moved = StageCatalog::get(active, position + 1);
        require(moved.selector == original.selector && moved.descriptor >= 0x40000
                && moved.events >= 0x40000, "Shuffle did not install an owned descriptor/event list");
        require(!std::memcmp(canonical.maincpu.rom + original.descriptor,
                    active.maincpu.rom + moved.descriptor, 16), "Shuffle changed descriptor content");
        require(std::find(descriptors.begin(), descriptors.end(), moved.descriptor) == descriptors.end(),
                "Shuffle descriptors alias each other");
        descriptors.push_back(moved.descriptor);
        uint32_t source = original.events, destination = moved.events;
        for (;;) {
            const auto terminal = StageCatalog::Detail::word(canonical, source) == 0xffff;
            const auto command = canonical.maincpu.rom[source + 2];
            if (terminal || command != 18) {
                require(!std::memcmp(canonical.maincpu.rom + source,
                            active.maincpu.rom + destination, 8),
                        "Shuffle changed a retained event or its timestamp");
                if (!terminal) require(active.maincpu.rom[destination + 2] != 18,
                                        "Shuffle retained old-adjacency palette preparation");
                destination += 8;
            }
            if (terminal) break;
            source += 8;
        }
    }
}

void compare_palette(const Roms& canonical, Roms& active, const Shuffle::Plan& plan) {
    MainBus::Memory memory;
    MainBus bus(active.maincpu.rom, active.maincpu.length,
                active.subcpu.rom, active.subcpu.length, memory);
    for (unsigned position = 0; position < plan.stages.size(); ++position) {
        memory.bytes.fill(0xa5);
        auto expected = memory.bytes;
        std::array<uint8_t, MainBus::MEMORY_BYTES> writes{}, expected_writes{};
        MainBus::Trace trace{nullptr, writes.data()}; bus.trace = &trace;
        const auto original = StageCatalog::get(canonical, plan.stages[position]);
        const auto alternate = StageCatalog::Detail::pointer(canonical, original.descriptor + 4);
        const auto gradient = StageCatalog::Detail::pointer(canonical, 0x140da + (plan.stages[position] - 1) * 4);
        auto copy = [&](uint32_t destination, uint32_t source, unsigned bytes) {
            const auto offset = MainBus::memory_offset(destination);
            std::copy_n(canonical.maincpu.rom + source, bytes, expected.begin() + offset);
            std::fill_n(expected_writes.begin() + offset, bytes, 1);
        };
        copy(0x122f00, alternate, 2);
        copy(0x122e60, alternate + 4, 4);
        copy(0x122e10, alternate + 8, 16);
        copy(0x122f02, gradient + 34, 28);
        Shuffle::prepare_alternate_palette(bus, active, position);
        require(memory.bytes == expected && writes == expected_writes,
                "Shuffle palette changed other state or used the wrong destination data");
    }
    bus.trace = nullptr;
    const auto before = memory.bytes;
    rejects([&] { Shuffle::prepare_alternate_palette(bus, active, unsigned(plan.stages.size())); });
    rejects([&] { Shuffle::prepare_alternate_palette(bus, active, std::numeric_limits<unsigned>::max()); });
    require(memory.bytes == before, "Invalid Shuffle palette position changed board state");
    const auto descriptor = StageCatalog::get(active, 1).descriptor;
    const auto alternate = StageCatalog::Detail::pointer(active, descriptor + 4);
    Shuffle::Detail::write32(active, descriptor + 4, active.maincpu.length - 2);
    rejects([&] { Shuffle::prepare_alternate_palette(bus, active, 0); });
    Shuffle::Detail::write32(active, descriptor + 4, alternate);
    const auto gradient = StageCatalog::Detail::pointer(active, 0x140da);
    Shuffle::Detail::write32(active, 0x140da, active.maincpu.length - 2);
    rejects([&] { Shuffle::prepare_alternate_palette(bus, active, 0); });
    Shuffle::Detail::write32(active, 0x140da, gradient);
    require(memory.bytes == before, "Invalid Shuffle palette pointer partially changed board state");
}

void verify(Roms& canonical) {
    const auto count = StageCatalog::count(canonical);
    const auto plan = Shuffle::generate(count, 0x51a9u);
    std::array<std::vector<uint8_t>, regions.size()> snapshots;
    for (unsigned i = 0; i < regions.size(); ++i) {
        const auto& region = canonical.*regions[i];
        snapshots[i].assign(region.rom, region.rom + region.length);
    }
    Shuffle::RomView view(canonical, plan);
    const auto& constant_view = view;
    require(&constant_view.rom() == &view.rom() && view.rom().maincpu.length == 0x80000,
            "Shuffle ROM lifetime or mapped extent changed");
    compare_tables(canonical, view.rom(), plan);
    compare_events(canonical, view.rom(), plan);
    compare_palette(canonical, view.rom(), plan);
    std::vector<bool> changed(canonical.maincpu.length);
    auto allow = [&](uint32_t base, unsigned bytes) {
        std::fill_n(changed.begin() + base, bytes, true);
    };
    for (const auto [base, stride] : tables) allow(base, count * stride);
    for (const uint32_t base : {0x32352u, 0x32224u})
        for (unsigned bank = 0; bank < 4; ++bank) allow(base + bank * 0x40, count * 2);
    allow(0x32482, count);
    for (unsigned number = 1; number <= count; ++number)
        allow(0x3249a + StageCatalog::get(canonical, number).selector * 4, 4);
    allow(0x2bc60, 28); allow(0x2cff4, 256); allow(0x2dbf4, 256); allow(0x2bf60, 28);
    for (unsigned byte = 0; byte < canonical.maincpu.length; ++byte)
        require(changed[byte] || canonical.maincpu.rom[byte] == view.rom().maincpu.rom[byte],
                "Shuffle changed code or data outside the declared source tables");
    for (unsigned position = 0; position < count; ++position) {
        bool needed = false;
        if (plan.stages[position] > 1) {
            auto event = StageCatalog::get(canonical, plan.stages[position] - 1).events;
            while (StageCatalog::Detail::word(canonical, event) != 0xffff
                    && canonical.maincpu.rom[event + 2] != 12) {
                needed |= canonical.maincpu.rom[event + 2] == 18;
                event += 8;
            }
        }
        require(view.needs_alternate_palette(position) == needed,
                "Shuffle lost the original predecessor's alternate-palette requirement");
    }
    rejects([&] { view.needs_alternate_palette(count); });
    for (unsigned i = 0; i < regions.size(); ++i) {
        auto& copy = view.rom().*regions[i];
        const auto& source = canonical.*regions[i];
        require(copy.rom != source.rom && copy.loaded == source.loaded,
                "Shuffle did not deep-clone a ROM region");
        if (i) require(copy.length == source.length && !std::memcmp(copy.rom, source.rom, source.length),
                       "Shuffle changed a non-main ROM region");
        copy.rom[0] ^= 0xff;
        copy.rom[copy.length - 1] ^= 0xff;
        require(!std::memcmp(source.rom, snapshots[i].data(), source.length),
                "Shuffle mutated the canonical ROM");
    }
    // An identity permutation still owns its data and removes adjacency events.
    Shuffle::Plan identity{0, std::vector<unsigned>(count)};
    std::iota(identity.stages.begin(), identity.stages.end(), 1u);
    Shuffle::RomView same(canonical, identity);
    compare_tables(canonical, same.rom(), identity);
    compare_events(canonical, same.rom(), identity);
}

void captures(const Roms& canonical) {
    unsigned identical = 0;
    for (const unsigned stage : {5u, 8u, 13u, 17u}) {
        const auto path = "build/oracle/native_stage" + std::to_string(stage) + "_palette.bin";
        std::ifstream in(path, std::ios::binary);
        char header[8];
        require(bool(in.read(header, 8)) && std::string_view(header, 8) == "AB2NSP01",
                "Missing or invalid retained MAME alternate-palette capture");
        auto word = [&] {
            const auto high = in.get(), low = in.get();
            require(high >= 0 && low >= 0, "Truncated retained MAME palette capture");
            return uint16_t(unsigned(high) * 256 + unsigned(low));
        };
        require(word() == stage, "MAME palette capture has wrong stage");
        const auto source = StageCatalog::Detail::pointer(canonical, 0x140da + (stage - 1) * 4) + 34;
        for (unsigned index = 0; index < 14; ++index) {
            require(word() == StageCatalog::Detail::word(canonical, source + index * 2),
                    "Shuffle settled gradient differs from independently executing MAME");
            ++identical;
        }
        require(in.peek() == std::char_traits<char>::eof(), "Trailing MAME palette capture data");
    }
    std::cout << "Shuffle palette provenance: " << identical << "/56 retained MAME gradient words identical, 0 skipped\n";
}
}

int main(int argc, char** argv) try {
    require(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--roms"),
            "Usage: shuffle_rom_regression [--roms]");
    for (unsigned count = 1; count <= 64; ++count)
        for (const auto seed : {0u, 1u, 0x51a9u, std::numeric_limits<uint32_t>::max()}) {
            const auto plan = Shuffle::generate(count, seed);
            Shuffle::validate(plan, count);
            require(plan.stages.back() == count,
                    "Shuffle moved the final ending sequence into gameplay");
            auto gameplay = std::vector<unsigned>(plan.stages.begin(), plan.stages.end() - 1);
            std::sort(gameplay.begin(), gameplay.end());
            for (unsigned position = 0; position < gameplay.size(); ++position)
                require(gameplay[position] == position + 1,
                        "Shuffle gameplay prefix omitted or repeated a stage");
            require(plan.seed == seed && plan.stages == Shuffle::generate(count, seed).stages,
                    "Shuffle seed did not reproduce its complete permutation");
        }
    require(Shuffle::generate(23, 1).stages != Shuffle::generate(23, 2).stages,
            "Different Shuffle seeds produced the same order");
    // Seeded output is a replay contract, independent of library shuffle APIs.
    const std::vector<unsigned> known_order{
        21,12,8,22,9,7,20,5,3,18,11,4,15,16,14,17,19,6,2,10,1,13,23};
    require(Shuffle::generate(23, 0x51a9u).stages == known_order,
            "Shuffle seeded gameplay order changed");
    require(Shuffle::generate(1, 0).stages == std::vector<unsigned>{1},
            "A single ending stage cannot be shuffled");
    rejects([] { Shuffle::generate(0, 0); });
    rejects([] { Shuffle::generate(65, 0); });
    rejects([] { Shuffle::validate({0, {1,1}}, 2); });
    rejects([] { Shuffle::validate({0, {0,2}}, 2); });
    rejects([] { Shuffle::validate({0, {1,3}}, 2); });
    rejects([] { Shuffle::validate({0, {1}}, 2); });
    rejects([] { Shuffle::validate({0, {2,1}}, 2); });
    rejects([] { Shuffle::validate({0, {}}, 0); });
    Roms artificial; synthetic(artificial); verify(artificial);
    // Multiple inherited rows, song changes, and explicit controls must work
    // even when a continue-only stage becomes the very first stage.
    for (unsigned stage = 0; stage < 23; ++stage)
        artificial.maincpu.rom[0x2b604 + stage] = stage % 4 ? 0x80 : uint8_t(0x91 + stage % 6);
    artificial.maincpu.rom[0x2b604 + 8] = 0x84; // Synthetic explicit control marker.
    verify(artificial);
    auto inherited_first = Shuffle::generate(23, 0);
    const auto second = std::find(inherited_first.stages.begin(), inherited_first.stages.end(), 2u);
    std::iter_swap(inherited_first.stages.begin(), second);
    Shuffle::RomView inherited_view(artificial, inherited_first);
    compare_tables(artificial, inherited_view.rom(), inherited_first);
    require(inherited_view.rom().maincpu.rom[0x2b604] == artificial.maincpu.rom[0x2b604],
            "Continue-only opening stage did not start its original song");
    auto misplaced_ending = Shuffle::generate(23, 0x51a9u);
    std::swap(misplaced_ending.stages.front(), misplaced_ending.stages.back());
    const auto original_main = std::vector<uint8_t>(artificial.maincpu.rom,
                                                   artificial.maincpu.rom + artificial.maincpu.length);
    rejects([&] { Shuffle::RomView bad(artificial, misplaced_ending); });
    require(std::equal(original_main.begin(), original_main.end(), artificial.maincpu.rom),
            "Rejected nonfinal ending modified the canonical ROM");
    const auto first = StageCatalog::get(artificial, 1);
    const auto sentinel = StageCatalog::Detail::word(artificial, first.events + 24);
    Shuffle::Detail::write16(artificial, first.events + 24, 0);
    rejects([&] { Shuffle::RomView bad(artificial, Shuffle::generate(23, 0)); });
    Shuffle::Detail::write16(artificial, first.events + 24, sentinel);
    const auto event_pointer = StageCatalog::Detail::pointer(artificial, first.descriptor + 16);
    Shuffle::Detail::write32(artificial, first.descriptor + 16, 0x3ffff);
    rejects([&] { Shuffle::RomView bad(artificial, Shuffle::generate(23, 0)); });
    Shuffle::Detail::write32(artificial, first.descriptor + 16, event_pointer);
    Roms empty;
    rejects([&] { Shuffle::RomView bad(empty, Shuffle::generate(23, 0)); });
    std::cout << "Shuffle ROM-free policy: deterministic permutations, all owned regions, exact table/event data, palette write sets and malformed bounds pass\n";
    if (argc == 2) {
        require(roms.load_aburner2(), "ROMs required");
        verify(roms); captures(roms);
        std::cout << "Shuffle real-ROM transforms pass; this is directed data-policy evidence, not natural arcade equivalence\n";
    }
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

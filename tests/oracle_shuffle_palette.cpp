#include "engine/shuffle_rom.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
uint16_t word(std::ifstream& in) {
    unsigned char bytes[2];
    if (!in.read(reinterpret_cast<char*>(bytes), 2))
        throw std::runtime_error("Truncated Shuffle palette capture");
    return (uint16_t(bytes[0]) << 8) | bytes[1];
}

std::array<uint32_t, 39> addresses() {
    std::array<uint32_t, 39> result{};
    unsigned cursor = 0;
    result[cursor++] = 0x122f00;
    for (unsigned i = 0; i < 16; ++i) result[cursor++] = 0x122e60 + i * 2;
    for (unsigned i = 0; i < 8; ++i) result[cursor++] = 0x122e10 + i * 2;
    for (unsigned i = 0; i < 14; ++i) result[cursor++] = 0x122f02 + i * 2;
    return result;
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: oracle_shuffle_palette capture");
    std::ifstream in(argv[1], std::ios::binary);
    char header[8];
    if (!in.read(header, 8) || std::string_view(header, 8) != "AB2SPA01")
        throw std::runtime_error("Invalid Shuffle palette capture");
    const auto stage = word(in);
    std::array<uint16_t, 39> expected{};
    for (auto& value : expected) value = word(in);
    if (in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Trailing Shuffle palette capture data");
    if (!roms.load_aburner2()) throw std::runtime_error("ROMs required");
    if (!stage || stage > StageCatalog::count(roms))
        throw std::runtime_error("Invalid captured Shuffle stage");

    MainBus::Memory memory;
    memory.bytes.fill(0xa5);
    MainBus bus(roms.maincpu.rom, roms.maincpu.length,
                roms.subcpu.rom, roms.subcpu.length, memory);
    std::vector<uint8_t> written(MainBus::MEMORY_BYTES), expected_written(MainBus::MEMORY_BYTES);
    const auto destinations = addresses();
    for (unsigned i = 0; i < destinations.size(); ++i) {
        const auto offset = MainBus::memory_offset(destinations[i]);
        if (i >= 3 && i < 17) {
            // $14424 uploads only the first sky long. The other fourteen
            // observed sky words remain zero after this fresh MAME startup.
            // This test pattern is not a captured CPU or RAM replay seed.
            memory.bytes[offset] = memory.bytes[offset + 1] = 0;
        } else expected_written.at(offset) = expected_written.at(offset + 1) = 1;
    }
    const auto before = memory.bytes;
    unsigned writes = 0;
    bool outside_write = false;
    MainBus::Trace trace;
    trace.written = written.data();
    trace.access = [&](bool write, uint32_t address, uint16_t, uint16_t mask) {
        if (!write) return;
        ++writes;
        const auto offset = MainBus::memory_offset(address);
        outside_write |= offset == MainBus::UNMAPPED || mask != 0xffff;
        if (offset != MainBus::UNMAPPED)
            outside_write |= !expected_written.at(offset) || !expected_written.at(offset + 1);
    };
    bus.trace = &trace;
    // Exercise only the enhanced recipe against independently executed MAME
    // output. No CPU execution/state, first-stage board or captured RAM seed.
    Shuffle::prepare_alternate_palette(bus, roms, stage - 1);
    bus.trace = nullptr;

    unsigned differences = 0;
    for (unsigned i = 0; i < destinations.size(); ++i) {
        const auto offset = MainBus::memory_offset(destinations[i]);
        const auto actual = (uint16_t(memory.bytes[offset]) << 8) | memory.bytes[offset + 1];
        differences += actual != expected[i];
    }
    if (differences || outside_write || written != expected_written || writes != 25)
        throw std::runtime_error("Shuffle alternate palette differs from original MAME: stage "
            + std::to_string(stage) + ", " + std::to_string(differences)
            + " word differences or mismatched write set");
    for (size_t offset = 0; offset < memory.bytes.size(); ++offset)
        if (!expected_written[offset] && memory.bytes[offset] != before[offset])
            throw std::runtime_error("Shuffle alternate palette changed unrelated RAM");
    // Repeat with nonzero prior sky-tail contents to prove the helper preserves
    // previous palette history instead of clearing the uncopied fourteen words.
    for (unsigned i = 3; i < 17; ++i) {
        const auto offset = MainBus::memory_offset(destinations[i]);
        memory.bytes[offset] = memory.bytes[offset + 1] = 0x5a;
    }
    const auto prior = memory.bytes;
    Shuffle::prepare_alternate_palette(bus, roms, stage - 1);
    if (memory.bytes != prior)
        throw std::runtime_error("Shuffle alternate palette differs from original MAME write ownership: prior sky tail changed");
    std::cout << "Shuffle stage " << stage << " alternate palette: 39 of 39 original-MAME observed words identical, "
                 "0 skipped; exact 25-word write set, prior sky tail and unrelated RAM preserved.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

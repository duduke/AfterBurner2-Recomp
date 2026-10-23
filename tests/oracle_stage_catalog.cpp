#include "engine/stage_catalog.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
uint32_t read(std::ifstream& in, unsigned bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        const int byte = in.get();
        if (byte == std::char_traits<char>::eof())
            throw std::runtime_error("Truncated stage catalog capture");
        value = (value << 8) | unsigned(byte);
    }
    return value;
}
template<class Function>
void require_rejection(Function function) {
    try { function(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid stage catalog input was accepted");
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: oracle_stage_catalog capture");
    std::ifstream in(argv[1], std::ios::binary);
    char header[8];
    if (!in.read(header, 8) || std::string_view(header, 8) != "AB2SC001")
        throw std::runtime_error("Invalid stage catalog capture");
    const unsigned expected_count = read(in, 2);
    if (!expected_count || expected_count > 64)
        throw std::runtime_error("Invalid captured stage count");
    if (!roms.load_aburner2()) throw std::runtime_error("ROMs required");
    if (StageCatalog::count(roms) != expected_count)
        throw std::runtime_error("Stage catalog differs from original MAME: count");
    unsigned identical = 0;
    for (unsigned number = 1; number <= expected_count; ++number) {
        const unsigned expected_number = read(in, 2);
        const unsigned expected_selector = read(in, 1);
        const uint32_t expected_descriptor = read(in, 4), expected_events = read(in, 4);
        if (expected_number != number)
            throw std::runtime_error("Captured stage catalog ordinal is invalid");
        const auto actual = StageCatalog::get(roms, number);
        if (actual.number != expected_number || actual.selector != expected_selector
            || actual.descriptor != expected_descriptor || actual.events != expected_events)
            throw std::runtime_error("Stage catalog differs from original MAME at stage " + std::to_string(number));
        ++identical;
    }
    if (in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Trailing stage catalog capture data");
    require_rejection([&] { StageCatalog::get(roms, 0); });
    require_rejection([&] { StageCatalog::get(roms, expected_count + 1); });
    require_rejection([&] { StageCatalog::get(roms, std::numeric_limits<unsigned>::max()); });
    Roms empty;
    require_rejection([&] { StageCatalog::count(empty); });
    // Bounds failures use synthetic storage; no ROM bytes are embedded here.
    Roms truncated;
    truncated.maincpu.init(0x32481);
    require_rejection([&] { StageCatalog::count(truncated); });
    truncated.maincpu.init(0x32482);
    require_rejection([&] { StageCatalog::count(truncated); });
    truncated.maincpu.rom[0x32481] = 65;
    require_rejection([&] { StageCatalog::count(truncated); });
    truncated.maincpu.rom[0x32481] = 1;
    require_rejection([&] { StageCatalog::count(truncated); });
    truncated.maincpu.init(0x32483);
    truncated.maincpu.rom[0x32481] = 1;
    require_rejection([&] { StageCatalog::get(truncated, 1); });
    truncated.maincpu.init(0x32500);
    truncated.maincpu.rom[0x32481] = 1;
    for (unsigned i = 0; i < 4; ++i) truncated.maincpu.rom[0x3249a + i] = 0xff;
    require_rejection([&] { StageCatalog::get(truncated, 1); });
    for (unsigned i = 0; i < 4; ++i) {
        truncated.maincpu.rom[0x3249a + i] = 0;
        truncated.maincpu.rom[16 + i] = 0xff;
    }
    require_rejection([&] { StageCatalog::get(truncated, 1); });
    std::cout << "Stage catalog: " << identical << " of " << expected_count
              << " original-MAME entries identical, 0 skipped; invalid ordinals and ROM bounds rejected.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

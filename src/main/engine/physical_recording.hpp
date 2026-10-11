#pragma once

#include "engine/game_controls.hpp"
#include "roms.hpp"
#include <fstream>
#include <iomanip>
#include <string>

// Physical board-frame inputs only. A fresh file always starts at cold boot;
// pause admits no rows. The count is updated after each attempted frame so a
// stop/crash leaves a usable input prefix, including the failing frame.
class PhysicalRecording {
public:
    explicit PhysicalRecording(const std::string& path, const Roms& rom)
        : out(path, std::ios::out | std::ios::trunc) {
        if (!out) throw std::runtime_error("Cannot open physical recording");
        out << "AB2PHYS1\n0000000000 255 255 221\n";
        write_metadata(path + ".json",rom,{255,255,221});
    }
    static void write_metadata(const std::string& path, const Roms& rom,
                               const std::array<uint8_t,3>& switches) {
        std::ofstream metadata(path);
        if (!metadata) throw std::runtime_error("Cannot open recording metadata");
        metadata << "{\"format\":\"AB2PHYS1\",\"initial_persistent_state\":\"fresh cold board; no NVRAM/config import\","
                    "\"switches\":[" << unsigned(switches[0]) << ',' << unsigned(switches[1]) << ',' << unsigned(switches[2])
                 << "],\"rom_regions_fnv1a64\":{\n";
        const RomLoader* regions[]{&rom.maincpu,&rom.subcpu,&rom.tiles,&rom.sprites,&rom.road,&rom.soundcpu,&rom.pcm};
        const char* names[]{"maincpu","subcpu","tiles","sprites","road","soundcpu","pcm"};
        for (unsigned i=0; i<7; ++i) {
            uint64_t hash=14695981039346656037ULL;
            for (unsigned j=0; j<regions[i]->length; ++j) hash=(hash^regions[i]->rom[j])*1099511628211ULL;
            metadata << (i?",\n":"") << '"' << names[i] << "\":{\"bytes\":" << regions[i]->length
                     << ",\"hash\":\"" << std::hex << hash << std::dec << "\"}";
        }
        metadata << "\n}}\n";
        if (!metadata) throw std::runtime_error("Recording metadata write failed");
    }
    void append(uint64_t frame, const GameControls& controls) {
        if (frame != count) throw std::runtime_error("Recording requires contiguous cold-boot frames");
        const auto pins = arcade_inputs(controls);
        out << count << ' ' << unsigned(pins.buttons);
        for (auto a : pins.analog) out << ' ' << unsigned(a);
        out << '\n'; ++count;
        const auto end = out.tellp();
        out.seekp(9); out << std::setw(10) << std::setfill('0') << count;
        out.seekp(end); out.flush();
        if (!out) throw std::runtime_error("Physical recording write failed");
    }
private:
    std::ofstream out;
    uint64_t count{};
};

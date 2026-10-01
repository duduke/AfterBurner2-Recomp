// Differential test: the ported sub CPU road work (engine/osubroad) against
// the real arcade code running in MAME.
//
// Input is a capture from tools/oracle/capture_subroad.lua: per frame, the
// shared/road RAM and math-chip state at the start of the sub CPU's road
// work, the state at its end, and the exact set of bytes the sub CPU wrote
// in between. Each frame is replayed from its start state; the port must
// write exactly the same bytes with exactly the same values. Frames where
// the main CPU changed something the port read are skipped as races.
//
// Usage: oracle_subroad [capture-file]   (run from the repository root)

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include "roms.hpp"
#include "engine/osubroad.hpp"

namespace
{
    constexpr size_t SHARED = SubBus::SHARED_BYTES;
    constexpr size_t ROAD = SubBus::ROAD_BYTES;

    // The sub CPU's stacks occupy the top of subram1: user stack near
    // $0A3F80 (return addresses) and supervisor stack from $0A4000 (VBLANK
    // exception frames, pushed while the PC still points at the interrupted
    // instruction, so the capture can't filter them). The port has no stack.
    constexpr size_t STACK_START = 0x7f00;

    uint16_t r16(const uint8_t* m, size_t o) { return static_cast<uint16_t>((m[o] << 8) | m[o + 1]); }
    uint32_t r32(const uint8_t* m, size_t o) { return (static_cast<uint32_t>(r16(m, o)) << 16) | r16(m, o + 2); }

    bool read_offsets(std::ifstream& in, std::vector<uint8_t>& marks)
    {
        uint8_t n[4];
        if (!in.read(reinterpret_cast<char*>(n), 4)) return false;
        std::fill(marks.begin(), marks.end(), 0);
        const uint32_t count=r32(n,0);
        if (count>marks.size()) throw std::runtime_error("invalid write-set count");
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t o[2];
            if (!in.read(reinterpret_cast<char*>(o), 2)) return false;
            marks.at(r16(o, 0)) = 1;
        }
        return true;
    }

    // Compare one memory against the arcade: identical write sets, identical
    // written values. Returns the number of differing bytes, printing a few.
    unsigned compare(const char* name, unsigned record, uint32_t frame,
                     const std::vector<uint8_t>& arcade_written, const std::vector<uint8_t>& port_written,
                     const std::vector<uint8_t>& arcade, const std::vector<uint8_t>& port, unsigned& reported)
    {
        unsigned differences = 0;
        for (size_t i = 0; i < arcade.size(); ++i)
        {
            const bool written_differs = arcade_written[i] != port_written[i];
            const bool value_differs = arcade_written[i] && arcade[i] != port[i];
            if (!written_differs && !value_differs) continue;
            ++differences;
            if (reported++ < 16)
            {
                char line[200];
                std::snprintf(line, sizeof line, "record %u (frame %u): %s $%04zX: %s", record, frame, name, i,
                              written_differs ? (arcade_written[i] ? "arcade wrote it, port didn't"
                                                                   : "port wrote it, arcade didn't")
                                              : "");
                std::cerr << line;
                if (!written_differs)
                    std::fprintf(stderr, "arcade %02X, port %02X", arcade[i], port[i]);
                std::cerr << '\n';
            }
        }
        return differences;
    }
}

int main(int argc, char** argv) try
{
    const std::string path = argc > 1 ? argv[1] : "build/oracle/subroad.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::cerr << "No capture at " << path << " — run `make oracle-capture` first.\n";
        return 1;
    }
    char magic[8];
    in.read(magic, 8);
    const bool explicit_races=std::memcmp(magic,"AB2ORCL4",8)==0;
    const bool initializer = explicit_races || std::memcmp(magic,"AB2ORCL3",8)==0;
    if (!in || (!initializer && std::memcmp(magic, "AB2ORCL2", 8) != 0))
    {
        std::cerr << path << " is not a road capture\n";
        return 1;
    }
    if (!roms.load_aburner2())
        return 1;

    std::vector<uint8_t> start_shared(SHARED), start_road(ROAD), end_shared(SHARED), end_road(ROAD);
    std::vector<uint8_t> shared(SHARED), road(ROAD);
    std::vector<uint8_t> arcade_shared_w(SHARED), arcade_road_w(ROAD);
    std::vector<uint8_t> other_written(SHARED);
    std::vector<uint8_t> port_shared_r(SHARED), port_shared_w(SHARED), port_road_w(ROAD);
    unsigned frames = 0, matched = 0, failed = 0, raced = 0, special = 0, reported = 0;
    OSubRoad road_work;

    while (in.peek()!=std::char_traits<char>::eof())
    {
        uint8_t header[8], chips[16];
        in.read(reinterpret_cast<char*>(header), 8);
        uint8_t entry[4]{};
        if (initializer) in.read(reinterpret_cast<char*>(entry),4);
        in.read(reinterpret_cast<char*>(start_shared.data()), SHARED);
        in.read(reinterpret_cast<char*>(start_road.data()), ROAD);
        in.read(reinterpret_cast<char*>(chips), 16);
        in.read(reinterpret_cast<char*>(end_shared.data()), SHARED);
        in.read(reinterpret_cast<char*>(end_road.data()), ROAD);
        if (!in || !read_offsets(in, arcade_shared_w) || !read_offsets(in, arcade_road_w)) throw std::runtime_error("truncated road capture");
        if (explicit_races && !read_offsets(in,other_written)) throw std::runtime_error("truncated race set");
        std::fill(arcade_shared_w.begin() + STACK_START, arcade_shared_w.end(), 0);
        const unsigned record = r32(header, 0);
        const uint32_t frame = r32(header, 4);
        ++frames;

        shared = start_shared;
        road = start_road;
        std::fill(port_shared_r.begin(), port_shared_r.end(), 0);
        std::fill(port_shared_w.begin(), port_shared_w.end(), 0);
        std::fill(port_road_w.begin(), port_road_w.end(), 0);
        SubBus::Trace trace{port_shared_r.data(), port_shared_w.data(), port_road_w.data()};
        SubBus bus(roms.subcpu.rom, roms.subcpu.length, shared.data(), road.data());
        bus.trace = &trace;
        bus.mul.load(r16(chips, 0), r16(chips, 2));
        const uint16_t div_regs[6] = {r16(chips, 4), r16(chips, 6), r16(chips, 8),
                                      r16(chips, 10), r16(chips, 12), r16(chips, 14)};
        bus.div.load(div_regs);

        if (initializer) {
            if (r32(entry,0)==0x436) road_work.service_requests(bus);
            else road_work.initialize(bus,r32(entry,0));
        }
        if (!initializer) {
            special += start_shared[0x321]==1 || static_cast<int8_t>(start_shared[0x321])<0;
            road_work.frame(bus);
        }

        // A byte that changed without the sub CPU writing it was written by
        // the main CPU mid-window; if the port read it, the replay is invalid.
        bool race = false;
        for (size_t i = 0; i < SHARED && !race; ++i)
            race = port_shared_r[i] && (explicit_races ? other_written[i] : (!arcade_shared_w[i] && start_shared[i] != end_shared[i]));
        if (race)
        {
            ++raced;
            continue;
        }

        const unsigned differences =
            compare("shared", record, frame, arcade_shared_w, port_shared_w, end_shared, shared, reported) +
            compare("road", record, frame, arcade_road_w, port_road_w, end_road, road, reported);
        differences ? ++failed : ++matched;
    }

    std::cout << "Sub CPU road work vs arcade: " << frames << " frames, " << matched << " identical, " << failed
              << " different, " << raced << " skipped (inputs changed mid-frame), " << special
              << " special-sequence frames\n";
    return failed == 0 && matched > 0 ? 0 : 1;
}

catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

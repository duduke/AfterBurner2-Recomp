// Differential test: the ported sub CPU sprite pass (engine/osubsprites)
// against the real arcade code running in MAME.
//
// Input is a capture from tools/oracle/capture_subsprites.lua (see `make
// oracle-capture`): per pass, the 32KB shared RAM block just before and just
// after the sub CPU ran it. Each pass is replayed from its "before" snapshot
// and every region the pass writes must match the "after" snapshot exactly.
//
// Usage: oracle_subsprites [capture-file]   (run from the repository root)

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "roms.hpp"
#include "engine/osubsprites.hpp"

namespace
{
    constexpr size_t SHARED = SubBus::SHARED_BYTES;

    uint16_t r16(const uint8_t* m, uint32_t o) { return static_cast<uint16_t>((m[o] << 8) | m[o + 1]); }
    uint32_t r32(const uint8_t* m, uint32_t o) { return (static_cast<uint32_t>(r16(m, o)) << 16) | r16(m, o + 2); }

    // Shared-block offset of a pointer stored in shared RAM, or -1.
    long offset_of(uint32_t ptr)
    {
        const uint32_t a = ptr & 0xfffff;
        if (a >= 0x80000 && a < 0xa0000) return a & 0x3fff;
        if (a >= 0xa0000 && a < 0xc0000) return 0x4000 + (a & 0x3fff);
        return -1;
    }

    struct Region { const char* name; long start; long length; };
}

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "build/oracle/subsprites.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::cerr << "No capture at " << path << " — run `make oracle-capture` first.\n";
        return 1;
    }
    char magic[8];
    in.read(magic, 8);
    if (!in || std::memcmp(magic, "AB2ORCL1", 8) != 0)
    {
        std::cerr << path << " is not a sprite-pass capture\n";
        return 1;
    }
    if (!roms.load_aburner2())
        return 1;

    std::vector<uint8_t> before(SHARED), after(SHARED), work(SHARED);
    unsigned passes = 0, raced = 0, matched = 0, failed = 0, reported = 0;
    OSubSprites sprite_pass;

    for (;;)
    {
        uint8_t header[8];
        in.read(reinterpret_cast<char*>(header), 8);
        in.read(reinterpret_cast<char*>(before.data()), SHARED);
        in.read(reinterpret_cast<char*>(after.data()), SHARED);
        if (!in) break;
        const uint32_t pass_no = r32(header, 0), frame = r32(header, 4);
        ++passes;

        // If the main CPU swapped buffers or changed the pass's inputs while
        // it ran, the "before" snapshot isn't what the pass actually read.
        if (std::memcmp(&before[0x7c04], &after[0x7c04], 0x20) != 0 ||
            r16(before.data(), 0x56) != r16(after.data(), 0x56) || before[0x3a] != after[0x3a])
        {
            ++raced;
            continue;
        }

        work = before;
        SubBus bus(roms.subcpu.rom, roms.subcpu.length, work.data());
        sprite_pass.pass(bus);

        const long sorted = offset_of(r32(after.data(), 0x7c18));
        const long count_at = offset_of(r32(after.data(), 0x7c20));
        if (sorted < 0 || count_at < 0)
        {
            std::cerr << "pass " << pass_no << ": output pointers outside shared RAM\n";
            return 1;
        }
        const long count = r16(after.data(), static_cast<uint32_t>(count_at));
        const long span = 16 * (count + 1);
        const Region regions[] = {
            {"sorted count", count_at, 2},
            {"sorted descriptors", sorted, span},
            {"interpolated descriptors", sorted + 0xc00, span},
            {"size table", 0x7800, 0x400},
            {"depth buckets", 0x5800, 0x2000},
            {"sort order", 0x7d00, 0x100},
        };

        bool ok = true;
        for (const auto& region : regions)
        {
            if (region.start + region.length > static_cast<long>(SHARED))
            {
                std::cerr << "pass " << pass_no << ": " << region.name << " runs past shared RAM\n";
                return 1;
            }
            for (long i = 0; i < region.length; ++i)
            {
                const long at = region.start + i;
                if (work[at] == after[at]) continue;
                ok = false;
                if (reported++ < 12)
                {
                    char line[160];
                    std::snprintf(line, sizeof line,
                                  "pass %u (frame %u): %s differs at +%ld (shared $%04lX): arcade %02X, port %02X",
                                  pass_no, frame, region.name, i, at, after[at], work[at]);
                    std::cerr << line;
                    if (region.length == span)
                        std::cerr << "  [descriptor " << i / 16 << ", byte " << i % 16 << " of " << count << "]";
                    std::cerr << '\n';
                }
                break;
            }
        }
        ok ? ++matched : ++failed;
    }

    std::cout << "Sub CPU sprite pass vs arcade: " << passes << " passes, " << matched << " identical, "
              << failed << " different, " << raced << " skipped (inputs changed mid-pass)\n";
    return failed == 0 && matched > 0 ? 0 : 1;
}

/***************************************************************************
    Binary ROM file loader.

    Loads an individual ROM chip dump into a region of memory, optionally
    de-interleaving it with sibling ROMs that together form a wider data bus
    (e.g. two 8-bit program ROMs interleaved to form a 16-bit 68000 bus, or
    four interleaved to form a 32-bit sprite/tile ROM bus).

    Architecture follows Chris White's Cannonball RomLoader.
***************************************************************************/

#pragma once

#include <cstdint>

class RomLoader
{
public:
    enum Mode { NORMAL = 1, INTERLEAVE2 = 2, INTERLEAVE4 = 4 };

    uint8_t* rom = nullptr;
    uint32_t length = 0;
    bool loaded = false;

    RomLoader();
    ~RomLoader();

    // Allocate a zeroed region of the given size to load ROM chips into.
    void init(uint32_t region_size);

    // Load a ROM file from the roms/ directory into this region at `offset`,
    // de-interleaving every `mode` bytes if mode != NORMAL, and verify it
    // against `expected_crc`. Returns 0 on success, 1 on failure.
    int load_rom(const char* filename, uint32_t offset, uint32_t expected_length,
                  uint32_t expected_crc, Mode mode = NORMAL, bool verbose = true);

    // Load a standalone file (no region, no interleave, no CRC check) — used
    // for things like external music data.
    int load_binary(const char* filename);

    void unload();

    inline uint8_t read8(uint32_t addr) const { return rom[addr]; }
    inline uint16_t read16(uint32_t addr) const { return (rom[addr] << 8) | rom[addr + 1]; }
    inline uint32_t read32(uint32_t addr) const
    {
        return (rom[addr] << 24) | (rom[addr + 1] << 16) | (rom[addr + 2] << 8) | rom[addr + 3];
    }

private:
    static long filesize(const char* path);
    static uint32_t crc32(const uint8_t* data, uint32_t length);
};

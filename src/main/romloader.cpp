#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "romloader.hpp"

namespace
{
    // Directory that ROM files are read from, relative to the working directory.
    constexpr const char* ROM_DIR = "roms/";
}

RomLoader::RomLoader() = default;

RomLoader::~RomLoader()
{
    unload();
}

void RomLoader::init(uint32_t region_size)
{
    unload();
    rom = new uint8_t[region_size]();
    length = region_size;
    loaded = false;
}

void RomLoader::unload()
{
    delete[] rom;
    rom = nullptr;
    length = 0;
    loaded = false;
}

long RomLoader::filesize(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f)
        return -1;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fclose(f);
    return size;
}

uint32_t RomLoader::crc32(const uint8_t* data, uint32_t length)
{
    static uint32_t table[256];
    static bool table_ready = false;
    if (!table_ready)
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        table_ready = true;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < length; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

int RomLoader::load_rom(const char* filename, uint32_t offset, uint32_t expected_length,
                         uint32_t expected_crc, Mode mode, bool verbose)
{
    std::string path = std::string(ROM_DIR) + filename;

    long size = filesize(path.c_str());
    if (size < 0)
    {
        std::cerr << "romloader: cannot open " << path << std::endl;
        return 1;
    }
    if (static_cast<uint32_t>(size) != expected_length)
    {
        std::cerr << "romloader: " << filename << " is " << size
                  << " bytes, expected " << expected_length << std::endl;
        return 1;
    }

    std::FILE* f = std::fopen(path.c_str(), "rb");
    std::vector<uint8_t> buffer(size);
    if (std::fread(buffer.data(), 1, size, f) != static_cast<size_t>(size))
    {
        std::fclose(f);
        std::cerr << "romloader: short read on " << filename << std::endl;
        return 1;
    }
    std::fclose(f);

    uint32_t actual_crc = crc32(buffer.data(), static_cast<uint32_t>(size));
    if (actual_crc != expected_crc)
    {
        std::cerr << "romloader: " << filename << " CRC32 mismatch (got "
                  << std::hex << actual_crc << ", expected " << expected_crc
                  << std::dec << ")" << std::endl;
        return 1;
    }

    for (long i = 0; i < size; i++)
        rom[offset + (static_cast<uint32_t>(i) * mode)] = buffer[i];

    if (verbose)
        std::cout << "romloader: loaded " << filename << std::endl;

    loaded = true;
    return 0;
}

int RomLoader::load_binary(const char* filename)
{
    long size = filesize(filename);
    if (size < 0)
        return 1;

    init(static_cast<uint32_t>(size));

    std::FILE* f = std::fopen(filename, "rb");
    if (std::fread(rom, 1, size, f) != static_cast<size_t>(size))
    {
        std::fclose(f);
        return 1;
    }
    std::fclose(f);

    loaded = true;
    return 0;
}

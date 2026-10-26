#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

// Compile the vendored reader here so standalone oracle tools need no new
// library or link flags. No archive writing or zlib compatibility API.
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_APIS
#include "../third_party/miniz/miniz.c"
#include "../third_party/miniz/miniz_tinfl.c"
#include "../third_party/miniz/miniz_zip.c"
#include "romloader.hpp"

namespace
{
    std::filesystem::path source;
    struct ZipReader {
        mz_zip_archive archive{};
        std::filesystem::path path;
        bool open{};
        ~ZipReader() { reset(); }
        void reset() {
            if (open) mz_zip_reader_end(&archive);
            if (input) std::fclose(input);
            input = nullptr; archive = {}; path.clear(); open = false;
        }
        bool read(const std::filesystem::path& filename, const char* chip,
                  uint32_t expected_length, std::vector<uint8_t>& buffer) {
            if (filename != path || !open) {
                reset(); path = filename;
                // Use a FILE opened through filesystem::path for Windows Unicode paths.
                #ifdef _WIN32
                auto* file = _wfopen(filename.c_str(), L"rb");
                #else
                auto* file = std::fopen(filename.c_str(), "rb");
                #endif
                if (!file) return false;
                open = mz_zip_reader_init_cfile(&archive, file, 0, 0);
                // init_cfile borrows the FILE. Keep ownership in this reader.
                if (!open) { std::fclose(file); return false; }
                input = file;
            }
            // Permit a folder prefix in ZIPs. Match complete chip basenames,
            // reject duplicates instead of depending on archive entry order.
            int found = -1;
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); ++i) {
                mz_zip_archive_file_stat stat{};
                if (!mz_zip_reader_file_stat(&archive, i, &stat)) return false;
                if (mz_zip_reader_is_file_a_directory(&archive, i)) continue;
                const std::string name(stat.m_filename);
                const auto slash = name.find_last_of("/\\");
                if (name.substr(slash == std::string::npos ? 0 : slash + 1) != chip) continue;
                if (found >= 0 || stat.m_uncomp_size != expected_length ||
                    !mz_zip_reader_is_file_supported(&archive, i)) return false;
                found = static_cast<int>(i);
            }
            if (found < 0) return false;
            buffer.resize(expected_length);
            return mz_zip_reader_extract_to_mem(&archive, found, buffer.data(), buffer.size(), 0);
        }
        std::FILE* input{};
    } zip;
}

void RomLoader::set_source(const std::string& path)
{
    zip.reset();
    source = std::filesystem::path(std::u8string(path.begin(), path.end()));
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
    const auto stride = static_cast<uint32_t>(mode);
    if (!rom || (stride != 1 && stride != 2 && stride != 4) || !expected_length ||
        offset >= length || uint64_t(offset) + uint64_t(expected_length - 1) * stride >= length)
        return 1;

    const auto base = source.empty() ? std::filesystem::path("roms") : source;
    std::error_code error;
    const bool archive = std::filesystem::is_regular_file(base, error);
    const auto path = base / filename;
    std::vector<uint8_t> buffer;
    if (archive || (source.empty() && !std::filesystem::exists(path, error))) {
        if (!zip.read(archive ? base : base / "aburner2.zip", filename, expected_length, buffer)) {
            std::cerr << "romloader: cannot read " << filename << " from ROM ZIP" << std::endl;
            return 1;
        }
    } else {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() != static_cast<std::streamoff>(expected_length)) {
            std::cerr << "romloader: missing or incorrect size: " << path << std::endl;
            return 1;
        }
        buffer.resize(expected_length);
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), buffer.size())) return 1;
    }
    const auto size = expected_length;

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

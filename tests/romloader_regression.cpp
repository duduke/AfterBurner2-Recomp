#include "romloader.hpp"
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#endif

int main(int argc, char** argv) {
    if (argc != 4) return 2;
#ifdef _WIN32
    // This standalone fixture has no SDL UTF-8 entrypoint. The narrow CRT argv
    // loses filenames outside the active code page before the loader sees them.
    int wide_count=0;
    auto* wide_args=CommandLineToArgvW(GetCommandLineW(),&wide_count);
    if(!wide_args || wide_count!=argc){if(wide_args)LocalFree(wide_args);return 2;}
    const auto utf8=std::filesystem::path(wide_args[1]).u8string();
    LocalFree(wide_args);
    RomLoader::set_source(std::string(utf8.begin(),utf8.end()));
#else
    RomLoader::set_source(argv[1]);
#endif
    const bool success = std::string(argv[2]) == "pass";
    const unsigned stride = std::strtoul(argv[3], nullptr, 10);
    RomLoader region;
    region.init(1025);
    // Python builds the fixture independently with bytes(range(256)).
    const auto mode = static_cast<RomLoader::Mode>(stride);
    const bool loaded = region.load_rom("chip.bin", 1, 256, 0x29058c73, mode, false) == 0;
    if (loaded != success || region.loaded != success) return 1;
    for (unsigned i = 0; i < region.length; ++i) {
        const auto expected = success && i >= 1 && (i-1)%stride == 0 && (i-1)/stride < 256 ? (i-1)/stride : 0;
        if (region.rom[i] != expected) return 1;
    }
    // Invalid placements must reject before touching the destination.
    if (!region.load_rom("chip.bin", 1025, 256, 0x29058c73, mode, false)) return 1;
    if (!region.load_rom("chip.bin", 1024, 256, 0x29058c73, mode, false)) return 1;
    if (!region.load_rom("chip.bin", 0, 0, 0, mode, false)) return 1;
    RomLoader::set_source(""); // closes an open archive; safe for repeated selection
    return 0;
}

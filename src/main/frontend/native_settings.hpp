#pragma once
#include <cstdio>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace Frontend {
enum class Scaling { nearest, linear, integer, pixel_art };
enum class SpriteFilter { none, epx, scale3x, sai, super_sai, scalefx };
inline constexpr const char* sprite_filter_keys[]{"none","epx","scale3x","2xsai","super2xsai","scalefx"};
inline constexpr const char* sprite_filter_labels[]{"OFF","EPX 2X","SCALE3X","2XSAI","SUPER2XSAI","SCALEFX 3X"};
inline constexpr int sprite_filter_count=6;
// MAME segaxbd.cpp, aburner2 IO1PORTD / SWB:4,5 and SWB:7,8.
inline constexpr uint8_t lives_bits[]{0x18,0x10,0x08,0x00};
inline constexpr uint8_t difficulty_bits[]{0x80,0xc0,0x40,0x00};
// UI splits SWB:4 (3/4 ships) and SWB:5 (Ship Increase); keep the
// combined index in preferences so earlier files preserve their exact pins.
inline constexpr const char* lives_labels[]{"3","4"};
inline constexpr const char* difficulty_labels[]{"EASY","NORMAL","HARD","HARDEST"};
enum class Stats { off, fps, detailed };
struct Settings {
    Scaling scaling{Scaling::nearest};
    bool fullscreen{};
    Stats stats{Stats::off};
    SpriteFilter sprites{SpriteFilter::none};
    bool clouds{},trails{},explosions{},afterburner{};
    unsigned lives{},difficulty{1};
    bool ground{},widescreen{};
    bool operator==(const Settings&) const = default;
};
inline unsigned viewport_width(const Settings& s){return s.widescreen?398:320;}
inline std::array<uint8_t,3> dip_switches(const Settings& settings) {
    return {0xff,0xff,uint8_t((0xdd & ~0xd8) | lives_bits[settings.lives] | difficulty_bits[settings.difficulty])};
}
inline Settings load_settings(const std::string& path, bool pixel_art) {
    Settings result;
    std::ifstream file(path); std::string line;
    if (!std::getline(file,line) || line != "AB2SETTINGS1") return result;
    while (std::getline(file,line)) {
        for(unsigned i=0;i<4;++i) {
            if(line=="lives="+std::to_string(i)) result.lives=i;
            if(line=="difficulty="+std::to_string(i)) result.difficulty=i;
        }
        for(int i=0;i<sprite_filter_count;++i)
            if(line==std::string("sprites=")+sprite_filter_keys[i]) result.sprites=static_cast<SpriteFilter>(i);
        if (line=="scaling=nearest") result.scaling=Scaling::nearest;
        else if (line=="scaling=linear") result.scaling=Scaling::linear;
        else if (line=="scaling=integer") result.scaling=Scaling::integer;
        else if (line=="scaling=pixel-art" && pixel_art) result.scaling=Scaling::pixel_art;
        else if (line=="fullscreen=1") result.fullscreen=true;
        else if (line=="fullscreen=0") result.fullscreen=false;
        else if (line=="clouds=1") result.clouds=true;
        else if (line=="clouds=0") result.clouds=false;
        else if (line=="trails=1") result.trails=true;
        else if (line=="trails=0") result.trails=false;
        else if (line=="explosions=1") result.explosions=true;
        else if (line=="explosions=0") result.explosions=false;
        else if (line=="afterburner=1") result.afterburner=true;
        else if (line=="afterburner=0") result.afterburner=false;
        else if (line=="widescreen=1") result.widescreen=true;
        else if (line=="widescreen=0") result.widescreen=false;
        else if (line=="ground=1") result.ground=true;
        else if (line=="ground=0") result.ground=false;
        else if (line=="stats=0") result.stats=Stats::off;
        else if (line=="stats=1") result.stats=Stats::fps;
        else if (line=="stats=2") result.stats=Stats::detailed;
    }
    return result;
}
inline bool save_settings(const std::string& path, const Settings& settings) {
    if (path.empty() || settings.lives>3 || settings.difficulty>3 || static_cast<unsigned>(settings.scaling)>3 ||
        static_cast<unsigned>(settings.stats)>2 || static_cast<unsigned>(settings.sprites)>=sprite_filter_count) return false;
    const char* scales[]{"nearest","linear","integer","pixel-art"};
    const auto temporary=path+".tmp";
    std::ofstream file(temporary,std::ios::trunc);
    if (!file) return false;
    file << "AB2SETTINGS1\nscaling=" << scales[static_cast<int>(settings.scaling)]
         << "\nclouds=" << settings.clouds << "\ntrails=" << settings.trails
         << "\nexplosions=" << settings.explosions << "\nafterburner=" << settings.afterburner
         << "\nwidescreen=" << settings.widescreen << "\nground=" << settings.ground
         << "\nlives=" << settings.lives << "\ndifficulty=" << settings.difficulty
         << "\nfullscreen=" << settings.fullscreen << "\nstats=" << static_cast<int>(settings.stats) << "\nsprites=" << sprite_filter_keys[static_cast<int>(settings.sprites)] << '\n';
    file.close();
    if (!file || std::rename(temporary.c_str(),path.c_str())!=0) {
        std::remove(temporary.c_str()); return false;
    }
    return true;
}
}

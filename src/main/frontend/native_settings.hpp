#pragma once
#include <cstdio>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace Frontend {
enum class Scaling { nearest, linear, integer, pixel_art };
enum class SpriteFilter { none, epx, scale3x, scalefx };
inline constexpr const char* sprite_filter_keys[]{"none","epx","scale3x","scalefx"};
inline constexpr const char* sprite_filter_labels[]{"OFF","EPX 2X","SCALE3X","SCALEFX 3X"};
inline constexpr int sprite_filter_count=4;
// MAME segaxbd.cpp, aburner2 IO1PORTD / SWB:4,5 and SWB:7,8.
inline constexpr uint8_t lives_bits[]{0x18,0x10,0x08,0x00};
inline constexpr uint8_t difficulty_bits[]{0x80,0xc0,0x40,0x00};
// UI splits SWB:4 (3/4 ships) and SWB:5 (Ship Increase); keep the
// combined index in preferences so earlier files preserve their exact pins.
inline constexpr const char* lives_labels[]{"3","4"};
inline constexpr const char* difficulty_labels[]{"EASY","NORMAL","HARD","HARDEST"};
enum class Stats { off, fps, detailed };
enum class Hud { original, sharp, custom };
inline constexpr const char* hud_keys[]{"original","sharp","custom"};
inline constexpr const char* hud_labels[]{"ORIGINAL","SHARP 3X","CUSTOM"};
inline constexpr unsigned hud_count=3;
enum class Crt { off, subtle, arcade, retroarch };
inline constexpr const char* crt_keys[]{"off","subtle","arcade","retroarch"};
inline constexpr const char* crt_labels[]{"OFF","SUBTLE","ARCADE","RETROARCH"};
inline constexpr unsigned crt_count=4;
enum class Viewport { original, wide16_9, wide16_10, wide21_9 };
inline constexpr const char* viewport_keys[]{"original","16:9","16:10","21:9"};
inline constexpr const char* viewport_labels[]{"ORIGINAL","16:9 PREVIEW","16:10 PREVIEW","21:9 PREVIEW"};
inline constexpr unsigned viewport_widths[]{320,398,358,522};
inline constexpr int viewport_count=4;
struct Settings {
    Scaling scaling{Scaling::nearest};
    bool fullscreen{};
    Stats stats{Stats::off};
    SpriteFilter sprites{SpriteFilter::none};
    bool clouds{},trails{},explosions{},afterburner{};
    unsigned lives{},difficulty{1};
    bool ground{true};
    bool interactive_rearm{true};
    Viewport viewport{Viewport::original};
    Hud hud{Hud::original};
    std::string hud_atlas;
    Crt crt{Crt::off};
    unsigned crt_scanlines{25},crt_mask{15};
    std::string shader_preset,shader_runtime;
    bool operator==(const Settings&) const = default;
};
inline bool is_wide(const Settings& s){return s.viewport!=Viewport::original;}
inline unsigned viewport_width(const Settings& s){return viewport_widths[static_cast<unsigned>(s.viewport)];}
inline std::array<uint8_t,3> dip_switches(const Settings& settings) {
    return {0xff,0xff,uint8_t((0xdd & ~0xd8) | lives_bits[settings.lives] | difficulty_bits[settings.difficulty])};
}
inline Settings load_settings(const std::string& path, bool pixel_art) {
    Settings result;Viewport explicit_viewport{};bool has_viewport=false;
    bool has_crt_scanlines=false,has_crt_mask=false;
    std::ifstream file(path); std::string line;
    if (!std::getline(file,line) || line != "AB2SETTINGS1") return result;
    while (std::getline(file,line)) {
        for(unsigned i=0;i<crt_count;++i)if(line==std::string("crt=")+crt_keys[i])result.crt=static_cast<Crt>(i);
        const auto percent=[&](const char* key,unsigned& target,bool& present){
            const std::string_view prefix(key);
            if(!line.starts_with(prefix))return;
            unsigned value{};const char* begin=line.data()+prefix.size();const char* end=line.data()+line.size();
            const auto parsed=std::from_chars(begin,end,value);
            if(parsed.ec==std::errc{} && parsed.ptr==end && value<=100){target=value;present=true;}
        };
        percent("crt-scanlines=",result.crt_scanlines,has_crt_scanlines);
        percent("crt-mask=",result.crt_mask,has_crt_mask);
        if(line.find('\0')==std::string::npos && line.find('\r')==std::string::npos){
            if(line.starts_with("shader-preset="))result.shader_preset=line.substr(14);
            if(line.starts_with("shader-runtime="))result.shader_runtime=line.substr(15);
        }
        for(unsigned i=0;i<hud_count;++i)if(line==std::string("hud=")+hud_keys[i])result.hud=static_cast<Hud>(i);
        if(line.starts_with("hud-atlas="))result.hud_atlas=line.substr(10);
        for(int i=0;i<viewport_count;++i)if(line==std::string("viewport=")+viewport_keys[i]){
            explicit_viewport=static_cast<Viewport>(i);has_viewport=true;
        }
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
        else if (line=="widescreen=1") result.viewport=Viewport::wide16_9;
        else if (line=="widescreen=0") result.viewport=Viewport::original;
        else if (line=="ground=1") result.ground=true;
        else if (line=="ground=0") result.ground=false;
        else if (line=="interactive-rearm=1") result.interactive_rearm=true;
        else if (line=="interactive-rearm=0") result.interactive_rearm=false;
        else if (line=="stats=0") result.stats=Stats::off;
        else if (line=="stats=1") result.stats=Stats::fps;
        else if (line=="stats=2") result.stats=Stats::detailed;
    }
    if(has_viewport)result.viewport=explicit_viewport;
    if(result.crt==Crt::arcade){if(!has_crt_scanlines)result.crt_scanlines=50;if(!has_crt_mask)result.crt_mask=35;}
    return result;
}
inline bool save_settings(const std::string& path, const Settings& settings) {
    if (path.empty() || settings.lives>3 || settings.difficulty>3 || static_cast<unsigned>(settings.scaling)>3 ||
        static_cast<unsigned>(settings.stats)>2 || static_cast<unsigned>(settings.sprites)>=sprite_filter_count ||
        static_cast<unsigned>(settings.viewport)>=viewport_count || static_cast<unsigned>(settings.hud)>=hud_count ||
        static_cast<unsigned>(settings.crt)>=crt_count || settings.crt_scanlines>100 || settings.crt_mask>100 ||
        settings.hud_atlas.find_first_of("\r\n")!=std::string::npos ||
        settings.shader_preset.find_first_of("\r\n")!=std::string::npos || settings.shader_preset.find('\0')!=std::string::npos ||
        settings.shader_runtime.find_first_of("\r\n")!=std::string::npos || settings.shader_runtime.find('\0')!=std::string::npos) return false;
    const char* scales[]{"nearest","linear","integer","pixel-art"};
    const auto temporary=path+".tmp";
    std::ofstream file(temporary,std::ios::trunc);
    if (!file) return false;
    file << "AB2SETTINGS1\nscaling=" << scales[static_cast<int>(settings.scaling)]
         << "\nclouds=" << settings.clouds << "\ntrails=" << settings.trails
         << "\nexplosions=" << settings.explosions << "\nafterburner=" << settings.afterburner
         << "\nwidescreen=" << is_wide(settings) << "\nviewport=" << viewport_keys[static_cast<unsigned>(settings.viewport)] << "\nground=" << settings.ground << "\ninteractive-rearm=" << settings.interactive_rearm
         << "\nhud=" << hud_keys[static_cast<unsigned>(settings.hud)] << "\nhud-atlas=" << settings.hud_atlas
         << "\ncrt=" << crt_keys[static_cast<unsigned>(settings.crt)] << "\ncrt-scanlines=" << settings.crt_scanlines << "\ncrt-mask=" << settings.crt_mask
         << "\nshader-preset=" << settings.shader_preset << "\nshader-runtime=" << settings.shader_runtime
         << "\nlives=" << settings.lives << "\ndifficulty=" << settings.difficulty
         << "\nfullscreen=" << settings.fullscreen << "\nstats=" << static_cast<int>(settings.stats) << "\nsprites=" << sprite_filter_keys[static_cast<int>(settings.sprites)] << '\n';
    file.close();
    std::error_code ec;
    if (file) std::filesystem::rename(temporary, path, ec);
    if (!file || ec) {
        std::remove(temporary.c_str()); return false;
    }
    return true;
}
}

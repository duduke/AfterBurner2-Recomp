#pragma once
#include <cstdio>
#include <fstream>
#include <string>

namespace Frontend {
enum class Scaling { nearest, linear, integer, pixel_art };
enum class SpriteFilter { none, epx, scale3x, sai, super_sai, scalefx };
inline constexpr const char* sprite_filter_keys[]{"none","epx","scale3x","2xsai","super2xsai","scalefx"};
inline constexpr const char* sprite_filter_labels[]{"OFF","EPX 2X","SCALE3X","2XSAI","SUPER2XSAI","SCALEFX 3X"};
inline constexpr int sprite_filter_count=6;
enum class Stats { off, fps, detailed };
struct Settings {
    Scaling scaling{Scaling::nearest};
    bool fullscreen{};
    Stats stats{Stats::off};
    SpriteFilter sprites{SpriteFilter::none};
    bool operator==(const Settings&) const = default;
};
inline Settings load_settings(const std::string& path, bool pixel_art) {
    Settings result;
    std::ifstream file(path); std::string line;
    if (!std::getline(file,line) || line != "AB2SETTINGS1") return result;
    while (std::getline(file,line)) {
        for(int i=0;i<sprite_filter_count;++i)
            if(line==std::string("sprites=")+sprite_filter_keys[i]) result.sprites=static_cast<SpriteFilter>(i);
        if (line=="scaling=nearest") result.scaling=Scaling::nearest;
        else if (line=="scaling=linear") result.scaling=Scaling::linear;
        else if (line=="scaling=integer") result.scaling=Scaling::integer;
        else if (line=="scaling=pixel-art" && pixel_art) result.scaling=Scaling::pixel_art;
        else if (line=="fullscreen=1") result.fullscreen=true;
        else if (line=="fullscreen=0") result.fullscreen=false;
        else if (line=="stats=0") result.stats=Stats::off;
        else if (line=="stats=1") result.stats=Stats::fps;
        else if (line=="stats=2") result.stats=Stats::detailed;
    }
    return result;
}
inline bool save_settings(const std::string& path, const Settings& settings) {
    if (path.empty() || static_cast<unsigned>(settings.scaling)>3 ||
        static_cast<unsigned>(settings.stats)>2 || static_cast<unsigned>(settings.sprites)>=sprite_filter_count) return false;
    const char* scales[]{"nearest","linear","integer","pixel-art"};
    const auto temporary=path+".tmp";
    std::ofstream file(temporary,std::ios::trunc);
    if (!file) return false;
    file << "AB2SETTINGS1\nscaling=" << scales[static_cast<int>(settings.scaling)]
         << "\nfullscreen=" << settings.fullscreen << "\nstats=" << static_cast<int>(settings.stats) << "\nsprites=" << sprite_filter_keys[static_cast<int>(settings.sprites)] << '\n';
    file.close();
    if (!file || std::rename(temporary.c_str(),path.c_str())!=0) {
        std::remove(temporary.c_str()); return false;
    }
    return true;
}
}

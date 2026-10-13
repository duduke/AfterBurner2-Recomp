#include "frontend/native_settings.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main() try {
    std::filesystem::create_directories("build");
    const std::string path="build/native_settings_regression.cfg";
    std::filesystem::remove(path);
    require(Frontend::load_settings(path,true)==Frontend::Settings{},"missing settings must preserve arcade defaults");
    const Frontend::Settings chosen{Frontend::Scaling::integer,true,Frontend::Stats::detailed};
    require(Frontend::save_settings(path,chosen),"cannot save settings");
    require(Frontend::load_settings(path,true)==chosen,"persistent settings round-trip changed");
    for(int i=0;i<Frontend::sprite_filter_count;++i) {
        auto filter=chosen;filter.sprites=static_cast<Frontend::SpriteFilter>(i);
        require(Frontend::save_settings(path,filter) && Frontend::load_settings(path,true)==filter,"sprite filter persistence failed");
    }
    for(bool explosions:{false,true})for(bool afterburner:{false,true}){auto effects=chosen;effects.explosions=explosions;effects.afterburner=afterburner;require(Frontend::save_settings(path,effects) && Frontend::load_settings(path,true)==effects,"explosion/flame persistence failed");}
    for(bool clouds:{false,true})for(bool trails:{false,true}){auto effects=chosen;effects.clouds=clouds;effects.trails=trails;require(Frontend::save_settings(path,effects) && Frontend::load_settings(path,true)==effects,"independent effect persistence failed");}
    require(!std::filesystem::exists(path+".tmp"),"temporary preferences left behind");
    {std::ofstream f(path);f<<"AB2SETTINGS1\nscaling=pixel-art\nfullscreen=1\nstats=1\nunknown=future\n";}
    require(Frontend::load_settings(path,true).scaling==Frontend::Scaling::pixel_art,"available pixel-art setting lost");
    const auto fallback=Frontend::load_settings(path,false);
    require(fallback.scaling==Frontend::Scaling::nearest && fallback.fullscreen && fallback.stats==Frontend::Stats::fps,
        "unsupported filter must fall back without losing independent settings");
    {std::ofstream f(path);f<<"AB2SETTINGS1\nscaling=999\nstats=-1\nfullscreen=invalid\nsprites=xbrz\n";}
    require(Frontend::load_settings(path,true)==Frontend::Settings{},"malformed values must not enter runtime enums");
    {std::ofstream f(path);f<<"UNKNOWN VERSION\nfullscreen=1\n";}
    require(Frontend::load_settings(path,true)==Frontend::Settings{},"unknown format must preserve defaults");
    require(!Frontend::save_settings("build/nonexistent-settings-directory/test.cfg",chosen),"save failure must be reported");
    std::filesystem::remove(path);
    std::cout<<"Native settings passed: defaults, persistence, unavailable-filter fallback, invalid/versioned input and save failure.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

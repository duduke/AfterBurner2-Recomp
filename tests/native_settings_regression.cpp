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
    auto wide=chosen;wide.viewport=Frontend::Viewport::wide16_9;require(Frontend::viewport_width(wide)==398,"wide logical width");
    require(Frontend::save_settings(path,wide)&&Frontend::load_settings(path,true)==wide,"wide preference persistence");
    auto narrow_wide=chosen;narrow_wide.viewport=Frontend::Viewport::wide16_10;
    require(Frontend::viewport_width(narrow_wide)==358,"16:10 logical width");
    require(Frontend::save_settings(path,narrow_wide)&&Frontend::load_settings(path,true)==narrow_wide,"16:10 preference persistence");
    for(bool enabled:{false,true}){
        {std::ofstream f(path);f<<"AB2SETTINGS1\nwidescreen="<<enabled<<'\n';}
        require(Frontend::load_settings(path,true).viewport==(enabled?Frontend::Viewport::wide16_9:Frontend::Viewport::original),"legacy viewport migration changed");
    }
    for(bool explicit_first:{false,true}){
        {std::ofstream f(path);f<<"AB2SETTINGS1\n";
         if(explicit_first)f<<"viewport=16:10\n";f<<"widescreen=1\n";if(!explicit_first)f<<"viewport=16:10\n";}
        require(Frontend::load_settings(path,true).viewport==Frontend::Viewport::wide16_10,"explicit viewport must override legacy value in either order");
    }
    for(int i=0;i<Frontend::sprite_filter_count;++i) {
        auto filter=chosen;filter.sprites=static_cast<Frontend::SpriteFilter>(i);
        require(Frontend::save_settings(path,filter) && Frontend::load_settings(path,true)==filter,"sprite filter persistence failed");
    }
    for(bool explosions:{false,true})for(bool afterburner:{false,true}){auto effects=chosen;effects.explosions=explosions;effects.afterburner=afterburner;require(Frontend::save_settings(path,effects) && Frontend::load_settings(path,true)==effects,"explosion/flame persistence failed");}
    for(bool clouds:{false,true})for(bool trails:{false,true}){auto effects=chosen;effects.clouds=clouds;effects.trails=trails;require(Frontend::save_settings(path,effects) && Frontend::load_settings(path,true)==effects,"independent effect persistence failed");}
    auto ground=chosen;ground.ground=true;require(Frontend::save_settings(path,ground) && Frontend::load_settings(path,true)==ground,"ground setting persistence failed");
    for(unsigned lives=0;lives<4;++lives)for(unsigned difficulty=0;difficulty<4;++difficulty) {
        auto dips=chosen;dips.lives=lives;dips.difficulty=difficulty;
        require(Frontend::save_settings(path,dips) && Frontend::load_settings(path,true)==dips,"DIP persistence failed");
        const unsigned life_bits[]{0x18,0x10,0x08,0};const unsigned difficulty_bits[]{0x80,0xc0,0x40,0};
        const auto pins=Frontend::dip_switches(dips);
        require(pins[0]==255 && pins[1]==255 && pins[2]==(5|life_bits[lives]|difficulty_bits[difficulty]),"DIP pin encoding differs from aburner2 MAME definition");
    }
    require(Frontend::dip_switches({})==std::array<uint8_t,3>{255,255,221},"default physical DIP pins changed");
    auto invalid=chosen;invalid.lives=4;require(!Frontend::save_settings(path,invalid),"invalid lives accepted");
    invalid=chosen;invalid.difficulty=4;require(!Frontend::save_settings(path,invalid),"invalid difficulty accepted");
    invalid=chosen;invalid.viewport=static_cast<Frontend::Viewport>(3);require(!Frontend::save_settings(path,invalid),"invalid viewport accepted");
    require(!std::filesystem::exists(path+".tmp"),"temporary preferences left behind");
    {std::ofstream f(path);f<<"AB2SETTINGS1\nscaling=pixel-art\nfullscreen=1\nstats=1\nunknown=future\n";}
    require(Frontend::load_settings(path,true).scaling==Frontend::Scaling::pixel_art,"available pixel-art setting lost");
    const auto fallback=Frontend::load_settings(path,false);
    require(fallback.scaling==Frontend::Scaling::nearest && fallback.fullscreen && fallback.stats==Frontend::Stats::fps,
        "unsupported filter must fall back without losing independent settings");
    {std::ofstream f(path);f<<"AB2SETTINGS1\nscaling=999\nstats=-1\nfullscreen=invalid\nsprites=xbrz\nlives=4\ndifficulty=-1\nviewport=21:9\n";}
    require(Frontend::load_settings(path,true)==Frontend::Settings{},"malformed values must not enter runtime enums");
    {std::ofstream f(path);f<<"UNKNOWN VERSION\nfullscreen=1\n";}
    require(Frontend::load_settings(path,true)==Frontend::Settings{},"unknown format must preserve defaults");
    require(!Frontend::save_settings("build/nonexistent-settings-directory/test.cfg",chosen),"save failure must be reported");
    std::filesystem::remove(path);
    std::cout<<"Native settings passed: defaults, persistence, unavailable-filter fallback, invalid/versioned input and save failure.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

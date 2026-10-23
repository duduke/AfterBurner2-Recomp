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
    Frontend::Settings chosen;chosen.scaling=Frontend::Scaling::integer;chosen.fullscreen=true;chosen.stats=Frontend::Stats::detailed;
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
    for(unsigned i=0;i<Frontend::hud_count;++i) {
        auto hud=chosen;hud.hud=static_cast<Frontend::Hud>(i);hud.hud_atlas="build/my HUD atlas.png";
        require(Frontend::save_settings(path,hud) && Frontend::load_settings(path,true)==hud,"HUD/atlas preference persistence failed");
    }
    for(unsigned i=0;i<Frontend::crt_count;++i)for(unsigned scanlines:{0u,1u,25u,99u,100u})for(unsigned mask:{0u,15u,100u}){
        auto crt=chosen;crt.crt=static_cast<Frontend::Crt>(i);crt.crt_scanlines=scanlines;crt.crt_mask=mask;
        require(Frontend::save_settings(path,crt) && Frontend::load_settings(path,true)==crt,"CRT mode/independent strengths persistence failed");
    }
    for(unsigned i=0;i<Frontend::crt_count;++i){
        {std::ofstream f(path);f<<"AB2SETTINGS1\ncrt="<<Frontend::crt_keys[i]<<'\n';}
        const auto crt=Frontend::load_settings(path,true);
        require(crt.crt==static_cast<Frontend::Crt>(i) && crt.crt_scanlines==(i==2?50:25) && crt.crt_mask==(i==2?35:15),"CRT mode without strengths lost preset defaults");
    }
    {
        auto shader=chosen;shader.crt=Frontend::Crt::retroarch;
        shader.shader_preset="/Users/test/CRT presets/custom=bright.slangp";
        shader.shader_runtime="build/deps/librashader/librashader.dylib";
        require(Frontend::save_settings(path,shader) && Frontend::load_settings(path,true)==shader,"RetroArch shader/runtime paths did not round trip");
        shader.crt=Frontend::Crt::off;
        require(Frontend::save_settings(path,shader) && Frontend::load_settings(path,true)==shader,"disabling CRT erased the selected shader/runtime paths");
    }
    for(bool preset_first:{false,true}){
        {std::ofstream f(path);f<<"AB2SETTINGS1\n";if(preset_first)f<<"crt=arcade\n";
         f<<"crt-scanlines=5\ncrt-mask=95\n";if(!preset_first)f<<"crt=arcade\n";}
        const auto crt=Frontend::load_settings(path,true);
        require(crt.crt==Frontend::Crt::arcade && crt.crt_scanlines==5 && crt.crt_mask==95,"CRT preset overrode explicit strengths based on file order");
    }
    for(const char* malformed:{"","-1","+5","101","4294967296","999999999999999999999999999999999999","25junk","25.0"," 25","25 ","0x10"}){
        {std::ofstream f(path);f<<"AB2SETTINGS1\ncrt-scanlines="<<malformed<<"\ncrt-mask="<<malformed<<'\n';}
        require(Frontend::load_settings(path,true)==Frontend::Settings{},"malformed CRT strengths entered runtime settings");
        {std::ofstream f(path);f<<"AB2SETTINGS1\ncrt=arcade\ncrt-scanlines=10\ncrt-mask=90\ncrt-scanlines="<<malformed<<"\ncrt-mask="<<malformed<<'\n';}
        const auto crt=Frontend::load_settings(path,true);
        require(crt.crt==Frontend::Crt::arcade && crt.crt_scanlines==10 && crt.crt_mask==90,"malformed duplicate CRT strength erased a valid setting");
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
    invalid=chosen;invalid.hud=static_cast<Frontend::Hud>(Frontend::hud_count);require(!Frontend::save_settings(path,invalid),"invalid HUD accepted");
    invalid=chosen;invalid.crt=static_cast<Frontend::Crt>(Frontend::crt_count);require(!Frontend::save_settings(path,invalid),"invalid CRT mode accepted");
    invalid=chosen;invalid.crt_scanlines=101;require(!Frontend::save_settings(path,invalid),"invalid CRT scanline strength accepted");
    invalid=chosen;invalid.crt_mask=101;require(!Frontend::save_settings(path,invalid),"invalid CRT mask strength accepted");
    invalid=chosen;invalid.hud_atlas="art.png\nhud=custom";require(!Frontend::save_settings(path,invalid),"atlas path injected a setting");
    invalid=chosen;invalid.shader_preset="crt.slangp\ncrt=retroarch";require(!Frontend::save_settings(path,invalid),"shader path injected a setting");
    invalid=chosen;invalid.shader_runtime="runtime.dylib\rcrt=retroarch";require(!Frontend::save_settings(path,invalid),"runtime path injected a setting");
    invalid=chosen;invalid.shader_preset=std::string("crt\0.slangp",11);require(!Frontend::save_settings(path,invalid),"shader path accepted an embedded NUL");
    invalid=chosen;invalid.shader_runtime=std::string("lib\0.dylib",10);require(!Frontend::save_settings(path,invalid),"runtime path accepted an embedded NUL");
    require(!std::filesystem::exists(path+".tmp"),"temporary preferences left behind");
    {std::ofstream f(path);f<<"AB2SETTINGS1\nscaling=pixel-art\nfullscreen=1\nstats=1\nunknown=future\n";}
    require(Frontend::load_settings(path,true).scaling==Frontend::Scaling::pixel_art,"available pixel-art setting lost");
    const auto fallback=Frontend::load_settings(path,false);
    require(fallback.scaling==Frontend::Scaling::nearest && fallback.fullscreen && fallback.stats==Frontend::Stats::fps,
        "unsupported filter must fall back without losing independent settings");
    {std::ofstream f(path);f<<"AB2SETTINGS1\nscaling=999\nstats=-1\nfullscreen=invalid\nsprites=xbrz\nlives=4\ndifficulty=-1\nviewport=21:9\nhud=unknown\ncrt=unknown\n";}
    require(Frontend::load_settings(path,true)==Frontend::Settings{},"malformed values must not enter runtime enums");
    {std::ofstream f(path);f<<"UNKNOWN VERSION\nfullscreen=1\n";}
    require(Frontend::load_settings(path,true)==Frontend::Settings{},"unknown format must preserve defaults");
    require(!Frontend::save_settings("build/nonexistent-settings-directory/test.cfg",chosen),"save failure must be reported");
    std::filesystem::remove(path);
    std::cout<<"Native settings passed: defaults, persistence, unavailable-filter fallback, invalid/versioned input and save failure.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

#pragma once
#include "engine/stage_catalog.hpp"
#include <algorithm>
#include <random>
#include <vector>

namespace Frontend {
inline std::vector<unsigned> native_tanker_variants(const Roms& rom){
    std::vector<unsigned> variants;
    for(unsigned number=1;number<=StageCatalog::count(rom);++number){const auto stage=StageCatalog::get(rom,number);
        auto event=stage.events;
        for(unsigned record=0;record<8192;++record,event+=8){
            if(event>rom.maincpu.length || rom.maincpu.length-event<8)throw std::runtime_error("Tanker event program outside ROM");
            if(rom.maincpu.read16(event)==0xffff)break;
            const auto command=rom.maincpu.rom[event+2];
            // Main $DEE4/$DEFC/$DF0A: command 0, subselector 4 sets the
            // actual refueling flag. Discover slots from runtime ROM metadata.
            if(command==0 && rom.maincpu.read16(event+4)==4){variants.push_back(number);break;}
            if(command==12)break;
        }
    }
    return variants;
}
class CustomStageVariantPicker {
    std::mt19937_64 random;std::vector<unsigned> catalog,remaining;unsigned last{};
public:
    explicit CustomStageVariantPicker(uint64_t seed):random(seed){}
    unsigned pick(const std::vector<unsigned>& available,unsigned fixed=0){
        if(available.empty())throw std::runtime_error("No native tanker variants available");
        if(fixed){if(std::find(available.begin(),available.end(),fixed)==available.end())throw std::runtime_error("Selected native stage is not a tanker variant");remaining.clear();last=fixed;return fixed;}
        if(catalog!=available){catalog=available;remaining.clear();}
        if(remaining.empty()){remaining=catalog;std::shuffle(remaining.begin(),remaining.end(),random);}
        if(remaining.size()>1 && remaining.back()==last)std::swap(remaining.front(),remaining.back());
        last=remaining.back();remaining.pop_back();return last;
    }
};
}

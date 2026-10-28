#pragma once
#include "frontend/custom_stage_json.hpp"
#include <array>
#include <algorithm>
#include <cstdint>

namespace Frontend {
// Classic arcade colors, with one shared horizon color and a short water-side
// transition. The broad sky and foreground retain their original saturated look.
struct CustomStageOcean {
    using RGB=std::array<uint8_t,3>;
    RGB sky{0,170,255},haze{220,250,255},water{0,0,255};
    static RGB mix(RGB a,RGB b,double t){RGB out{};t=std::clamp(t,0.,1.);for(unsigned i=0;i<3;++i)out[i]=uint8_t(std::lround(a[i]+(b[i]-a[i])*t));return out;}
    RGB sky_at(double t)const{return mix(sky,haze,std::clamp((t-.60)/.40,0.,1.));}
    RGB sea_at(double t)const{return mix(haze,water,std::clamp(t/.17,0.,1.));}
    void load(const StageJson& node){node.keys({"sky","haze","water"});
        for(auto [key,dest]:{std::pair{"sky",&sky},{"haze",&haze},{"water",&water}})if(auto value=node.find(key)){
            if(value->kind!=StageJson::Kind::array || value->array.size()!=3)throw std::runtime_error("Ocean colors require three RGB8 channels");
            for(unsigned i=0;i<3;++i){const double n=value->array[i].numeric();if(n<0 || n>255 || std::floor(n)!=n)throw std::runtime_error("Invalid ocean color");(*dest)[i]=uint8_t(n);}}
    }
};
}

#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include "frontend/native_settings.hpp"
#include "frontend/scalers/sai.hpp"
#include "frontend/scalers/scalefx.hpp"

namespace Frontend {
inline unsigned sprite_factor(SpriteFilter filter) {
    return filter==SpriteFilter::none?1:filter==SpriteFilter::scale3x || filter==SpriteFilter::scalefx?3:2;
}
// Presentation-only scaler. Inputs are premultiplied ARGB8888, with transparent
// sprite texels represented by zero. Never samples the road/text into an edge.
class SpriteScaler {
public:
    std::span<const uint32_t> compose(std::span<const uint32_t> background,
                                     std::span<const uint32_t> sprites,
                                     unsigned width,unsigned height,SpriteFilter filter,
                                     std::span<const uint8_t> protected_pixels={}) {
        if(static_cast<unsigned>(filter)>=sprite_filter_count) throw std::invalid_argument("invalid sprite filter");
        if(!width || !height || background.size()!=size_t(width)*height || sprites.size()!=background.size())
            throw std::invalid_argument("invalid sprite presentation dimensions");
        if(!protected_pixels.empty() && protected_pixels.size()!=background.size())
            throw std::invalid_argument("invalid sprite protection dimensions");
        const auto blend=[](uint32_t fg,uint32_t bg) {
            const auto alpha=fg>>24;
            if(!alpha)return bg;
            if(alpha==255)return fg;
            uint32_t color=0xff000000u;
            for(unsigned shift: {0u,8u,16u}) {
                const auto value=((fg>>shift)&255)+((((bg>>shift)&255)*(255-alpha)+127)/255);
                color|=std::min(value,255u)<<shift;
            }
            return color;
        };
        // Off needs only composition: avoid the scaler's scratch image,
        // neighbourhood setup and per-pixel coordinate divisions.
        if(filter==SpriteFilter::none) {
            result.resize(background.size());
            for(size_t i=0;i<result.size();++i)
                result[i]=!protected_pixels.empty()&&protected_pixels[i]?background[i]:blend(sprites[i],background[i]);
            return result;
        }
        const auto factor=sprite_factor(filter);
        filtered.resize(background.size()*factor*factor); result.resize(filtered.size());
        if(filter==SpriteFilter::scalefx) ScaleFX::scale(sprites,width,height,filtered,metric,strength,flags,tags);
        else for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const auto get=[&](int dx,int dy){return sprites[std::clamp(int(y)+dy,0,int(height)-1)*width+
                std::clamp(int(x)+dx,0,int(width)-1)];};
            std::array<uint32_t,9> block{};const auto E=get(0,0);block.fill(E);
            if(filter==SpriteFilter::epx || filter==SpriteFilter::scale3x) {
                const auto B=get(0,-1),D=get(-1,0),F=get(1,0),H=get(0,1);
                if(B!=H && D!=F) {
                    if(factor==2) block={D==B?D:E,B==F?F:E,D==H?D:E,H==F?F:E};
                    else {
                        const auto A=get(-1,-1),C=get(1,-1),G=get(-1,1),I=get(1,1);
                        block={D==B?D:E,((D==B && E!=C)||(B==F && E!=A))?B:E,B==F?F:E,
                            ((D==B && E!=G)||(D==H && E!=A))?D:E,E,
                            ((B==F && E!=I)||(H==F && E!=C))?F:E,D==H?D:E,
                            ((D==H && E!=I)||(H==F && E!=G))?H:E,H==F?F:E};
                    }
                }
            } else if(filter==SpriteFilter::sai || filter==SpriteFilter::super_sai) {
                const auto b=filter==SpriteFilter::sai?SaI::sai(get):SaI::super_sai(get);
                std::copy(b.begin(),b.end(),block.begin());
            }
            for(unsigned j=0;j<factor*factor;++j)filtered[(y*factor+j/factor)*width*factor+x*factor+j%factor]=block[j];
        }
        for(unsigned y=0;y<height*factor;++y)for(unsigned x=0;x<width*factor;++x) {
            const auto i=y*width*factor+x;const auto fg=filtered[i],bg=background[(y/factor)*width+x/factor];
            if(!protected_pixels.empty() && protected_pixels[(y/factor)*width+x/factor]) {result[i]=bg;continue;}
            result[i]=blend(fg,bg);
        }
        return result;
    }
private:
    std::vector<uint32_t> filtered,result;
    std::vector<ScaleFX::vec4> metric,strength,flags,tags;
};
}

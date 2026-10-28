#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Frontend {
// Deproject the parked ROM road image into a reusable surface strip. Every
// output pixel is sampled from the original HWRoad RGB; no markings are drawn.
struct CustomRunwayArt {
    static constexpr unsigned width=128,height=144;
    static std::vector<uint32_t> extract(const std::vector<uint32_t>& source){
        if(source.size()!=320*224)throw std::runtime_error("Invalid native runway material");
        std::vector<uint32_t> result(width*height);unsigned usable=0;
        for(unsigned y=0;y<height;++y){const auto row=source.data()+(80+y)*320;
            unsigned left=160,right=160;
            while(left && row[left-1]!=row[0])--left;
            while(right<319 && row[right+1]!=row[319])++right;
            if(right-left>=8)++usable;
            for(unsigned x=0;x<width;++x)result[y*width+x]=row[left+x*(right-left)/(width-1)];
        }
        if(usable<height/2)throw std::runtime_error("Original runway was not visible at the material frontier");
        return result;
    }
};
}

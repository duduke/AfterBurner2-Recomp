#pragma once
#include "hwvideo/wide_scene.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

// Independent evidence for the additional flat-color fallback. Retain the
// original same-row index gate; a cross-row donor must be observed inside the
// same partial clip, use the identical road color word, and every observed
// native ground pen for that word must have exactly the same palette value.
inline std::array<uint8_t,224> wide_ground_fill_witness(
        const Presentation::WideScene::View& before,std::span<const Presentation::SceneClip> clips){
    std::array<uint8_t,224> result{};
    for(const auto& clip:clips){
        const auto word=[&](unsigned i){return unsigned(clip.road[2*i])<<8|clip.road[2*i+1];};
        std::map<unsigned,uint8_t> observed;
        for(unsigned y=clip.top;y<=clip.bottom;++y){
            if(word(y)&0xa00)continue;
            auto& mask=observed[word(0x600+y)];
            for(unsigned x=before.origin;x<before.origin+320;++x){const unsigned i=y*before.width+x,pen=before.ground_indices[i];
                if(!before.protection[i]&&pen>=0x1700&&pen<0x1708)mask|=1u<<(pen-0x1700);
            }
        }
        for(unsigned y=clip.top;y<=clip.bottom;++y){
            if(word(y)&0xa00)continue;
            const auto mask=observed[word(0x600+y)];int color=-1;bool uniform=true;
            for(unsigned pen=0;pen<8;++pen)if(mask&(1u<<pen)){
                if(color<0)color=clip.palette[0x1700+pen];else if(color!=clip.palette[0x1700+pen])uniform=false;
            }
            if(uniform)result[y]=mask;
        }
    }
    return result;
}
inline bool valid_wide_ground_change(const Presentation::WideScene::View& before,
                                     const Presentation::WideScene::View& after,unsigned i,
                                     std::span<const uint8_t> fill_witness={}){
    const auto a=before.ground_indices[i],b=after.ground_indices[i];
    if(a>=0x1700&&a<0x1708&&b>=0x1720&&b<0x1730)return true;
    const unsigned y=i/before.width;
    if(before.width<=320||a<0x1720||a>=0x1730||b<0x1700||b>=0x1708)return false;
    const auto row=before.ground_indices.subspan(y*before.width+before.origin,320);
    if(std::find(row.begin(),row.end(),b)!=row.end())return true;
    return fill_witness.size()==224&&(fill_witness[y]&(1u<<(b-0x1700)));
}

inline void save_wide_ground_fixture(const Presentation::WideScene::View& v,const std::string& path){
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path,std::ios::binary);file.write("AB2WIDE1",8);uint32_t dims[]{v.width,224,uint32_t(v.fragments.size())};
    file.write(reinterpret_cast<const char*>(dims),sizeof(dims));
    for(auto data:{v.background,v.sprites,v.heads})file.write(reinterpret_cast<const char*>(data.data()),data.size_bytes());
    file.write(reinterpret_cast<const char*>(v.protection.data()),v.protection.size_bytes());
    file.write(reinterpret_cast<const char*>(v.fragments.data()),v.fragments.size_bytes());
    if(!file)throw std::runtime_error("Cannot save wide ground GPU fixture");
}

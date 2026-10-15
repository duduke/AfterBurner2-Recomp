#pragma once
#include "hwvideo/wide_scene.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

// Sky correction may replace plain road-0 fill. The reverse operation is
// restricted to the enhanced wide plane and must copy an existing native-row index.
inline bool valid_wide_ground_change(const Presentation::WideScene::View& before,
                                     const Presentation::WideScene::View& after,unsigned i){
    const auto a=before.ground_indices[i],b=after.ground_indices[i];
    if(a>=0x1700&&a<0x1708&&b>=0x1720&&b<0x1730)return true;
    const unsigned y=i/before.width;
    if(before.width<=320||a<0x1720||a>=0x1730||b<0x1700||b>=0x1708)return false;
    const auto row=before.ground_indices.subspan(y*before.width+before.origin,320);
    return std::find(row.begin(),row.end(),b)!=row.end();
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

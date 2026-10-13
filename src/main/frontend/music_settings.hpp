#pragma once
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>
namespace Frontend {
inline constexpr std::array<const char*,6> music_titles{"MAXIMUM POWER","FINAL TAKE OFF","RED OUT","SUPER STRIPE","AFTER BURNER","CITY 202"};
inline constexpr std::array<const char*,6> music_keys{"maximum_power","final_take_off","red_out","super_stripe","after_burner","city_202"};
struct MusicSettings {
    int music=100,effects=100;std::array<int,6> track_volume{100,100,100,100,100,100};std::array<std::string,6> files{};
    bool operator==(const MusicSettings&)const=default;
};
inline bool music_filename(const std::string& value) {
    return value.find('\0')==std::string::npos && (value.empty() || (value!="." && value!=".." && value.find_first_of("/\\\r\n")==std::string::npos));
}
inline MusicSettings load_music_settings(const std::string& path) {
    MusicSettings s;std::ifstream file(path);std::string line;if(!std::getline(file,line) || line!="AB2AUDIO1")return s;
    while(std::getline(file,line)) {
        std::istringstream row(line);std::string key,filename,extra;int value;
        if(!(row>>key>>value) || value<0 || value>100)continue;
        if(key=="music" || key=="effects") {if(row>>extra)continue;(key=="music"?s.music:s.effects)=value;continue;}
        if(!(row>>std::quoted(filename)) || !music_filename(filename) || row>>extra)continue;
        for(unsigned i=0;i<6;++i)if(key==music_keys[i]){s.track_volume[i]=value;s.files[i]=filename;}
    }
    return s;
}
inline bool save_music_settings(const std::string& path,const MusicSettings& s) {
    if(path.empty() || s.music<0 || s.music>100 || s.effects<0 || s.effects>100)return false;
    for(unsigned i=0;i<6;++i)if(s.track_volume[i]<0 || s.track_volume[i]>100 || !music_filename(s.files[i]))return false;
    const auto tmp=path+".tmp";std::ofstream file(tmp,std::ios::trunc);file<<"AB2AUDIO1\nmusic "<<s.music<<"\neffects "<<s.effects<<'\n';
    for(unsigned i=0;i<6;++i)file<<music_keys[i]<<' '<<s.track_volume[i]<<' '<<std::quoted(s.files[i])<<'\n';
    file.close();std::error_code ec;if(file)std::filesystem::rename(tmp,path,ec);return file && !ec;
}
inline std::string music_sort_key(std::string value){for(auto& ch:value)ch=char(std::tolower(static_cast<unsigned char>(ch)));return value;}
inline std::vector<std::string> soundtrack_files(const std::filesystem::path& root) {
    std::vector<std::string> result;std::error_code ec;
    auto canonical=std::filesystem::weakly_canonical(root,ec);if(ec)return result;
    for(auto it=std::filesystem::directory_iterator(root,ec);!ec && it!=std::filesystem::directory_iterator();it.increment(ec)) {
        if(!it->is_regular_file(ec) || ec)continue;
        const auto real=std::filesystem::weakly_canonical(it->path(),ec);if(ec || real.parent_path()!=canonical)continue;
        const auto name=it->path().filename().string(),ext=music_sort_key(it->path().extension().string());
        if(music_filename(name) && (ext==".wav" || ext==".flac"))result.push_back(name);
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){auto x=music_sort_key(a),y=music_sort_key(b);return x==y?a<b:x<y;});return result;
}
}

#pragma once
#include <SDL3/SDL.h>
#include "music_settings.hpp"
#include "../../third_party/miniz/miniz.h"
#include <set>
#include <memory>
#include <string_view>
#include <stdexcept>

namespace Frontend {
// Private, immutable imports: never replace a file a decoder/renderer may own.
struct ContentImportResult {std::vector<std::string> paths;std::string error;};
inline std::filesystem::path unused_import_path(const std::filesystem::path& root,const std::string& stem,const std::string& extension) {
    for(unsigned i=0;i<10000;++i) {
        auto path=root/(stem+(i?" ("+std::to_string(i)+")":"")+extension);
        if(!std::filesystem::exists(path))return path;
    }
    throw std::runtime_error("IMPORT LIBRARY FULL");
}
inline void copy_import(const std::string& source,const std::filesystem::path& destination,size_t limit) {
    auto* raw=SDL_IOFromFile(source.c_str(),"rb");
    if(!raw)throw std::runtime_error("CANNOT READ SELECTED FILE");
    std::unique_ptr<SDL_IOStream,decltype(&SDL_CloseIO)> input(raw,SDL_CloseIO);
    std::ofstream out(destination,std::ios::binary|std::ios::trunc);
    char buffer[32768];size_t total=0;
    for(;;) {
        auto count=SDL_ReadIO(input.get(),buffer,sizeof(buffer));
        if(!count){if(SDL_GetIOStatus(input.get())!=SDL_IO_STATUS_EOF)throw std::runtime_error("FILE READ FAILED");break;}
        if(count>limit-total)throw std::runtime_error("IMPORT FILE TOO LARGE");
        total+=count;out.write(buffer,count);if(!out)throw std::runtime_error("IMPORT STORAGE UNAVAILABLE");
    }
    out.close();if(!out || !total)throw std::runtime_error("EMPTY FILE OR STORAGE UNAVAILABLE");
}
inline ContentImportResult import_music_file(const std::string& source,const std::string& name,const std::filesystem::path& root) {
    std::filesystem::path temporary;
    try {
        if(!music_filename(name) || name.empty())throw std::runtime_error("INVALID MUSIC FILENAME");
        const auto extension=music_sort_key(std::filesystem::path(name).extension().string());
        if(extension!=".wav" && extension!=".flac")throw std::runtime_error("CHOOSE WAV OR FLAC FILES");
        std::filesystem::create_directories(root);
        const auto destination=unused_import_path(root,std::filesystem::path(name).stem().string(),extension);
        temporary=destination;temporary+=".importing";
        copy_import(source,temporary,256ull*1024*1024);
        std::ifstream in(temporary,std::ios::binary);char magic[12]{};in.read(magic,sizeof(magic));
        const bool valid=extension==".flac"?std::string_view(magic,4)=="fLaC":
            std::string_view(magic,4)=="RIFF" && std::string_view(magic+8,4)=="WAVE";
        if(!valid)throw std::runtime_error("FILE IS NOT WAV OR FLAC AUDIO");
        in.close();
        std::filesystem::rename(temporary,destination);
        return {{destination.string()}, {}};
    } catch(const std::exception& e) {std::error_code ec;if(!temporary.empty())std::filesystem::remove(temporary,ec);return {{},e.what()};}
}
inline bool safe_pack_path(const std::string& name) {
    if(name.empty() || name.front()=='/' || name.find("//")!=std::string::npos || name.find_first_of("\\:")!=std::string::npos)return false;
    for(unsigned char c:name)if(c<32 || c==127)return false;
    for(const auto& part:std::filesystem::path(name))if(part==".." || part==".")return false;
    return true;
}
inline ContentImportResult import_shader_pack(const std::string& source,const std::filesystem::path& root) {
    std::filesystem::path staging,archive;
    mz_zip_archive zip{};bool opened=false;
    try {
        std::filesystem::create_directories(root);
        auto destination=unused_import_path(root,"pack","");
        staging=destination;staging+=".importing";archive=destination;archive+=".zip.importing";
        // A previous interrupted operation owns no published content.
        std::filesystem::remove_all(staging);std::filesystem::create_directory(staging);
        copy_import(source,archive,128ull*1024*1024);
        opened=mz_zip_reader_init_file(&zip,archive.string().c_str(),0);
        if(!opened)throw std::runtime_error("INVALID SHADER ZIP");
        auto count=mz_zip_reader_get_num_files(&zip);
        if(count>4096)throw std::runtime_error("TOO MANY SHADER PACK FILES");
        uint64_t total=0;std::set<std::string> names;std::vector<std::string> presets;
        for(mz_uint i=0;i<count;++i) {
            mz_zip_archive_file_stat stat{};
            if(!mz_zip_reader_file_stat(&zip,i,&stat))throw std::runtime_error("INVALID ZIP ENTRY");
            auto length=mz_zip_reader_get_filename(&zip,i,nullptr,0);
            if(!length || length>1024)throw std::runtime_error("INVALID ZIP FILENAME");
            std::string name(length,'\0');mz_zip_reader_get_filename(&zip,i,name.data(),length);name.pop_back();
            if(!safe_pack_path(name) || name.find('\0')!=std::string::npos ||
               ((stat.m_external_attr>>16)&0170000)==0120000 || !names.insert(music_sort_key(name)).second)
                throw std::runtime_error("UNSAFE OR DUPLICATE ZIP PATH");
            auto output=staging/std::filesystem::path(name);
            if(mz_zip_reader_is_file_a_directory(&zip,i)){std::filesystem::create_directories(output);continue;}
            if(!mz_zip_reader_is_file_supported(&zip,i) || stat.m_uncomp_size>32ull*1024*1024 ||
               stat.m_uncomp_size>256ull*1024*1024-total)throw std::runtime_error("UNSUPPORTED OR OVERSIZED SHADER PACK");
            total+=stat.m_uncomp_size;
            std::filesystem::create_directories(output.parent_path());
            if(!mz_zip_reader_extract_to_file(&zip,i,output.string().c_str(),0))throw std::runtime_error("SHADER ZIP DATA OR CRC ERROR");
            if(music_sort_key(output.extension().string())==".slangp")presets.push_back(name);
        }
        if(presets.empty())throw std::runtime_error("ZIP CONTAINS NO .SLANGP PRESETS");
        mz_zip_reader_end(&zip);opened=false;
        std::filesystem::remove(archive);std::filesystem::rename(staging,destination);
        std::sort(presets.begin(),presets.end());for(auto& preset:presets)preset=(destination/preset).string();
        return {presets,{}};
    } catch(const std::exception& e) {
        if(opened)mz_zip_reader_end(&zip);
        std::error_code ec;if(!staging.empty())std::filesystem::remove_all(staging,ec);if(!archive.empty())std::filesystem::remove(archive,ec);
        return {{},e.what()};
    }
}
inline std::vector<std::string> imported_shader_presets(const std::filesystem::path& root) {
    std::vector<std::string> paths;std::error_code ec;
    for(std::filesystem::recursive_directory_iterator it(root,ec),end;!ec && it!=end;it.increment(ec)) {
        if(it->is_directory(ec) && it->path().filename().string().ends_with(".importing")){it.disable_recursion_pending();continue;}
        if(it->is_regular_file(ec) && music_sort_key(it->path().extension().string())==".slangp")paths.push_back(it->path().string());
    }
    std::sort(paths.begin(),paths.end());return paths;
}
}

#include "frontend/content_import.hpp"
#include <iostream>
#include <thread>
namespace {
SDL_DialogFileCallback callback{};void* userdata{};bool multiple{};
void fake_picker(SDL_DialogFileCallback cb,void* data,SDL_Window*,const SDL_DialogFileFilter*,int,const char*,bool multi) {
    callback=cb;userdata=data;multiple=multi;
}
}
#define SDL_ShowOpenFileDialog fake_picker
#include "frontend/android_content_import.hpp"
#undef SDL_ShowOpenFileDialog
void require(bool value){if(!value)throw std::runtime_error("Picker lifecycle assertion failed");}
void picker_lifecycle(const std::filesystem::path& root) {
    using Imports=Frontend::AndroidContentImports;
    auto await=[](Imports& imports){
        for(int i=0;i<5000;++i){if(auto result=imports.poll())return *result;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        throw std::runtime_error("Picker worker timed out");
    };
    Imports imports(root);
    require(imports.choose(Imports::Kind::music,nullptr) && multiple);
    require(!imports.choose(Imports::Kind::shaders,nullptr));
    const char* cancelled[]{nullptr};callback(userdata,cancelled,0);
    require(await(imports).cancelled);
    require(imports.choose(Imports::Kind::shaders,nullptr) && !multiple);
    callback(userdata,nullptr,0);require(!await(imports).content.error.empty());
    require(imports.choose(Imports::Kind::music,nullptr));
    auto good=(root.parent_path()/"picked.flac").string(),bad=(root.parent_path()/"missing.wav").string();
    {std::ofstream out(good,std::ios::binary);out<<"fLaCtest";}
    const char* selected[]{good.c_str(),bad.c_str(),nullptr};callback(userdata,selected,0);
    auto result=await(imports);require(result.imported==1 && result.failed==1 && result.content.paths.size()==1);
    require(std::filesystem::exists(result.content.paths[0]));
    require(imports.choose(Imports::Kind::music,nullptr));
    std::vector<const char*> too_many(66,good.c_str());too_many.back()=nullptr;
    callback(userdata,too_many.data(),0);require(!await(imports).content.error.empty());
    {Imports abandoned(root);require(abandoned.choose(Imports::Kind::music,nullptr));}
    callback(userdata,selected,0); // Weak callback after owner destruction is harmless.
    {Imports joining(root);require(joining.choose(Imports::Kind::music,nullptr));callback(userdata,selected,0);joining.poll();}
    require(std::filesystem::exists(root/"soundtrack"/"picked (1).flac"));
    std::cout<<"Picker lifecycle: cancellation, errors, busy guard, multi-select, partial success, selection limit, late callback and worker shutdown passed\n";
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="lifecycle"){picker_lifecycle(argv[2]);return 0;}
    if(argc!=5)return 2;
    const auto result=std::string(argv[1])=="music"?
        Frontend::import_music_file(argv[2],argv[4],argv[3]):Frontend::import_shader_pack(argv[2],argv[3]);
    if(!result.error.empty()){std::cerr<<result.error<<'\n';return 1;}
    for(const auto& path:result.paths)std::cout<<path<<'\n';
    auto scanned=Frontend::imported_shader_presets(argv[3]);
    if(std::string(argv[1])!="music" && scanned.size()<result.paths.size())return 3;
    return 0;
}

#pragma once
// SDL's Android picker returns content:// URIs. Only SDL IO opens these; the
// native decoders and shader library see immutable private filesystem copies.
#include "content_import.hpp"
#include <future>
#include <mutex>
#include <optional>
#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>
#endif
namespace Frontend {
inline std::string android_import_name(const std::string& uri) {
#ifdef SDL_PLATFORM_ANDROID
    auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(!env || !activity)return {};
    auto type=env->GetObjectClass(activity);
    auto method=env->GetMethodID(type,"getImportDisplayName","(Ljava/lang/String;)[B");
    auto input=env->NewStringUTF(uri.c_str());
    auto output=method?static_cast<jbyteArray>(env->CallObjectMethod(activity,method,input)):nullptr;
    std::string result;
    if(env->ExceptionCheck())env->ExceptionClear();
    else if(output){result.resize(env->GetArrayLength(output));env->GetByteArrayRegion(output,0,result.size(),reinterpret_cast<jbyte*>(result.data()));}
    if(output)env->DeleteLocalRef(output);env->DeleteLocalRef(input);env->DeleteLocalRef(type);env->DeleteLocalRef(activity);
    return result;
#else
    return std::filesystem::path(uri).filename().string();
#endif
}
class AndroidContentImports {
public:
    enum class Kind { music, shaders };
    struct Result {Kind kind;ContentImportResult content;unsigned imported{},failed{};bool cancelled{};};
private:
    struct Selection {std::vector<std::pair<std::string,std::string>> files;std::string error;};
    struct State {std::mutex mutex;bool active{};std::optional<Selection> selection;};
    std::shared_ptr<State> state=std::make_shared<State>();
    std::filesystem::path root;
    Kind kind{};
    std::future<Result> work;
    static void SDLCALL selected(void* userdata,const char* const* files,int) {
        std::unique_ptr<std::weak_ptr<State>> request(static_cast<std::weak_ptr<State>*>(userdata));
        auto state=request->lock();if(!state)return;
        Selection selection;
        if(!files)selection.error="ANDROID FILE PICKER UNAVAILABLE";
        else for(unsigned i=0;files[i];++i){
            if(i>=64){selection.error="SELECT AT MOST 64 FILES";selection.files.clear();break;}
            selection.files.emplace_back(files[i],android_import_name(files[i]));
        }
        std::lock_guard lock(state->mutex);state->selection=std::move(selection);
    }
public:
    explicit AndroidContentImports(std::filesystem::path root):root(std::move(root)){}
    // std::future joins before SDL_Quit, including while an import is in flight.
    bool choose(Kind requested,SDL_Window* window) {
        {std::lock_guard lock(state->mutex);if(state->active)return false;
        kind=requested;state->active=true;}
        static const SDL_DialogFileFilter music{"WAV / FLAC music","wav;flac"},shaders{"Shader pack ZIP","zip"};
        SDL_ShowOpenFileDialog(selected,new std::weak_ptr(state),window,requested==Kind::shaders?&shaders:&music,1,nullptr,requested==Kind::music);
        return true;
    }
    std::optional<Result> poll() {
        if(work.valid()) {
            if(work.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
            auto result=work.get();std::lock_guard lock(state->mutex);state->active=false;return result;
        }
        std::optional<Selection> selection;
        {std::lock_guard lock(state->mutex);selection=std::move(state->selection);state->selection.reset();}
        if(!selection)return {};
        if(!selection->error.empty() || selection->files.empty()) {
            std::lock_guard lock(state->mutex);state->active=false;
            return Result{kind,{{},selection->error},0,0,selection->error.empty()};
        }
        work=std::async(std::launch::async,[root=root,kind=kind,selection=std::move(*selection)] {
            Result result{kind,{}};
            for(const auto& [uri,name]:selection.files) {
                auto imported=kind==Kind::music?import_music_file(uri,name,root/"soundtrack"):import_shader_pack(uri,root/"shaders");
                if(imported.error.empty()){++result.imported;result.content.paths.insert(result.content.paths.end(),imported.paths.begin(),imported.paths.end());}
                else {++result.failed;result.content.error=imported.error;}
            }
            return result;
        });
        return {};
    }
};
}

#pragma once
#include <SDL3/SDL.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <mutex>
#include <stdexcept>
#include <string>

// The optional runtime is loaded only when spatial audio is requested.
// An explicit path is authoritative: failures must fall back to SDL, never
// silently load another DLL from the process search path.
namespace Frontend::OpenALRuntime {
struct Api {
    SDL_SharedObject* module{};
    std::mutex mutex;
    decltype(&::alBufferData) alBufferData{};
    decltype(&::alDeleteBuffers) alDeleteBuffers{};
    decltype(&::alDeleteSources) alDeleteSources{};
    decltype(&::alDistanceModel) alDistanceModel{};
    decltype(&::alDopplerFactor) alDopplerFactor{};
    decltype(&::alGenBuffers) alGenBuffers{};
    decltype(&::alGenSources) alGenSources{};
    decltype(&::alGetError) alGetError{};
    decltype(&::alGetIntegerv) alGetIntegerv{};
    decltype(&::alGetProcAddress) alGetProcAddress{};
    decltype(&::alGetSourcef) alGetSourcef{};
    decltype(&::alGetSourcei) alGetSourcei{};
    decltype(&::alGetString) alGetString{};
    decltype(&::alIsExtensionPresent) alIsExtensionPresent{};
    decltype(&::alListener3f) alListener3f{};
    decltype(&::alListenerfv) alListenerfv{};
    decltype(&::alSource3f) alSource3f{};
    decltype(&::alSourcePausev) alSourcePausev{};
    decltype(&::alSourcePlayv) alSourcePlayv{};
    decltype(&::alSourceQueueBuffers) alSourceQueueBuffers{};
    decltype(&::alSourceRewind) alSourceRewind{};
    decltype(&::alSourceRewindv) alSourceRewindv{};
    decltype(&::alSourceStopv) alSourceStopv{};
    decltype(&::alSourceUnqueueBuffers) alSourceUnqueueBuffers{};
    decltype(&::alSourcef) alSourcef{};
    decltype(&::alSourcei) alSourcei{};
    decltype(&::alcCloseDevice) alcCloseDevice{};
    decltype(&::alcCreateContext) alcCreateContext{};
    decltype(&::alcDestroyContext) alcDestroyContext{};
    decltype(&::alcGetIntegerv) alcGetIntegerv{};
    decltype(&::alcGetProcAddress) alcGetProcAddress{};
    decltype(&::alcGetString) alcGetString{};
    decltype(&::alcIsExtensionPresent) alcIsExtensionPresent{};
    decltype(&::alcMakeContextCurrent) alcMakeContextCurrent{};
    decltype(&::alcOpenDevice) alcOpenDevice{};

    void load() {
        std::lock_guard lock(mutex);
        if (module) return;
        const char* override_path=SDL_getenv("AB2_OPENAL_RUNTIME");
        std::string path;
        if (override_path && *override_path) path=override_path;
        else {
            const char* base=SDL_GetBasePath();
            if (!base) throw std::runtime_error("OpenAL application directory unavailable");
#ifdef _WIN32
            path=std::string(base)+"OpenAL32.dll";
#elif defined(__APPLE__)
            path=std::string(base)+"libopenal.1.dylib";
#else
            path=std::string(base)+"libopenal.so.1";
#endif
        }
        SDL_SharedObject* candidate=SDL_LoadObject(path.c_str());
        if (!candidate) throw std::runtime_error("Optional OpenAL runtime unavailable: "+std::string(SDL_GetError()));
        try {
            auto next_alBufferData=reinterpret_cast<decltype(alBufferData)>(SDL_LoadFunction(candidate,"alBufferData"));
            if (!next_alBufferData) throw std::runtime_error("Optional OpenAL runtime missing alBufferData");
            auto next_alDeleteBuffers=reinterpret_cast<decltype(alDeleteBuffers)>(SDL_LoadFunction(candidate,"alDeleteBuffers"));
            if (!next_alDeleteBuffers) throw std::runtime_error("Optional OpenAL runtime missing alDeleteBuffers");
            auto next_alDeleteSources=reinterpret_cast<decltype(alDeleteSources)>(SDL_LoadFunction(candidate,"alDeleteSources"));
            if (!next_alDeleteSources) throw std::runtime_error("Optional OpenAL runtime missing alDeleteSources");
            auto next_alDistanceModel=reinterpret_cast<decltype(alDistanceModel)>(SDL_LoadFunction(candidate,"alDistanceModel"));
            if (!next_alDistanceModel) throw std::runtime_error("Optional OpenAL runtime missing alDistanceModel");
            auto next_alDopplerFactor=reinterpret_cast<decltype(alDopplerFactor)>(SDL_LoadFunction(candidate,"alDopplerFactor"));
            if (!next_alDopplerFactor) throw std::runtime_error("Optional OpenAL runtime missing alDopplerFactor");
            auto next_alGenBuffers=reinterpret_cast<decltype(alGenBuffers)>(SDL_LoadFunction(candidate,"alGenBuffers"));
            if (!next_alGenBuffers) throw std::runtime_error("Optional OpenAL runtime missing alGenBuffers");
            auto next_alGenSources=reinterpret_cast<decltype(alGenSources)>(SDL_LoadFunction(candidate,"alGenSources"));
            if (!next_alGenSources) throw std::runtime_error("Optional OpenAL runtime missing alGenSources");
            auto next_alGetError=reinterpret_cast<decltype(alGetError)>(SDL_LoadFunction(candidate,"alGetError"));
            if (!next_alGetError) throw std::runtime_error("Optional OpenAL runtime missing alGetError");
            auto next_alGetIntegerv=reinterpret_cast<decltype(alGetIntegerv)>(SDL_LoadFunction(candidate,"alGetIntegerv"));
            if (!next_alGetIntegerv) throw std::runtime_error("Optional OpenAL runtime missing alGetIntegerv");
            auto next_alGetProcAddress=reinterpret_cast<decltype(alGetProcAddress)>(SDL_LoadFunction(candidate,"alGetProcAddress"));
            if (!next_alGetProcAddress) throw std::runtime_error("Optional OpenAL runtime missing alGetProcAddress");
            auto next_alGetSourcef=reinterpret_cast<decltype(alGetSourcef)>(SDL_LoadFunction(candidate,"alGetSourcef"));
            if (!next_alGetSourcef) throw std::runtime_error("Optional OpenAL runtime missing alGetSourcef");
            auto next_alGetSourcei=reinterpret_cast<decltype(alGetSourcei)>(SDL_LoadFunction(candidate,"alGetSourcei"));
            if (!next_alGetSourcei) throw std::runtime_error("Optional OpenAL runtime missing alGetSourcei");
            auto next_alGetString=reinterpret_cast<decltype(alGetString)>(SDL_LoadFunction(candidate,"alGetString"));
            if (!next_alGetString) throw std::runtime_error("Optional OpenAL runtime missing alGetString");
            auto next_alIsExtensionPresent=reinterpret_cast<decltype(alIsExtensionPresent)>(SDL_LoadFunction(candidate,"alIsExtensionPresent"));
            if (!next_alIsExtensionPresent) throw std::runtime_error("Optional OpenAL runtime missing alIsExtensionPresent");
            auto next_alListener3f=reinterpret_cast<decltype(alListener3f)>(SDL_LoadFunction(candidate,"alListener3f"));
            if (!next_alListener3f) throw std::runtime_error("Optional OpenAL runtime missing alListener3f");
            auto next_alListenerfv=reinterpret_cast<decltype(alListenerfv)>(SDL_LoadFunction(candidate,"alListenerfv"));
            if (!next_alListenerfv) throw std::runtime_error("Optional OpenAL runtime missing alListenerfv");
            auto next_alSource3f=reinterpret_cast<decltype(alSource3f)>(SDL_LoadFunction(candidate,"alSource3f"));
            if (!next_alSource3f) throw std::runtime_error("Optional OpenAL runtime missing alSource3f");
            auto next_alSourcePausev=reinterpret_cast<decltype(alSourcePausev)>(SDL_LoadFunction(candidate,"alSourcePausev"));
            if (!next_alSourcePausev) throw std::runtime_error("Optional OpenAL runtime missing alSourcePausev");
            auto next_alSourcePlayv=reinterpret_cast<decltype(alSourcePlayv)>(SDL_LoadFunction(candidate,"alSourcePlayv"));
            if (!next_alSourcePlayv) throw std::runtime_error("Optional OpenAL runtime missing alSourcePlayv");
            auto next_alSourceQueueBuffers=reinterpret_cast<decltype(alSourceQueueBuffers)>(SDL_LoadFunction(candidate,"alSourceQueueBuffers"));
            if (!next_alSourceQueueBuffers) throw std::runtime_error("Optional OpenAL runtime missing alSourceQueueBuffers");
            auto next_alSourceRewind=reinterpret_cast<decltype(alSourceRewind)>(SDL_LoadFunction(candidate,"alSourceRewind"));
            if (!next_alSourceRewind) throw std::runtime_error("Optional OpenAL runtime missing alSourceRewind");
            auto next_alSourceRewindv=reinterpret_cast<decltype(alSourceRewindv)>(SDL_LoadFunction(candidate,"alSourceRewindv"));
            if (!next_alSourceRewindv) throw std::runtime_error("Optional OpenAL runtime missing alSourceRewindv");
            auto next_alSourceStopv=reinterpret_cast<decltype(alSourceStopv)>(SDL_LoadFunction(candidate,"alSourceStopv"));
            if (!next_alSourceStopv) throw std::runtime_error("Optional OpenAL runtime missing alSourceStopv");
            auto next_alSourceUnqueueBuffers=reinterpret_cast<decltype(alSourceUnqueueBuffers)>(SDL_LoadFunction(candidate,"alSourceUnqueueBuffers"));
            if (!next_alSourceUnqueueBuffers) throw std::runtime_error("Optional OpenAL runtime missing alSourceUnqueueBuffers");
            auto next_alSourcef=reinterpret_cast<decltype(alSourcef)>(SDL_LoadFunction(candidate,"alSourcef"));
            if (!next_alSourcef) throw std::runtime_error("Optional OpenAL runtime missing alSourcef");
            auto next_alSourcei=reinterpret_cast<decltype(alSourcei)>(SDL_LoadFunction(candidate,"alSourcei"));
            if (!next_alSourcei) throw std::runtime_error("Optional OpenAL runtime missing alSourcei");
            auto next_alcCloseDevice=reinterpret_cast<decltype(alcCloseDevice)>(SDL_LoadFunction(candidate,"alcCloseDevice"));
            if (!next_alcCloseDevice) throw std::runtime_error("Optional OpenAL runtime missing alcCloseDevice");
            auto next_alcCreateContext=reinterpret_cast<decltype(alcCreateContext)>(SDL_LoadFunction(candidate,"alcCreateContext"));
            if (!next_alcCreateContext) throw std::runtime_error("Optional OpenAL runtime missing alcCreateContext");
            auto next_alcDestroyContext=reinterpret_cast<decltype(alcDestroyContext)>(SDL_LoadFunction(candidate,"alcDestroyContext"));
            if (!next_alcDestroyContext) throw std::runtime_error("Optional OpenAL runtime missing alcDestroyContext");
            auto next_alcGetIntegerv=reinterpret_cast<decltype(alcGetIntegerv)>(SDL_LoadFunction(candidate,"alcGetIntegerv"));
            if (!next_alcGetIntegerv) throw std::runtime_error("Optional OpenAL runtime missing alcGetIntegerv");
            auto next_alcGetProcAddress=reinterpret_cast<decltype(alcGetProcAddress)>(SDL_LoadFunction(candidate,"alcGetProcAddress"));
            if (!next_alcGetProcAddress) throw std::runtime_error("Optional OpenAL runtime missing alcGetProcAddress");
            auto next_alcGetString=reinterpret_cast<decltype(alcGetString)>(SDL_LoadFunction(candidate,"alcGetString"));
            if (!next_alcGetString) throw std::runtime_error("Optional OpenAL runtime missing alcGetString");
            auto next_alcIsExtensionPresent=reinterpret_cast<decltype(alcIsExtensionPresent)>(SDL_LoadFunction(candidate,"alcIsExtensionPresent"));
            if (!next_alcIsExtensionPresent) throw std::runtime_error("Optional OpenAL runtime missing alcIsExtensionPresent");
            auto next_alcMakeContextCurrent=reinterpret_cast<decltype(alcMakeContextCurrent)>(SDL_LoadFunction(candidate,"alcMakeContextCurrent"));
            if (!next_alcMakeContextCurrent) throw std::runtime_error("Optional OpenAL runtime missing alcMakeContextCurrent");
            auto next_alcOpenDevice=reinterpret_cast<decltype(alcOpenDevice)>(SDL_LoadFunction(candidate,"alcOpenDevice"));
            if (!next_alcOpenDevice) throw std::runtime_error("Optional OpenAL runtime missing alcOpenDevice");
            alBufferData=next_alBufferData;
            alDeleteBuffers=next_alDeleteBuffers;
            alDeleteSources=next_alDeleteSources;
            alDistanceModel=next_alDistanceModel;
            alDopplerFactor=next_alDopplerFactor;
            alGenBuffers=next_alGenBuffers;
            alGenSources=next_alGenSources;
            alGetError=next_alGetError;
            alGetIntegerv=next_alGetIntegerv;
            alGetProcAddress=next_alGetProcAddress;
            alGetSourcef=next_alGetSourcef;
            alGetSourcei=next_alGetSourcei;
            alGetString=next_alGetString;
            alIsExtensionPresent=next_alIsExtensionPresent;
            alListener3f=next_alListener3f;
            alListenerfv=next_alListenerfv;
            alSource3f=next_alSource3f;
            alSourcePausev=next_alSourcePausev;
            alSourcePlayv=next_alSourcePlayv;
            alSourceQueueBuffers=next_alSourceQueueBuffers;
            alSourceRewind=next_alSourceRewind;
            alSourceRewindv=next_alSourceRewindv;
            alSourceStopv=next_alSourceStopv;
            alSourceUnqueueBuffers=next_alSourceUnqueueBuffers;
            alSourcef=next_alSourcef;
            alSourcei=next_alSourcei;
            alcCloseDevice=next_alcCloseDevice;
            alcCreateContext=next_alcCreateContext;
            alcDestroyContext=next_alcDestroyContext;
            alcGetIntegerv=next_alcGetIntegerv;
            alcGetProcAddress=next_alcGetProcAddress;
            alcGetString=next_alcGetString;
            alcIsExtensionPresent=next_alcIsExtensionPresent;
            alcMakeContextCurrent=next_alcMakeContextCurrent;
            alcOpenDevice=next_alcOpenDevice;
            module=candidate;
        } catch (...) { SDL_UnloadObject(candidate); throw; }
    }
    ~Api() { if (module) SDL_UnloadObject(module); }
};
inline Api& api() { static Api value; return value; }
}

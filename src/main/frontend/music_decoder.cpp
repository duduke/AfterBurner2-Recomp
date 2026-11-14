#include "frontend/music_decoder.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <cmath>
#include <algorithm>
#define DR_FLAC_IMPLEMENTATION
#include "../../third_party/dr_flac/dr_flac.h"
namespace Frontend {
std::shared_ptr<const DecodedMusic> decode_music(const std::string& path,std::string& error) try {
    constexpr uint64_t limit=256ull*1024*1024;
    if(std::filesystem::file_size(path)>limit)throw std::runtime_error("AUDIO FILE TOO LARGE");
    auto* io=SDL_IOFromFile(path.c_str(),"rb");if(!io)throw std::runtime_error("AUDIO FILE MISSING");
    unsigned char magic[4]{};const auto n=SDL_ReadIO(io,magic,4);SDL_CloseIO(io);
    SDL_AudioSpec source{};Uint8* bytes=nullptr;Uint32 length=0;std::vector<float> flac_data;
    const bool flac=n==4 && std::string_view(reinterpret_cast<char*>(magic),4)=="fLaC";
    if(flac) {
        auto* decoder=drflac_open_file(path.c_str(),nullptr);if(!decoder)throw std::runtime_error("INVALID FLAC FILE");
        std::unique_ptr<drflac,decltype(&drflac_close)> decoder_guard(decoder,drflac_close);
        const auto frames=decoder->totalPCMFrameCount;source={SDL_AUDIO_F32,int(decoder->channels),int(decoder->sampleRate)};
        if(source.channels<1 || source.channels>2 || source.freq<1 || frames>limit/sizeof(float)/source.channels ||
           double(frames)/source.freq*48000*8>limit)throw std::runtime_error("UNSUPPORTED FLAC SIZE OR CHANNELS");
        if(frames) {
            flac_data.resize(frames*source.channels);
            if(drflac_read_pcm_frames_f32(decoder,frames,flac_data.data())!=frames)throw std::runtime_error("TRUNCATED FLAC FILE");
        } else {
            // STREAMINFO permits an unknown total. Decode bounded chunks, then
            // resample the complete file once to preserve filter continuity.
            std::vector<float> block(4096*source.channels);
            while(const auto read=drflac_read_pcm_frames_f32(decoder,4096,block.data())) {
                const auto size=flac_data.size()+read*source.channels;
                if(size>limit/sizeof(float) || double(size)/source.channels/source.freq*48000*8>limit)throw std::runtime_error("DECODED AUDIO TOO LARGE");
                flac_data.insert(flac_data.end(),block.begin(),block.begin()+read*source.channels);
            }
        }
        bytes=reinterpret_cast<Uint8*>(flac_data.data());length=Uint32(flac_data.size()*sizeof(float));
    } else {
        if(!SDL_LoadWAV(path.c_str(),&source,&bytes,&length))throw std::runtime_error("INVALID WAV OR FLAC FILE");
    }
    struct Free {Uint8* data;bool owned;~Free(){if(owned)SDL_free(data);}} guard{bytes,!flac};
    if(source.channels<1 || source.channels>2 || source.freq<1 || !length)throw std::runtime_error("UNSUPPORTED AUDIO CHANNELS");
    const auto sample_bytes=SDL_AUDIO_BYTESIZE(source.format);
    if(!sample_bytes || double(length)/sample_bytes/source.channels/source.freq*48000*8>limit)throw std::runtime_error("DECODED AUDIO TOO LARGE");
    SDL_AudioSpec target{SDL_AUDIO_F32,2,48000};Uint8* converted=nullptr;int converted_length=0;
    if(!SDL_ConvertAudioSamples(&source,bytes,int(length),&target,&converted,&converted_length))throw std::runtime_error("AUDIO CONVERSION FAILED");
    Free converted_guard{converted,true};
    if(converted_length<8 || converted_length%8 || uint64_t(converted_length)>limit)throw std::runtime_error("EMPTY OR INVALID AUDIO");
    auto music=std::make_shared<DecodedMusic>();const auto* values=reinterpret_cast<const float*>(converted);
    music->stereo.assign(values,values+converted_length/sizeof(float));
    if(!std::all_of(music->stereo.begin(),music->stereo.end(),[](float x){return std::isfinite(x);}))throw std::runtime_error("INVALID AUDIO SAMPLES");
    return music;
} catch(const std::exception& e) {error=e.what();return {};}
std::shared_ptr<const DecodedMusic> decode_music_memory(const void* data,size_t size,std::string& error) try {
    constexpr uint64_t limit=256ull*1024*1024;
    if(size>limit)throw std::runtime_error("AUDIO FILE TOO LARGE");
    if(size<4)throw std::runtime_error("INVALID WAV OR FLAC FILE");
    SDL_AudioSpec source{};Uint8* bytes=nullptr;Uint32 length=0;std::vector<float> flac_data;
    const bool flac=std::string_view(static_cast<const char*>(data),4)=="fLaC";
    if(flac) {
        auto* decoder=drflac_open_memory(data,size,nullptr);if(!decoder)throw std::runtime_error("INVALID FLAC FILE");
        std::unique_ptr<drflac,decltype(&drflac_close)> decoder_guard(decoder,drflac_close);
        source={SDL_AUDIO_F32,int(decoder->channels),int(decoder->sampleRate)};
        if(source.channels<1 || source.channels>2 || source.freq<1)throw std::runtime_error("UNSUPPORTED FLAC SIZE OR CHANNELS");
        std::vector<float> block(4096*source.channels);
        while(const auto read=drflac_read_pcm_frames_f32(decoder,4096,block.data())) {
            const auto total=flac_data.size()+read*source.channels;
            if(total>limit/sizeof(float) || double(total)/source.channels/source.freq*48000*8>limit)throw std::runtime_error("DECODED AUDIO TOO LARGE");
            flac_data.insert(flac_data.end(),block.begin(),block.begin()+read*source.channels);
        }
        bytes=reinterpret_cast<Uint8*>(flac_data.data());length=Uint32(flac_data.size()*sizeof(float));
    } else {
        auto* io=SDL_IOFromConstMem(data,size);
        if(!io || !SDL_LoadWAV_IO(io,true,&source,&bytes,&length))throw std::runtime_error("INVALID WAV OR FLAC FILE");
    }
    struct Free {Uint8* data;bool owned;~Free(){if(owned)SDL_free(data);}} guard{bytes,!flac};
    if(source.channels<1 || source.channels>2 || source.freq<1 || !length)throw std::runtime_error("UNSUPPORTED AUDIO CHANNELS");
    const auto sample_bytes=SDL_AUDIO_BYTESIZE(source.format);
    if(!sample_bytes || double(length)/sample_bytes/source.channels/source.freq*48000*8>limit)throw std::runtime_error("DECODED AUDIO TOO LARGE");
    SDL_AudioSpec target{SDL_AUDIO_F32,2,48000};Uint8* converted=nullptr;int converted_length=0;
    if(!SDL_ConvertAudioSamples(&source,bytes,int(length),&target,&converted,&converted_length))throw std::runtime_error("AUDIO CONVERSION FAILED");
    Free converted_guard{converted,true};
    if(converted_length<8 || converted_length%8 || uint64_t(converted_length)>limit)throw std::runtime_error("EMPTY OR INVALID AUDIO");
    auto music=std::make_shared<DecodedMusic>();const auto* values=reinterpret_cast<const float*>(converted);
    music->stereo.assign(values,values+converted_length/sizeof(float));
    if(!std::all_of(music->stereo.begin(),music->stereo.end(),[](float x){return std::isfinite(x);}))throw std::runtime_error("INVALID AUDIO SAMPLES");
    return music;
} catch(const std::exception& e) {error=e.what();return {};}
}

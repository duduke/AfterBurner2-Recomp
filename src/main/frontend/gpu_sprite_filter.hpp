#pragma once
#include <SDL3/SDL.h>
#if !SDL_VERSION_ATLEAST(3,4,0)
#error "SDL GPU sprite presentation requires SDL 3.4 or newer"
#endif
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>
#include "frontend/sprite_filter.hpp"
#include "hwvideo/sprite_scene.hpp"
#include "frontend/scalers/gpu_shaders.hpp"

namespace Frontend {
// SDL GPU owns every scaling/composition pass. CPU reference filters are kept
// separately for validation and headless/software fallback.
class GPUSpriteScaler {
public:
    explicit GPUSpriteScaler(SDL_Renderer* renderer): renderer(renderer),device(SDL_GetGPURendererDevice(renderer)) {
        if(!device) throw std::runtime_error("Sprite shaders require SDL GPU renderer");
        const auto formats=SDL_GetGPUShaderFormats(device);
        const auto format=(formats&SDL_GPU_SHADERFORMAT_MSL)?SDL_GPU_SHADERFORMAT_MSL:SDL_GPU_SHADERFORMAT_SPIRV;
        using namespace GPUShaders;
        const std::array<std::span<const unsigned char>,6> binaries{sprite_pass0_spv,sprite_pass1_spv,sprite_pass2_spv,sprite_pass3_spv,sprite_pass4_spv,sprite_scene_spv};
        const char* sources[]{sprite_pass0_msl,sprite_pass1_msl,sprite_pass2_msl,sprite_pass3_msl,sprite_pass4_msl,sprite_scene_msl};
        try {
            for(unsigned i=0;i<6;++i) {
                SDL_GPUComputePipelineCreateInfo info{};
                info.format=format;
                info.code=format==SDL_GPU_SHADERFORMAT_MSL?reinterpret_cast<const Uint8*>(sources[i]):binaries[i].data();
                info.code_size=format==SDL_GPU_SHADERFORMAT_MSL?std::strlen(sources[i]):binaries[i].size();
                info.entrypoint=format==SDL_GPU_SHADERFORMAT_MSL?"main0":"main";
                info.num_readonly_storage_buffers=1;info.num_readwrite_storage_buffers=1;
                info.num_readwrite_storage_textures=i==5?0:1;info.num_uniform_buffers=1;
                info.threadcount_x=8;info.threadcount_y=8;info.threadcount_z=1;
                pipelines[i]=SDL_CreateGPUComputePipeline(device,&info);
                if(!pipelines[i]) fail("compile sprite shader");
            }
        } catch(...) {release();throw;}
    }
    ~GPUSpriteScaler(){release();}
    GPUSpriteScaler(const GPUSpriteScaler&)=delete;
    GPUSpriteScaler& operator=(const GPUSpriteScaler&)=delete;

    SDL_Texture* compose(std::span<const uint32_t> background,std::span<const uint32_t> sprites,
                         unsigned width,unsigned height,SpriteFilter filter,std::span<const uint8_t> protection={},
                         std::span<const uint32_t> heads={},std::span<const Presentation::Fragment> fragments={},
                         unsigned cloud_alpha=255,unsigned trail_alpha=255,unsigned explosion_alpha=255,unsigned afterburner_alpha=255) {
        if(!width || !height || background.size()!=size_t(width)*height || sprites.size()!=background.size() ||
           (!protection.empty() && protection.size()!=background.size()) ||
           unsigned(filter)>=sprite_filter_count) throw std::invalid_argument("Invalid GPU sprite planes/filter");
        if((!heads.empty() && heads.size()!=background.size()) || cloud_alpha>255 || trail_alpha>255 || explosion_alpha>255 || afterburner_alpha>255)throw std::invalid_argument("Invalid GPU scene");
        const auto factor=sprite_factor(filter);
        // Finish any renderer commands referencing the previous output before
        // submitting a new compute command buffer. No GPU-to-CPU readback.
        if(!SDL_FlushRenderer(renderer))fail("flush presentation");
        if(width!=w || height!=h || factor!=scale) resize(width,height,factor);
        const auto n=background.size();
        auto* data=static_cast<uint32_t*>(SDL_MapGPUTransferBuffer(device,upload,true));
        if(!data)fail("map sprite upload");
        std::memcpy(data,background.data(),n*4);std::memcpy(data+n,sprites.data(),n*4);
        for(size_t i=0;i<n;++i)data[2*n+i]=protection.empty()?0:protection[i];
        SDL_UnmapGPUTransferBuffer(device,upload);
        if(!heads.empty()){
            const size_t bytes=n*4+fragments.size_bytes();
            if(bytes>layer_capacity){
                if(layers)SDL_ReleaseGPUBuffer(device,layers);if(layer_upload)SDL_ReleaseGPUTransferBuffer(device,layer_upload);
                layer_capacity=Uint32(bytes);
                SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;info.size=layer_capacity;
                layers=SDL_CreateGPUBuffer(device,&info);
                SDL_GPUTransferBufferCreateInfo transfer{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,layer_capacity,0};layer_upload=SDL_CreateGPUTransferBuffer(device,&transfer);
                if(!layers || !layer_upload)fail("allocate sprite layers");
            }
            auto* packet=static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device,layer_upload,true));if(!packet)fail("map sprite layers");
            std::memcpy(packet,heads.data(),n*4);if(!fragments.empty())std::memcpy(packet+n*4,fragments.data(),fragments.size_bytes());
            SDL_UnmapGPUTransferBuffer(device,layer_upload);
        }
        auto* command=SDL_AcquireGPUCommandBuffer(device);if(!command)fail("acquire sprite command buffer");
        auto* copy=SDL_BeginGPUCopyPass(command);
        SDL_GPUTransferBufferLocation source{upload,0};SDL_GPUBufferRegion destination{input,0,Uint32(n*12)};
        SDL_UploadToGPUBuffer(copy,&source,&destination,true);if(!heads.empty()){
            SDL_GPUTransferBufferLocation from{layer_upload,0};SDL_GPUBufferRegion to{layers,0,Uint32(n*4+fragments.size_bytes())};SDL_UploadToGPUBuffer(copy,&from,&to,true);
        }
        SDL_EndGPUCopyPass(copy);
        if(!heads.empty()){
            struct SceneParams{int32_t w,h,clouds,trails,explosions,afterburner,padding[2];} scene{int32_t(w),int32_t(h),int32_t(cloud_alpha),int32_t(trail_alpha),int32_t(explosion_alpha),int32_t(afterburner_alpha),{0,0}};
            SDL_PushGPUComputeUniformData(command,0,&scene,sizeof(scene));
            SDL_GPUStorageBufferReadWriteBinding target{};target.buffer=input;
            auto* pass=SDL_BeginGPUComputePass(command,nullptr,0,&target,1);
            SDL_BindGPUComputePipeline(pass,pipelines[5]);SDL_BindGPUComputeStorageBuffers(pass,0,&layers,1);
            SDL_DispatchGPUCompute(pass,(w+7)/8,(h+7)/8,1);SDL_EndGPUComputePass(pass);
        }
        struct Params {int32_t w,h,filter,factor;} params{int32_t(w),int32_t(h),int32_t(filter),int32_t(scale)};
        SDL_PushGPUComputeUniformData(command,0,&params,sizeof(params));
        const auto dispatch=[&](unsigned index,bool first){
            SDL_GPUStorageTextureReadWriteBinding texture_binding{};texture_binding.texture=output;texture_binding.cycle=index==4;
            SDL_GPUStorageBufferReadWriteBinding buffer_binding{};buffer_binding.buffer=intermediate;buffer_binding.cycle=first;
            auto* pass=SDL_BeginGPUComputePass(command,&texture_binding,1,&buffer_binding,1);
            SDL_BindGPUComputePipeline(pass,pipelines[index]);SDL_BindGPUComputeStorageBuffers(pass,0,&input,1);
            const auto f=index==4?scale:1;
            SDL_DispatchGPUCompute(pass,(w*f+7)/8,(h*f+7)/8,1);SDL_EndGPUComputePass(pass);
        };
        if(filter==SpriteFilter::scalefx)for(unsigned i=0;i<4;++i)dispatch(i,i==0);
        dispatch(4,filter!=SpriteFilter::scalefx);
        if(!SDL_SubmitGPUCommandBuffer(command))fail("submit sprite shaders");
        return texture;
    }
private:
    SDL_Renderer* renderer{};SDL_GPUDevice* device{};
    std::array<SDL_GPUComputePipeline*,6> pipelines{};
    SDL_GPUBuffer *input{},*intermediate{};SDL_GPUTransferBuffer* upload{};
    SDL_GPUBuffer* layers{};SDL_GPUTransferBuffer* layer_upload{};Uint32 layer_capacity{};
    SDL_GPUTexture* output{};SDL_Texture* texture{};
    unsigned w{},h{},scale{};
    [[noreturn]] static void fail(const char* what){throw std::runtime_error(std::string("SDL GPU ")+what+": "+SDL_GetError());}
    void release_planes(){
        SDL_DestroyTexture(texture);texture=nullptr;
        if(output)SDL_ReleaseGPUTexture(device,output);output=nullptr;
        if(input)SDL_ReleaseGPUBuffer(device,input);input=nullptr;
        if(intermediate)SDL_ReleaseGPUBuffer(device,intermediate);intermediate=nullptr;
        if(upload)SDL_ReleaseGPUTransferBuffer(device,upload);upload=nullptr;
        w=h=scale=0;
    }
    void release(){release_planes();if(layers)SDL_ReleaseGPUBuffer(device,layers);if(layer_upload)SDL_ReleaseGPUTransferBuffer(device,layer_upload);for(auto*& pipeline:pipelines){if(pipeline)SDL_ReleaseGPUComputePipeline(device,pipeline);pipeline=nullptr;}}
    void resize(unsigned width,unsigned height,unsigned factor){
        release_planes();
        const auto n=width*height;
        SDL_GPUBufferCreateInfo buffer{};buffer.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ|SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;buffer.size=n*12;
        input=SDL_CreateGPUBuffer(device,&buffer);buffer.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ|SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;buffer.size=n*64;
        intermediate=SDL_CreateGPUBuffer(device,&buffer);
        SDL_GPUTransferBufferCreateInfo transfer{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,n*12,0};upload=SDL_CreateGPUTransferBuffer(device,&transfer);
        SDL_GPUTextureCreateInfo image{};image.type=SDL_GPU_TEXTURETYPE_2D;image.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        image.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        image.width=width*factor;image.height=height*factor;image.layer_count_or_depth=1;image.num_levels=1;
        output=SDL_CreateGPUTexture(device,&image);
        if(!input || !intermediate || !upload || !output)fail("allocate sprite resources");
        auto props=SDL_CreateProperties();
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_ABGR8888);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,width*factor);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,height*factor);
        SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER,output);
        texture=SDL_CreateTextureWithProperties(renderer,props);SDL_DestroyProperties(props);
        if(!texture)fail("wrap sprite output");
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST);
        w=width;h=height;scale=factor;
    }
};
}

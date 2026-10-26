#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CoreVideo.h>
#include "frontend/native_metal_sprite_filter.hpp"
#include "frontend/scalers/gpu_shaders.hpp"
#include <cstring>
#include <string_view>

namespace Frontend {
namespace {
void checked(bool ok,const char* operation) {
    if(!ok)throw std::runtime_error(std::string(operation)+": "+SDL_GetError());
}
void complete(id<MTLCommandBuffer> command) {
    if(!command)throw std::runtime_error("Metal sprite command buffer unavailable");
    [command commit];[command waitUntilCompleted];
    if(command.status==MTLCommandBufferStatusError)
        throw std::runtime_error("Metal sprite execution: "+std::string(command.error.localizedDescription.UTF8String?:"unknown error"));
}
}
struct NativeMetalSpriteScaler::Impl {
    SDL_Renderer* renderer{};
    id<MTLDevice> device;id<MTLCommandQueue> queue;id<MTLFence> fence;
    id<MTLComputePipelineState> pipelines[6];
    id<MTLBuffer> input,intermediate,layers;
    CVMetalTextureCacheRef cache{};CVPixelBufferRef pixels{};CVMetalTextureRef alias{};
    id<MTLTexture> output;SDL_Texture* texture{};
    unsigned w{},h{},factor{};
    explicit Impl(SDL_Renderer* r):renderer(r) {
        if(std::string_view(SDL_GetRendererName(r)?SDL_GetRendererName(r):"")!="metal")
            throw std::runtime_error("Native Metal sprite filters require SDL Metal");
        CAMetalLayer* layer=(__bridge CAMetalLayer*)SDL_GetRenderMetalLayer(r);device=layer.device;
        if(!device)throw std::runtime_error("Native Metal sprite device unavailable");
        queue=[device newCommandQueue];fence=[device newFence];
        if(!queue||!fence)throw std::runtime_error("Native Metal sprite queue/fence unavailable");
        using namespace GPUShaders;
        const char* sources[]{sprite_pass0_msl,sprite_pass1_msl,sprite_pass2_msl,sprite_pass3_msl,sprite_pass4_msl,sprite_scene_msl};
        MTLCompileOptions* options=[MTLCompileOptions new];options.fastMathEnabled=NO;
        for(unsigned i=0;i<6;++i) {
            NSError* error=nil;
            id<MTLLibrary> library=[device newLibraryWithSource:[NSString stringWithUTF8String:sources[i]] options:options error:&error];
            id<MTLFunction> function=[library newFunctionWithName:@"main0"];
            if(function)pipelines[i]=[device newComputePipelineStateWithFunction:function error:&error];
            if(!pipelines[i])throw std::runtime_error("Compile native Metal sprite kernel: "+std::string(error.localizedDescription.UTF8String?:"missing main0"));
        }
        if(CVMetalTextureCacheCreate(kCFAllocatorDefault,nullptr,device,nullptr,&cache)!=kCVReturnSuccess)
            throw std::runtime_error("Native Metal sprite texture cache unavailable");
    }
    void release_output() {
        SDL_DestroyTexture(texture);texture=nullptr;output=nil;
        if(alias)CFRelease(alias);alias=nullptr;
        if(pixels)CFRelease(pixels);pixels=nullptr;
        w=h=factor=0;
    }
    ~Impl(){release_output();if(cache)CFRelease(cache);}
    void resize(unsigned width,unsigned height,unsigned scale) {
        release_output();
        const size_t n=size_t(width)*height;
        input=[device newBufferWithLength:n*12 options:MTLResourceStorageModeShared];
        intermediate=[device newBufferWithLength:n*64 options:MTLResourceStorageModePrivate];
        NSDictionary* attributes=@{(__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey:@{},(__bridge NSString*)kCVPixelBufferMetalCompatibilityKey:@YES};
        if(CVPixelBufferCreate(kCFAllocatorDefault,width*scale,height*scale,kCVPixelFormatType_32BGRA,(__bridge CFDictionaryRef)attributes,&pixels)!=kCVReturnSuccess)
            throw std::runtime_error("Native Metal sprite IOSurface unavailable");
        NSDictionary* usage=@{(__bridge NSString*)kCVMetalTextureUsage:@(MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite)};
        if(CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,cache,pixels,(__bridge CFDictionaryRef)usage,MTLPixelFormatBGRA8Unorm,width*scale,height*scale,0,&alias)!=kCVReturnSuccess)
            throw std::runtime_error("Native Metal sprite texture alias unavailable");
        output=CVMetalTextureGetTexture(alias);
        const auto props=SDL_CreateProperties();
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_ARGB8888);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,width*scale);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,height*scale);
        SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_METAL_PIXELBUFFER_POINTER,pixels);
        texture=SDL_CreateTextureWithProperties(renderer,props);SDL_DestroyProperties(props);
        if(!input||!intermediate||!output||!texture)throw std::runtime_error("Allocate native Metal sprite resources: "+std::string(SDL_GetError()));
        checked(SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE)&&SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST),"Configure native Metal sprite output");
        w=width;h=height;factor=scale;
    }
};
NativeMetalSpriteScaler::NativeMetalSpriteScaler(SDL_Renderer* r):impl(std::make_unique<Impl>(r)){}
NativeMetalSpriteScaler::~NativeMetalSpriteScaler()=default;
SDL_Texture* NativeMetalSpriteScaler::compose(std::span<const uint32_t> background,std::span<const uint32_t> sprites,
    unsigned width,unsigned height,SpriteFilter filter,std::span<const uint8_t> protection,
    std::span<const uint32_t> heads,std::span<const Presentation::Fragment> fragments,
    unsigned cloud_alpha,unsigned trail_alpha,unsigned explosion_alpha,unsigned afterburner_alpha) {@autoreleasepool {
    auto& p=*impl;const size_t n=size_t(width)*height;
    if(!width||!height||width>8192||height>8192||background.size()!=n||sprites.size()!=n||
        (!protection.empty()&&protection.size()!=n)||(!heads.empty()&&heads.size()!=n)||
        unsigned(filter)>=sprite_filter_count||cloud_alpha>255||trail_alpha>255||explosion_alpha>255||afterburner_alpha>255)
        throw std::invalid_argument("Invalid native Metal sprite planes/filter");
    // Submit SDL's pending reads of our previous IOSurface before overwriting
    // it. Target switching submits SDL's encoder; the fence orders both queues.
    checked(SDL_FlushRenderer(p.renderer),"Flush native Metal sprites");
    auto* target=SDL_GetRenderTarget(p.renderer);
    id<MTLRenderCommandEncoder> encoder=(__bridge id<MTLRenderCommandEncoder>)SDL_GetRenderMetalCommandEncoder(p.renderer);
    if(!encoder)throw std::runtime_error("Native Metal sprite encoder unavailable");
    [encoder updateFence:p.fence afterStages:MTLRenderStageFragment];
    // This path is used inside Slang's captured source target (or a test target).
    if(!target)throw std::runtime_error("Native Metal sprites require a render target");
    checked(SDL_SetRenderTarget(p.renderer,nullptr),"Submit native Metal sprite reads");
    checked(SDL_SetRenderTarget(p.renderer,target),"Restore native Metal sprite target");
    id<MTLCommandBuffer> bridge=[p.queue commandBuffer];id<MTLBlitCommandEncoder> wait=[bridge blitCommandEncoder];
    [wait waitForFence:p.fence];[wait endEncoding];complete(bridge);
    if(p.w!=width||p.h!=height||p.factor!=sprite_factor(filter))p.resize(width,height,sprite_factor(filter));
    auto* data=static_cast<uint32_t*>(p.input.contents);
    std::memcpy(data,background.data(),n*4);std::memcpy(data+n,sprites.data(),n*4);
    for(size_t i=0;i<n;++i)data[n*2+i]=protection.empty()?0:protection[i];
    id<MTLCommandBuffer> command=[p.queue commandBuffer];
    if(!heads.empty()) {
        const size_t bytes=n*4+fragments.size_bytes();
        if(!p.layers||p.layers.length<bytes)p.layers=[p.device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        if(!p.layers)throw std::runtime_error("Native Metal sprite layers unavailable");
        std::memcpy(p.layers.contents,heads.data(),n*4);
        if(!fragments.empty())std::memcpy(static_cast<uint8_t*>(p.layers.contents)+n*4,fragments.data(),fragments.size_bytes());
        struct SceneParams{int32_t w,h,clouds,trails,explosions,afterburner;} params{int32_t(width),int32_t(height),int32_t(cloud_alpha),int32_t(trail_alpha),int32_t(explosion_alpha),int32_t(afterburner_alpha)};
        id<MTLComputeCommandEncoder> pass=[command computeCommandEncoder];
        [pass setComputePipelineState:p.pipelines[5]];[pass setBytes:&params length:sizeof(params) atIndex:0];
        [pass setBuffer:p.layers offset:0 atIndex:1];[pass setBuffer:p.input offset:0 atIndex:2];
        [pass dispatchThreadgroups:MTLSizeMake((width+7)/8,(height+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[pass endEncoding];
    }
    struct Params{int32_t w,h,mode,factor;} params{int32_t(width),int32_t(height),int32_t(filter),int32_t(p.factor)};
    for(unsigned i=filter==SpriteFilter::scalefx?0:4;i<5;++i) {
        id<MTLComputeCommandEncoder> pass=[command computeCommandEncoder];
        [pass setComputePipelineState:p.pipelines[i]];[pass setBytes:&params length:sizeof(params) atIndex:0];
        [pass setBuffer:p.input offset:0 atIndex:1];[pass setBuffer:p.intermediate offset:0 atIndex:2];
        if(i==4)[pass setTexture:p.output atIndex:0];
        const unsigned scale=i==4?p.factor:1;
        [pass dispatchThreadgroups:MTLSizeMake((width*scale+7)/8,(height*scale+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[pass endEncoding];
    }
    complete(command);return p.texture;
}}
}

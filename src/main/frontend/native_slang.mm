#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CoreVideo.h>
#include "frontend/native_slang.hpp"
#define LIBRA_RUNTIME_METAL
#include "../../third_party/librashader/librashader.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Frontend {
namespace {
constexpr size_t api_version=5,abi_version=2;
struct Api {
    SDL_SharedObject* object{};
#define SLANG_API(name) PFN_##name name{}
    SLANG_API(libra_instance_abi_version);SLANG_API(libra_instance_api_version);
    SLANG_API(libra_preset_create_with_options);SLANG_API(libra_preset_free);
    SLANG_API(libra_preset_color_space);SLANG_API(libra_preset_get_runtime_params);
    SLANG_API(libra_preset_free_runtime_params);SLANG_API(libra_preset_get_param);
    SLANG_API(libra_error_write);SLANG_API(libra_error_free_string);SLANG_API(libra_error_free);
    SLANG_API(libra_mtl_filter_chain_create);SLANG_API(libra_mtl_filter_chain_frame);
    SLANG_API(libra_mtl_filter_chain_free);SLANG_API(libra_mtl_filter_chain_set_param);
#undef SLANG_API
    ~Api(){if(object)SDL_UnloadObject(object);}
    void open(const std::string& requested) {
        std::vector<std::string> paths;
        if(!requested.empty())paths.push_back(requested);
        else {
            if(const char* p=SDL_getenv("AB2_LIBRASHADER"))if(*p)paths.emplace_back(p);
            if(const char* base=SDL_GetBasePath()) {
                paths.emplace_back(std::string(base)+"librashader.dylib");
                paths.emplace_back(std::string(base)+"../Frameworks/librashader.dylib");
                paths.emplace_back(std::string(base)+"deps/librashader/librashader.dylib");
                paths.emplace_back(std::string(base)+"build/deps/librashader/librashader.dylib");
            }
            paths.emplace_back("build/deps/librashader/librashader.dylib");
            paths.emplace_back("librashader.dylib");
        }
        for(const auto& path:paths){object=SDL_LoadObject(path.c_str());if(object)break;}
        if(!object)throw std::runtime_error("Optional librashader runtime unavailable: "+std::string(SDL_GetError()));
#define LOAD(name) name=reinterpret_cast<PFN_##name>(SDL_LoadFunction(object,#name));if(!name)throw std::runtime_error("Missing librashader symbol: " #name)
        LOAD(libra_instance_abi_version);LOAD(libra_instance_api_version);
        if(libra_instance_abi_version()!=abi_version || libra_instance_api_version()!=api_version)
            throw std::runtime_error("librashader requires ABI 2 / API 5");
        LOAD(libra_preset_create_with_options);LOAD(libra_preset_free);
        LOAD(libra_preset_color_space);LOAD(libra_preset_get_runtime_params);
        LOAD(libra_preset_free_runtime_params);LOAD(libra_preset_get_param);
        LOAD(libra_error_write);LOAD(libra_error_free_string);LOAD(libra_error_free);
        LOAD(libra_mtl_filter_chain_create);LOAD(libra_mtl_filter_chain_frame);
        LOAD(libra_mtl_filter_chain_free);LOAD(libra_mtl_filter_chain_set_param);
#undef LOAD
        SDL_ClearError();
    }
    void check(libra_error_t err,const char* operation)const {
        if(!err)return;
        char* text{};libra_error_write(err,&text);
        std::string message=std::string(operation)+": "+(text?text:"unknown librashader error");
        if(text)libra_error_free_string(&text);libra_error_free(&err);
        throw std::runtime_error(message);
    }
    void discard(libra_error_t err)const{if(err)libra_error_free(&err);}
};
struct RenderState {
    SDL_Texture* target{};int w{},h{};SDL_RendererLogicalPresentation presentation{};
    float sx{},sy{};SDL_Rect viewport{},clip{};bool has_viewport{},has_clip{};
    bool capture(SDL_Renderer* r) {
        target=SDL_GetRenderTarget(r);has_viewport=SDL_RenderViewportSet(r);has_clip=SDL_RenderClipEnabled(r);
        return SDL_GetRenderLogicalPresentation(r,&w,&h,&presentation) && SDL_GetRenderScale(r,&sx,&sy) &&
            SDL_GetRenderViewport(r,&viewport) && SDL_GetRenderClipRect(r,&clip);
    }
    bool restore(SDL_Renderer* r)const {
        bool result=SDL_SetRenderTarget(r,target);
        result=SDL_SetRenderLogicalPresentation(r,w,h,presentation) && result;
        result=SDL_SetRenderScale(r,sx,sy) && result;
        result=SDL_SetRenderViewport(r,has_viewport?&viewport:nullptr) && result;
        return SDL_SetRenderClipRect(r,has_clip?&clip:nullptr) && result;
    }
};
struct SharedTarget {
    CVPixelBufferRef pixels{};CVMetalTextureRef alias{};SDL_Texture* texture{};id<MTLTexture> metal;
    ~SharedTarget(){SDL_DestroyTexture(texture);if(alias)CFRelease(alias);if(pixels)CFRelease(pixels);}
    void create(SDL_Renderer* renderer,CVMetalTextureCacheRef cache,unsigned w,unsigned h) {
        if(!w || !h || w>8192 || h>8192 || uint64_t(w)*h*4>64*1024*1024)
            throw std::runtime_error("Slang display target exceeds the 64 MiB image limit");
        NSDictionary* attributes=@{(__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey:@{},(__bridge NSString*)kCVPixelBufferMetalCompatibilityKey:@YES};
        if(CVPixelBufferCreate(kCFAllocatorDefault,w,h,kCVPixelFormatType_32BGRA,(__bridge CFDictionaryRef)attributes,&pixels)!=kCVReturnSuccess)
            throw std::runtime_error("Slang IOSurface allocation failed");
        NSDictionary* usage=@{(__bridge NSString*)kCVMetalTextureUsage:@(MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget)};
        if(CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,cache,pixels,(__bridge CFDictionaryRef)usage,MTLPixelFormatBGRA8Unorm,w,h,0,&alias)!=kCVReturnSuccess)
            throw std::runtime_error("Slang native texture alias failed");
        metal=CVMetalTextureGetTexture(alias);
        const auto props=SDL_CreateProperties();
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_ARGB8888);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_TARGET);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,w);SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,h);
        SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_METAL_PIXELBUFFER_POINTER,pixels);
        texture=SDL_CreateTextureWithProperties(renderer,props);SDL_DestroyProperties(props);
        if(!texture || !SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE) || !SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST))
            throw std::runtime_error("Slang SDL texture import failed: "+std::string(SDL_GetError()));
    }
};
void checked(bool result,const char* operation){if(!result)throw std::runtime_error(std::string(operation)+": "+SDL_GetError());}
void complete(id<MTLCommandBuffer> buffer) {
    if(!buffer)throw std::runtime_error("Slang Metal command buffer unavailable");
    [buffer commit];[buffer waitUntilCompleted];
    if(buffer.status==MTLCommandBufferStatusError)
        throw std::runtime_error("Slang Metal execution: "+std::string(buffer.error.localizedDescription.UTF8String?:"unknown error"));
}
}
struct NativeSlang::Impl {
    SDL_Renderer* renderer{};SDL_Window* window{};std::string message,preset_path;
    id<MTLDevice> device;id<MTLCommandQueue> queue;id<MTLFence> fence;
    CVMetalTextureCacheRef cache{};std::unique_ptr<Api> api;libra_mtl_filter_chain_t chain{};
    std::unique_ptr<SharedTarget> source,output;
    std::vector<ShaderParameter> params;RenderState saved;SDL_FRect area{};
    unsigned logical_width{},output_width{},output_height{};uint64_t last_frame{};
    bool capturing{},cached{},dirty{true},clear_next{true},ever_rendered{};
    Impl(SDL_Renderer* r,SDL_Window* w):renderer(r),window(w){}
    ~Impl(){if(capturing)saved.restore(renderer);if(chain&&api)api->discard(api->libra_mtl_filter_chain_free(&chain));output.reset();source.reset();if(cache)CFRelease(cache);}
    bool bounds(SDL_FRect& rect,unsigned& width,unsigned& height)const {
        if(!SDL_GetRenderLogicalPresentationRect(renderer,&rect))return false;
        if(!std::isfinite(rect.w)||!std::isfinite(rect.h)||rect.w<=0||rect.h<=0)return false;
        width=unsigned(std::ceil(rect.w));height=unsigned(std::ceil(rect.h));return width&&height;
    }
    void initialize() {
        if(device)return;
        if(!renderer || std::string_view(SDL_GetRendererName(renderer)?SDL_GetRendererName(renderer):"")!="metal")
            throw std::runtime_error("RetroArch Slang shaders require the native SDL Metal renderer; restart with a shader preset selected");
        CAMetalLayer* layer=(__bridge CAMetalLayer*)SDL_GetRenderMetalLayer(renderer);
        if(!layer || !layer.device)throw std::runtime_error("Slang Metal device unavailable");
        id<MTLDevice> next_device=layer.device;id<MTLCommandQueue> next_queue=[next_device newCommandQueue];id<MTLFence> next_fence=[next_device newFence];
        if(!next_queue||!next_fence)throw std::runtime_error("Slang Metal queue/fence unavailable");
        CVMetalTextureCacheRef next_cache{};
        if(CVMetalTextureCacheCreate(kCFAllocatorDefault,nullptr,next_device,nullptr,&next_cache)!=kCVReturnSuccess)
            throw std::runtime_error("Slang Metal texture cache unavailable");
        device=next_device;queue=next_queue;fence=next_fence;cache=next_cache;
    }
    void reset_history() {
        // librashader 0.12.0 encodes an invalid attachment-free Metal render
        // pass when clear_history is requested for a preset without history.
        // Recreate the chain instead; this also resets feedback buffers, and
        // retains all user parameter values. Reset is outside steady rendering.
        libra_shader_preset_t preset{};libra_mtl_filter_chain_t replacement{};
        try {
            libra_preset_opt_t options{};options.version=api_version;
            options.original_aspect_uniforms=true;options.frametime_uniforms=true;
            api->check(api->libra_preset_create_with_options(preset_path.c_str(),nullptr,&options,&preset),"Reset Slang preset");
            LIBRA_COLOR_SPACE color{};api->check(api->libra_preset_color_space(&preset,&color),"Inspect reset Slang output");
            if(color!=LIBRA_COLOR_SPACE_SDR)throw std::runtime_error("Reset Slang preset changed to unsupported HDR output");
            filter_chain_mtl_opt_t chain_options{};chain_options.version=api_version;
            api->check(api->libra_mtl_filter_chain_create(&preset,queue,&chain_options,&replacement),"Reset Slang history");
            for(const auto& parameter:params)
                api->check(api->libra_mtl_filter_chain_set_param(&replacement,parameter.name.c_str(),parameter.value),"Restore Slang parameter");
        }catch(...){if(preset)api->discard(api->libra_preset_free(&preset));if(replacement)api->discard(api->libra_mtl_filter_chain_free(&replacement));throw;}
        api->discard(api->libra_mtl_filter_chain_free(&chain));chain=replacement;
    }
    void draw(SDL_Texture* texture) {
        if(!texture)return;
        RenderState state;checked(state.capture(renderer),"Save Slang presentation state");
        bool ok=SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
        ok=SDL_SetRenderScale(renderer,1,1)&&ok;ok=SDL_SetRenderViewport(renderer,nullptr)&&ok;ok=SDL_SetRenderClipRect(renderer,nullptr)&&ok;
        if(ok)ok=SDL_RenderTexture(renderer,texture,nullptr,&area);
        const std::string failure=ok?std::string{}:SDL_GetError();const bool restored=state.restore(renderer);
        if(!ok)throw std::runtime_error("Draw Slang output: "+failure);checked(restored,"Restore Slang presentation state");
    }
};
NativeSlang::NativeSlang(SDL_Renderer* renderer,SDL_Window* window):impl(std::make_unique<Impl>(renderer,window)){}
NativeSlang::~NativeSlang()=default;
const std::string& NativeSlang::error()const{return impl->message;}
bool NativeSlang::available()const{return impl->chain!=nullptr;}
const std::vector<ShaderParameter>& NativeSlang::parameters()const{return impl->params;}
bool NativeSlang::load(const std::string& path,const std::string& runtime) {@autoreleasepool{
    auto& p=*impl;try {
        if(p.capturing)throw std::runtime_error("Cannot change Slang preset during a captured frame");
        if(std::filesystem::path(path).extension()!=".slangp")throw std::runtime_error("Select a RetroArch .slangp shader preset");
        p.initialize();auto next=std::make_unique<Api>();next->open(runtime);libra_shader_preset_t preset{};
        std::vector<ShaderParameter> params;libra_mtl_filter_chain_t chain{};
        try {
            libra_preset_opt_t opt{};opt.version=api_version;opt.original_aspect_uniforms=true;opt.frametime_uniforms=true;
            next->check(next->libra_preset_create_with_options(path.c_str(),nullptr,&opt,&preset),"Load Slang preset");
            LIBRA_COLOR_SPACE color{};next->check(next->libra_preset_color_space(&preset,&color),"Inspect Slang output");
            if(color!=LIBRA_COLOR_SPACE_SDR)throw std::runtime_error("HDR Slang presets require an HDR output path; select an SDR preset");
            libra_preset_param_list_t list{};next->check(next->libra_preset_get_runtime_params(&preset,&list),"Read Slang parameters");
            try {for(uint64_t i=0;i<list.length;++i){const auto& x=list.parameters[i];float value=x.initial;next->check(next->libra_preset_get_param(&preset,x.name,&value),"Read Slang parameter");params.push_back({x.name?x.name:"",x.description?x.description:"",value,x.minimum,x.maximum,x.step});}}
            catch(...){next->discard(next->libra_preset_free_runtime_params(list));throw;}
            next->discard(next->libra_preset_free_runtime_params(list));
            filter_chain_mtl_opt_t chain_options{};chain_options.version=api_version;
            next->check(next->libra_mtl_filter_chain_create(&preset,p.queue,&chain_options,&chain),"Compile Slang preset");
        }catch(...){if(preset)next->discard(next->libra_preset_free(&preset));if(chain)next->discard(next->libra_mtl_filter_chain_free(&chain));throw;}
        if(p.chain)p.api->discard(p.api->libra_mtl_filter_chain_free(&p.chain));
        p.api=std::move(next);p.chain=chain;p.params=std::move(params);p.preset_path=path;p.cached=false;p.dirty=true;p.clear_next=true;p.ever_rendered=false;p.message.clear();return true;
    }catch(const std::exception& e){p.message=e.what();return false;}
}}
bool NativeSlang::needs_frame(uint64_t frame)const {
    const auto& p=*impl;if(!p.chain)return false;SDL_FRect rect{};unsigned w{},h{};
    if(!p.bounds(rect,w,h))return false;
    int logical_w{},logical_h{};SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderLogicalPresentation(p.renderer,&logical_w,&logical_h,&mode);
    return p.dirty || !p.cached || p.last_frame!=frame || w!=p.output_width || h!=p.output_height ||
        rect.x!=p.area.x || rect.y!=p.area.y || rect.w!=p.area.w || rect.h!=p.area.h || (logical_w>0&&unsigned(logical_w)!=p.logical_width);
}
bool NativeSlang::begin(unsigned width,uint64_t) {@autoreleasepool{
    auto& p=*impl;try {
        if(!p.chain)throw std::runtime_error("No Slang preset is loaded");if(p.capturing)throw std::runtime_error("Slang frame is already active");
        if(width<1||width>8192)throw std::runtime_error("Invalid Slang logical width");
        SDL_FRect area{};unsigned w{},h{};if(!p.bounds(area,w,h))throw std::runtime_error("Slang drawable area unavailable");
        checked(p.saved.capture(p.renderer),"Save Slang renderer state");
        if(!p.source||p.logical_width!=width||p.output_width!=w||p.output_height!=h){
            auto source=std::make_unique<SharedTarget>(),output=std::make_unique<SharedTarget>();
            source->create(p.renderer,p.cache,width,224);output->create(p.renderer,p.cache,w,h);
            p.source=std::move(source);p.output=std::move(output);p.logical_width=width;p.output_width=w;p.output_height=h;p.clear_next=true;p.cached=false;
        }
        p.area=area;p.capturing=true;
        checked(SDL_SetRenderTarget(p.renderer,p.source->texture),"Begin Slang source");
        checked(SDL_SetRenderLogicalPresentation(p.renderer,width,224,SDL_LOGICAL_PRESENTATION_STRETCH),"Slang source coordinates");
        checked(SDL_SetRenderScale(p.renderer,1,1)&&SDL_SetRenderViewport(p.renderer,nullptr)&&SDL_SetRenderClipRect(p.renderer,nullptr),"Reset Slang source viewport");return true;
    }catch(const std::exception& e){if(p.capturing){p.saved.restore(p.renderer);p.capturing=false;}p.message=e.what();return false;}
}}
bool NativeSlang::end(uint64_t frame,double fps,double delta_us,bool clear_history) {@autoreleasepool{
    auto& p=*impl;if(!p.capturing){p.message="No Slang source frame is active";return false;}
    bool success=false;
    try {
        checked(SDL_FlushRenderer(p.renderer),"Flush Slang source");
        id<MTLRenderCommandEncoder> encoder=(__bridge id<MTLRenderCommandEncoder>)SDL_GetRenderMetalCommandEncoder(p.renderer);
        if(!encoder)throw std::runtime_error("Slang source Metal encoder unavailable");
        [encoder updateFence:p.fence afterStages:MTLRenderStageFragment];
        // Switching targets commits SDL 3.4's native Metal source command buffer.
        checked(SDL_SetRenderTarget(p.renderer,p.saved.target),"Submit Slang source");
        // Synchronize the external IOSurface aliases using public Metal APIs.
        // The fresh librashader command buffer must contain no prior commands.
        id<MTLCommandBuffer> bridge=[p.queue commandBuffer];id<MTLBlitCommandEncoder> wait=[bridge blitCommandEncoder];
        [wait waitForFence:p.fence];[wait endEncoding];complete(bridge);
        if(p.ever_rendered&&(clear_history||p.clear_next))p.reset_history();
        frame_mtl_opt_t opt{};opt.version=api_version;opt.frame_direction=1;
        opt.total_subframes=1;opt.current_subframe=1;opt.aspect_ratio=float(p.logical_width)/224.f;
        opt.frames_per_second=std::isfinite(fps)&&fps>0?float(fps):60.f;
        // Slang's FrameTimeDelta is microseconds. The v0.12.0 C header's
        // milliseconds comment is inconsistent with its unchanged raw binding.
        opt.frametime_delta=std::isfinite(delta_us)?uint32_t(std::clamp(delta_us,0.,double(UINT32_MAX))):0;
        opt.color_space=LIBRA_COLOR_SPACE_SDR;opt.brightness_nits=200;
        id<MTLCommandBuffer> commands=[p.queue commandBuffer];
        p.api->check(p.api->libra_mtl_filter_chain_frame(&p.chain,commands,size_t(frame),p.source->metal,p.output->metal,nullptr,nullptr,&opt),"Render Slang frame");
        complete(commands);p.last_frame=frame;p.cached=true;p.dirty=false;p.clear_next=false;p.ever_rendered=true;p.message.clear();success=true;
    }catch(const std::exception& e){p.message=e.what();p.cached=false;p.dirty=true;p.clear_next=true;}
    const bool restored=p.saved.restore(p.renderer);p.capturing=false;
    if(!restored){p.message="Restore Slang renderer state: "+std::string(SDL_GetError());return false;}
    try {p.draw(success?p.output->texture:p.source->texture);}catch(const std::exception& e){p.message=e.what();return false;}
    return success;
}}
void NativeSlang::draw_cached() {@autoreleasepool{auto& p=*impl;try{if(p.cached)p.draw(p.output->texture);}catch(const std::exception& e){p.message=e.what();}}}
void NativeSlang::invalidate(){impl->dirty=true;impl->clear_next=true;}
bool NativeSlang::set_parameter(const std::string& name,float value) {@autoreleasepool{
    auto& p=*impl;try {
        auto it=std::find_if(p.params.begin(),p.params.end(),[&](const auto& x){return x.name==name;});
        if(!p.chain||it==p.params.end()||!std::isfinite(value)||value<it->minimum||value>it->maximum)throw std::runtime_error("Invalid Slang shader parameter");
        p.api->check(p.api->libra_mtl_filter_chain_set_param(&p.chain,name.c_str(),value),"Set Slang parameter");it->value=value;p.dirty=true;p.clear_next=true;p.message.clear();return true;
    }catch(const std::exception& e){p.message=e.what();return false;}
}}
}

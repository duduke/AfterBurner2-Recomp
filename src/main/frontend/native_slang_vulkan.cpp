#include "frontend/native_slang.hpp"
#define LIBRA_RUNTIME_VULKAN
#include "../../third_party/librashader/librashader.h"
#include <SDL3/SDL_vulkan.h>
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
    SLANG_API(libra_vk_filter_chain_create);SLANG_API(libra_vk_filter_chain_frame);
    SLANG_API(libra_vk_filter_chain_free);SLANG_API(libra_vk_filter_chain_set_param);
#undef SLANG_API
    ~Api(){if(object)SDL_UnloadObject(object);}
    void open(const std::string& requested) {
        std::vector<std::string> paths;
        if(!requested.empty())paths.push_back(requested);
        else {
            if(const char* p=SDL_getenv("AB2_LIBRASHADER"))if(*p)paths.emplace_back(p);
#ifdef _WIN32
            constexpr const char* library="librashader.dll";
#else
            constexpr const char* library="liblibrashader_capi.so";
#endif
            if(const char* base=SDL_GetBasePath()) {
                paths.emplace_back(std::string(base)+library);
                paths.emplace_back(std::string(base)+"lib/"+library);
                paths.emplace_back(std::string(base)+"build/deps/librashader/"+library);
                // CMake build/linux/afterburner2, independent of launch cwd.
                paths.emplace_back(std::string(base)+"../deps/librashader/"+library);
            }
            paths.emplace_back(std::string("build/deps/librashader/")+library);
            paths.emplace_back(library);
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
        LOAD(libra_vk_filter_chain_create);LOAD(libra_vk_filter_chain_frame);
        LOAD(libra_vk_filter_chain_free);LOAD(libra_vk_filter_chain_set_param);
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
void checked(bool result,const char* operation){if(!result)throw std::runtime_error(std::string(operation)+": "+SDL_GetError());}
void checked_vk(VkResult result,const char* operation){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": VkResult "+std::to_string(result));}
struct SharedTarget {
    SDL_Texture* texture{};libra_image_vk_t image{};
    ~SharedTarget(){SDL_DestroyTexture(texture);}
    void create(SDL_Renderer* renderer,unsigned w,unsigned h) {
        if(!w || !h || w>8192 || h>8192 || uint64_t(w)*h*4>64*1024*1024)
            throw std::runtime_error("Slang display target exceeds the 64 MiB image limit");
        texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
        checked(texture && SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE) && SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST),"Create Slang SDL target");
        image={reinterpret_cast<VkImage>(uintptr_t(SDL_GetNumberProperty(SDL_GetTextureProperties(texture),SDL_PROP_TEXTURE_VULKAN_TEXTURE_NUMBER,0))),VK_FORMAT_B8G8R8A8_UNORM,w,h};
        if(!image.handle)throw std::runtime_error("Slang Vulkan texture unavailable");
        // SDL transitions its target to COLOR_ATTACHMENT, then to SHADER_READ
        // on unbinding, and submits it. Keep SDL's tracked layout unchanged
        // across the external pass, including the very first output frame.
        checked(SDL_SetRenderTarget(renderer,texture),"Initialize Slang target");
        checked(SDL_SetRenderTarget(renderer,nullptr),"Submit Slang target");
    }
};
}
struct NativeSlang::Impl {
    SDL_Renderer* renderer{};SDL_Window* window{};std::string message,preset_path;
    libra_device_vk_t vk{};VkCommandPool pool{};VkCommandBuffer commands{};VkFence fence{};
#define VK_API(name) PFN_##name name{}
    VK_API(vkGetDeviceQueue);VK_API(vkCreateCommandPool);VK_API(vkDestroyCommandPool);
    VK_API(vkAllocateCommandBuffers);VK_API(vkResetCommandBuffer);VK_API(vkBeginCommandBuffer);VK_API(vkEndCommandBuffer);
    VK_API(vkCreateFence);VK_API(vkDestroyFence);VK_API(vkResetFences);VK_API(vkWaitForFences);
    VK_API(vkQueueSubmit);VK_API(vkDeviceWaitIdle);VK_API(vkCmdPipelineBarrier);
#undef VK_API
    std::unique_ptr<Api> api;libra_vk_filter_chain_t chain{};
    std::unique_ptr<SharedTarget> source,output;
    std::vector<ShaderParameter> params;RenderState saved;SDL_FRect area{};
    unsigned logical_width{},output_width{},output_height{};uint64_t last_frame{};
    bool capturing{},cached{},dirty{true},clear_next{true},ever_rendered{},pending{};
    Impl(SDL_Renderer* r,SDL_Window* w):renderer(r),window(w){}
    ~Impl(){
        if(capturing)saved.restore(renderer);
        if(vk.device&&vkDeviceWaitIdle)vkDeviceWaitIdle(vk.device);
        if(chain&&api)api->discard(api->libra_vk_filter_chain_free(&chain));
        output.reset();source.reset();
        if(fence)vkDestroyFence(vk.device,fence,nullptr);
        if(pool)vkDestroyCommandPool(vk.device,pool,nullptr);
    }
    bool bounds(SDL_FRect& rect,unsigned& width,unsigned& height)const {
        if(!SDL_GetRenderLogicalPresentationRect(renderer,&rect))return false;
        if(!std::isfinite(rect.w)||!std::isfinite(rect.h)||rect.w<=0||rect.h<=0)return false;
        width=unsigned(std::ceil(rect.w));height=unsigned(std::ceil(rect.h));return width&&height;
    }
    void initialize() {
        if(pool)return;
        if(!renderer || std::string_view(SDL_GetRendererName(renderer)?SDL_GetRendererName(renderer):"")!="vulkan")
            throw std::runtime_error("RetroArch Slang shaders require the native SDL Vulkan renderer");
        const auto props=SDL_GetRendererProperties(renderer);
        vk.instance=static_cast<VkInstance>(SDL_GetPointerProperty(props,SDL_PROP_RENDERER_VULKAN_INSTANCE_POINTER,nullptr));
        vk.physical_device=static_cast<VkPhysicalDevice>(SDL_GetPointerProperty(props,SDL_PROP_RENDERER_VULKAN_PHYSICAL_DEVICE_POINTER,nullptr));
        vk.device=static_cast<VkDevice>(SDL_GetPointerProperty(props,SDL_PROP_RENDERER_VULKAN_DEVICE_POINTER,nullptr));
        vk.entry=reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        if(!vk.instance||!vk.physical_device||!vk.device||!vk.entry)throw std::runtime_error("Slang Vulkan device unavailable");
        auto get=reinterpret_cast<PFN_vkGetDeviceProcAddr>(vk.entry(vk.instance,"vkGetDeviceProcAddr"));
        if(!get)throw std::runtime_error("Slang Vulkan device loader unavailable");
#define LOAD_VK(name) name=reinterpret_cast<PFN_##name>(get(vk.device,#name));if(!name)throw std::runtime_error("Missing Vulkan symbol: " #name)
        LOAD_VK(vkGetDeviceQueue);LOAD_VK(vkCreateCommandPool);LOAD_VK(vkDestroyCommandPool);
        LOAD_VK(vkAllocateCommandBuffers);LOAD_VK(vkResetCommandBuffer);LOAD_VK(vkBeginCommandBuffer);LOAD_VK(vkEndCommandBuffer);
        LOAD_VK(vkCreateFence);LOAD_VK(vkDestroyFence);LOAD_VK(vkResetFences);LOAD_VK(vkWaitForFences);
        LOAD_VK(vkQueueSubmit);LOAD_VK(vkDeviceWaitIdle);LOAD_VK(vkCmdPipelineBarrier);
#undef LOAD_VK
        const auto family=uint32_t(SDL_GetNumberProperty(props,SDL_PROP_RENDERER_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER,0));
        vkGetDeviceQueue(vk.device,family,0,&vk.queue);
        VkCommandPoolCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;ci.queueFamilyIndex=family;
        VkCommandPool next{};checked_vk(vkCreateCommandPool(vk.device,&ci,nullptr,&next),"Create Slang command pool");
        try {
            VkCommandBufferAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ai.commandPool=next;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
            checked_vk(vkAllocateCommandBuffers(vk.device,&ai,&commands),"Allocate Slang commands");
            VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            checked_vk(vkCreateFence(vk.device,&fi,nullptr,&fence),"Create Slang fence");
        }catch(...){vkDestroyCommandPool(vk.device,next,nullptr);throw;}
        pool=next;
    }
    void wait() {
        if(!pending)return;
        checked_vk(vkWaitForFences(vk.device,1,&fence,VK_TRUE,UINT64_MAX),"Wait for Slang resources");
        pending=false;
    }
    void begin_commands() {
        wait();
        checked_vk(vkResetCommandBuffer(commands,0),"Reset Slang commands");
        VkCommandBufferBeginInfo bi{};bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked_vk(vkBeginCommandBuffer(commands,&bi),"Begin Slang commands");
    }
    void submit() {
        checked_vk(vkEndCommandBuffer(commands),"End Slang commands");
        checked_vk(vkResetFences(vk.device,1,&fence),"Reset Slang fence");
        VkSubmitInfo si{};si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;si.commandBufferCount=1;si.pCommandBuffers=&commands;
        checked_vk(vkQueueSubmit(vk.queue,1,&si,fence),"Submit Slang commands");
        // SDL samples the output on this same queue, after output_barrier(false).
        // Let presentation/CPU work overlap instead of blocking on the GPU here.
        // The fence is still required before reusing commands or chain resources.
        pending=true;
    }
    void output_barrier(bool before) {
        VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout=before?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.newLayout=before?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask=before?VK_ACCESS_SHADER_READ_BIT:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask=before?VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=output->image.handle;
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(commands,before?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            before?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
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
bool NativeSlang::load(const std::string& path,const std::string& runtime) {
    auto& p=*impl;try {
        if(p.capturing)throw std::runtime_error("Cannot change Slang preset during a captured frame");
        if(std::filesystem::path(path).extension()!=".slangp")throw std::runtime_error("Select a RetroArch .slangp shader preset");
        p.initialize();p.wait();auto next=std::make_unique<Api>();next->open(runtime);libra_shader_preset_t preset{};
        std::vector<ShaderParameter> params;libra_vk_filter_chain_t chain{};
        try {
            libra_preset_opt_t opt{};opt.version=api_version;opt.original_aspect_uniforms=true;opt.frametime_uniforms=true;
            next->check(next->libra_preset_create_with_options(path.c_str(),nullptr,&opt,&preset),"Load Slang preset");
            LIBRA_COLOR_SPACE color{};next->check(next->libra_preset_color_space(&preset,&color),"Inspect Slang output");
            if(color!=LIBRA_COLOR_SPACE_SDR)throw std::runtime_error("HDR Slang presets require an HDR output path; select an SDR preset");
            libra_preset_param_list_t list{};next->check(next->libra_preset_get_runtime_params(&preset,&list),"Read Slang parameters");
            try {for(uint64_t i=0;i<list.length;++i){const auto& x=list.parameters[i];float value=x.initial;next->check(next->libra_preset_get_param(&preset,x.name,&value),"Read Slang parameter");params.push_back({x.name?x.name:"",x.description?x.description:"",value,x.minimum,x.maximum,x.step});}}
            catch(...){next->discard(next->libra_preset_free_runtime_params(list));throw;}
            next->discard(next->libra_preset_free_runtime_params(list));
            filter_chain_vk_opt_t chain_options{};chain_options.version=api_version;chain_options.frames_in_flight=1;
            next->check(next->libra_vk_filter_chain_create(&preset,p.vk,&chain_options,&chain),"Compile Slang preset");
        }catch(...){if(preset)next->discard(next->libra_preset_free(&preset));if(chain)next->discard(next->libra_vk_filter_chain_free(&chain));throw;}
        if(p.chain)p.api->discard(p.api->libra_vk_filter_chain_free(&p.chain));
        p.api=std::move(next);p.chain=chain;p.params=std::move(params);p.preset_path=path;p.cached=false;p.dirty=true;p.clear_next=true;p.ever_rendered=false;p.message.clear();return true;
    }catch(const std::exception& e){p.message=e.what();return false;}
}
bool NativeSlang::needs_frame(uint64_t frame)const {
    const auto& p=*impl;if(!p.chain)return false;SDL_FRect rect{};unsigned w{},h{};
    if(!p.bounds(rect,w,h))return false;
    int logical_w{},logical_h{};SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderLogicalPresentation(p.renderer,&logical_w,&logical_h,&mode);
    return p.dirty || !p.cached || p.last_frame!=frame || w!=p.output_width || h!=p.output_height ||
        rect.x!=p.area.x || rect.y!=p.area.y || rect.w!=p.area.w || rect.h!=p.area.h || (logical_w>0&&unsigned(logical_w)!=p.logical_width);
}
bool NativeSlang::begin(unsigned width,uint64_t) {
    auto& p=*impl;try {
        if(!p.chain)throw std::runtime_error("No Slang preset is loaded");if(p.capturing)throw std::runtime_error("Slang frame is already active");
        if(width<1||width>8192)throw std::runtime_error("Invalid Slang logical width");
        SDL_FRect area{};unsigned w{},h{};if(!p.bounds(area,w,h))throw std::runtime_error("Slang drawable area unavailable");
        p.wait();
        checked(p.saved.capture(p.renderer),"Save Slang renderer state");
        if(!p.source||p.logical_width!=width||p.output_width!=w||p.output_height!=h){
            auto source=std::make_unique<SharedTarget>(),output=std::make_unique<SharedTarget>();
            source->create(p.renderer,width,224);output->create(p.renderer,w,h);
            p.source=std::move(source);p.output=std::move(output);p.logical_width=width;p.output_width=w;p.output_height=h;p.clear_next=true;p.cached=false;
        }
        p.area=area;p.capturing=true;
        checked(SDL_SetRenderTarget(p.renderer,p.source->texture),"Begin Slang source");
        checked(SDL_SetRenderLogicalPresentation(p.renderer,width,224,SDL_LOGICAL_PRESENTATION_STRETCH),"Slang source coordinates");
        checked(SDL_SetRenderScale(p.renderer,1,1)&&SDL_SetRenderViewport(p.renderer,nullptr)&&SDL_SetRenderClipRect(p.renderer,nullptr),"Reset Slang source viewport");return true;
    }catch(const std::exception& e){if(p.capturing){p.saved.restore(p.renderer);p.capturing=false;}p.message=e.what();return false;}
}
bool NativeSlang::end(uint64_t frame,double fps,double delta_us,bool clear_history) {
    auto& p=*impl;if(!p.capturing){p.message="No Slang source frame is active";return false;}
    bool success=false;
    try {
        // SDL 3.4's native Vulkan target unbind transitions to shader-read,
        // flushes and submits the source commands, and waits for its batch.
        // A preceding SDL_FlushRenderer submits a separate batch and adds a
        // redundant queue-idle wait. Keep drawing and the transition together.
        // SDL and Slang use the same graphics queue; no image copies or CPU
        // readback are needed.
        checked(SDL_SetRenderTarget(p.renderer,nullptr),"Submit Slang source");
        frame_vk_opt_t opt{};opt.version=api_version;opt.frame_direction=1;opt.clear_history=clear_history||p.clear_next;
        opt.total_subframes=1;opt.current_subframe=1;opt.aspect_ratio=float(p.logical_width)/224.f;
        opt.frames_per_second=std::isfinite(fps)&&fps>0?float(fps):60.f;
        // Slang's FrameTimeDelta is microseconds. The v0.12.0 C header's
        // milliseconds comment is inconsistent with its unchanged raw binding.
        opt.frametime_delta=std::isfinite(delta_us)?uint32_t(std::clamp(delta_us,0.,double(UINT32_MAX))):0;
        opt.color_space=LIBRA_COLOR_SPACE_SDR;opt.brightness_nits=200;
        p.begin_commands();p.output_barrier(true);
        p.api->check(p.api->libra_vk_filter_chain_frame(&p.chain,p.commands,size_t(frame),p.source->image,p.output->image,nullptr,nullptr,&opt),"Render Slang frame");
        p.output_barrier(false);p.submit();p.last_frame=frame;p.cached=true;p.dirty=false;p.clear_next=false;p.ever_rendered=true;p.message.clear();success=true;
    }catch(const std::exception& e){p.message=e.what();p.cached=false;p.dirty=true;p.clear_next=true;}
    const bool restored=p.saved.restore(p.renderer);p.capturing=false;
    if(!restored){p.message="Restore Slang renderer state: "+std::string(SDL_GetError());return false;}
    try {p.draw(success?p.output->texture:p.source->texture);}catch(const std::exception& e){p.message=e.what();return false;}
    return success;
}
void NativeSlang::draw_cached() {auto& p=*impl;try{if(p.cached)p.draw(p.output->texture);}catch(const std::exception& e){p.message=e.what();}}
void NativeSlang::invalidate(){impl->dirty=true;impl->clear_next=true;}
bool NativeSlang::set_parameter(const std::string& name,float value) {
    auto& p=*impl;try {
        auto it=std::find_if(p.params.begin(),p.params.end(),[&](const auto& x){return x.name==name;});
        if(!p.chain||it==p.params.end()||!std::isfinite(value)||value<it->minimum||value>it->maximum)throw std::runtime_error("Invalid Slang shader parameter");
        p.wait();
        p.api->check(p.api->libra_vk_filter_chain_set_param(&p.chain,name.c_str(),value),"Set Slang parameter");it->value=value;p.dirty=true;p.clear_next=true;p.message.clear();return true;
    }catch(const std::exception& e){p.message=e.what();return false;}
}
}

#include "frontend/native_vulkan_sprite_filter.hpp"
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include "frontend/scalers/gpu_shaders.hpp"
#include <array>
#include <cstring>
#include <string_view>

namespace Frontend {
namespace {
void checked(bool ok,const char* action) {
    if(!ok)throw std::runtime_error(std::string(action)+": "+SDL_GetError());
}
void checked(VkResult result,const char* action) {
    if(result!=VK_SUCCESS)throw std::runtime_error(std::string(action)+": VkResult "+std::to_string(result));
}
}
struct NativeVulkanSpriteScaler::Impl {
    SDL_Renderer* renderer{};
    VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkCommandPool pool{};VkCommandBuffer commands{};VkFence fence{};bool pending{};
    VkDescriptorPool descriptors{};
    VkDescriptorSetLayout storage_layout{},output_layout{},uniform_layout{};
    VkPipelineLayout layout{},scene_layout{};
    std::array<VkPipeline,6> pipelines{};
    std::array<VkDescriptorSet,6> sets{};
    struct Buffer {VkBuffer handle{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize size{};};
    Buffer input,intermediate,layers,params,scene_params;
    VkImage image{};VkImageView view{};VkDeviceMemory image_memory{};SDL_Texture* texture{};
    unsigned w{},h{},factor{};
#define FUNCTIONS(X) \
    X(vkGetDeviceQueue) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkWaitForFences) X(vkQueueSubmit) X(vkDeviceWaitIdle) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateComputePipelines) X(vkDestroyPipeline) \
    X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)
#define DECLARE(name) PFN_##name name{};
    FUNCTIONS(DECLARE)
#undef DECLARE
    explicit Impl(SDL_Renderer* r):renderer(r) {
        try {initialize();}catch(...){release();throw;}
    }
    ~Impl(){release();}
    uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)
            if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("Native Vulkan sprite memory type unavailable");
    }
    void free_buffer(Buffer& buffer) {
        if(buffer.mapped)vkUnmapMemory(device,buffer.memory);
        if(buffer.handle)vkDestroyBuffer(device,buffer.handle,nullptr);
        if(buffer.memory)vkFreeMemory(device,buffer.memory,nullptr);
        buffer={};
    }
    void allocate(Buffer& buffer,VkDeviceSize bytes,VkBufferUsageFlags usage,bool host) {
        free_buffer(buffer);
        VkBufferCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;ci.size=bytes;ci.usage=usage;
        checked(vkCreateBuffer(device,&ci,nullptr,&buffer.handle),"Create native sprite buffer");
        VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,buffer.handle,&requirements);
        VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=requirements.size;
        ai.memoryTypeIndex=memory_type(requirements.memoryTypeBits,host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checked(vkAllocateMemory(device,&ai,nullptr,&buffer.memory),"Allocate native sprite buffer");
        checked(vkBindBufferMemory(device,buffer.handle,buffer.memory,0),"Bind native sprite buffer");
        if(host)checked(vkMapMemory(device,buffer.memory,0,bytes,0,&buffer.mapped),"Map native sprite buffer");
        buffer.size=bytes;
    }
    void wait() {
        if(pending){checked(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"Wait for native sprite compute");pending=false;}
    }
    void drain() {
        // SDL batches reads of the previous output. A target switch submits
        // those commands; native SDL Vulkan waits before recycling its batch.
        // Keep this ordering even for paused queued reads and resource resize.
        auto* target=SDL_GetRenderTarget(renderer);
        if(!target)throw std::runtime_error("Native Vulkan sprites require a render target");
        checked(SDL_FlushRenderer(renderer),"Flush native sprite reads");
        checked(SDL_SetRenderTarget(renderer,nullptr),"Submit native sprite reads");
        checked(SDL_SetRenderTarget(renderer,target),"Restore native sprite target");
        wait();
    }
    void initialize() {
        if(std::string_view(SDL_GetRendererName(renderer)?SDL_GetRendererName(renderer):"")!="vulkan")
            throw std::runtime_error("Native Vulkan sprites require SDL Vulkan");
        const auto props=SDL_GetRendererProperties(renderer);
        auto instance=static_cast<VkInstance>(SDL_GetPointerProperty(props,SDL_PROP_RENDERER_VULKAN_INSTANCE_POINTER,nullptr));
        physical=static_cast<VkPhysicalDevice>(SDL_GetPointerProperty(props,SDL_PROP_RENDERER_VULKAN_PHYSICAL_DEVICE_POINTER,nullptr));
        device=static_cast<VkDevice>(SDL_GetPointerProperty(props,SDL_PROP_RENDERER_VULKAN_DEVICE_POINTER,nullptr));
        auto entry=reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        if(!instance||!physical||!device||!entry)throw std::runtime_error("Native sprite Vulkan device unavailable");
        auto get=reinterpret_cast<PFN_vkGetDeviceProcAddr>(entry(instance,"vkGetDeviceProcAddr"));
        auto get_memory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(entry(instance,"vkGetPhysicalDeviceMemoryProperties"));
        if(!get||!get_memory)throw std::runtime_error("Native sprite Vulkan entry points unavailable");
#define LOAD(name) name=reinterpret_cast<PFN_##name>(get(device,#name));if(!name)throw std::runtime_error("Missing native sprite function: " #name);
        FUNCTIONS(LOAD)
#undef LOAD
#undef FUNCTIONS
        get_memory(physical,&memory);
        auto family=uint32_t(SDL_GetNumberProperty(props,SDL_PROP_RENDERER_VULKAN_GRAPHICS_QUEUE_FAMILY_INDEX_NUMBER,0));
        vkGetDeviceQueue(device,family,0,&queue);
        VkCommandPoolCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;ci.queueFamilyIndex=family;
        checked(vkCreateCommandPool(device,&ci,nullptr,&pool),"Create native sprite command pool");
        VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
        checked(vkAllocateCommandBuffers(device,&ca,&commands),"Allocate native sprite commands");
        VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        checked(vkCreateFence(device,&fi,nullptr,&fence),"Create native sprite fence");
        const auto make_layout=[&](std::span<const VkDescriptorSetLayoutBinding> bindings,VkDescriptorSetLayout& result){
            VkDescriptorSetLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;info.bindingCount=uint32_t(bindings.size());info.pBindings=bindings.data();
            checked(vkCreateDescriptorSetLayout(device,&info,nullptr,&result),"Create native sprite descriptor layout");
        };
        const VkDescriptorSetLayoutBinding storage{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        const VkDescriptorSetLayoutBinding uniform{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        const std::array<VkDescriptorSetLayoutBinding,2> output{{{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
        make_layout({&storage,1},storage_layout);make_layout({&uniform,1},uniform_layout);make_layout(output,output_layout);
        const std::array<VkDescriptorSetLayout,6> layouts{storage_layout,output_layout,uniform_layout,storage_layout,storage_layout,uniform_layout};
        VkPipelineLayoutCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pi.setLayoutCount=3;pi.pSetLayouts=layouts.data();
        checked(vkCreatePipelineLayout(device,&pi,nullptr,&layout),"Create native sprite pipeline layout");pi.pSetLayouts=layouts.data()+3;
        checked(vkCreatePipelineLayout(device,&pi,nullptr,&scene_layout),"Create native scene pipeline layout");
        const std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2}}};
        VkDescriptorPoolCreateInfo di{};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;di.maxSets=6;di.poolSizeCount=uint32_t(sizes.size());di.pPoolSizes=sizes.data();
        checked(vkCreateDescriptorPool(device,&di,nullptr,&descriptors),"Create native sprite descriptor pool");
        VkDescriptorSetAllocateInfo da{};da.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;da.descriptorPool=descriptors;da.descriptorSetCount=6;da.pSetLayouts=layouts.data();
        checked(vkAllocateDescriptorSets(device,&da,sets.data()),"Allocate native sprite descriptors");
        using namespace GPUShaders;
        const std::array<std::span<const unsigned char>,6> binaries{sprite_pass0_spv,sprite_pass1_spv,sprite_pass2_spv,sprite_pass3_spv,sprite_pass4_spv,sprite_scene_spv};
        for(unsigned i=0;i<6;++i){
            std::vector<uint32_t> code(binaries[i].size()/4);std::memcpy(code.data(),binaries[i].data(),binaries[i].size());
            VkShaderModuleCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;si.codeSize=binaries[i].size();si.pCode=code.data();
            VkShaderModule shader{};checked(vkCreateShaderModule(device,&si,nullptr,&shader),"Create native sprite shader");
            VkComputePipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;info.layout=i==5?scene_layout:layout;
            info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;info.stage.module=shader;info.stage.pName="main";
            const auto result=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&pipelines[i]);
            vkDestroyShaderModule(device,shader,nullptr);checked(result,"Create native sprite pipeline");
        }
        allocate(params,16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true);allocate(scene_params,32,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true);
    }
    void release_output() {
        SDL_DestroyTexture(texture);texture=nullptr;
        if(view)vkDestroyImageView(device,view,nullptr);view={};
        if(image)vkDestroyImage(device,image,nullptr);image={};
        if(image_memory)vkFreeMemory(device,image_memory,nullptr);image_memory={};
        w=h=factor=0;
    }
    void release() {
        if(!vkDeviceWaitIdle)return;
        vkDeviceWaitIdle(device);release_output();
        for(auto* buffer:{&input,&intermediate,&layers,&params,&scene_params})free_buffer(*buffer);
        for(auto pipeline:pipelines)if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(scene_layout)vkDestroyPipelineLayout(device,scene_layout,nullptr);
        if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);
        for(auto set_layout:{storage_layout,output_layout,uniform_layout})if(set_layout)vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
        if(fence)vkDestroyFence(device,fence,nullptr);
        if(pool)vkDestroyCommandPool(device,pool,nullptr);
    }
    void resize(unsigned width,unsigned height,unsigned scale) {
        release_output();
        const auto n=VkDeviceSize(width)*height;
        allocate(input,n*12,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true);allocate(intermediate,n*64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,false);
        VkImageCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent={width*scale,height*scale,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
        ci.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        checked(vkCreateImage(device,&ci,nullptr,&image),"Create native sprite output");
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,image,&requirements);
        VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=requirements.size;ai.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checked(vkAllocateMemory(device,&ai,nullptr,&image_memory),"Allocate native sprite image");checked(vkBindImageMemory(device,image,image_memory,0),"Bind native sprite image");
        VkImageViewCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ci.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        checked(vkCreateImageView(device,&vi,nullptr,&view),"Create native sprite storage view");
        auto props=SDL_CreateProperties();SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_ABGR8888);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_TARGET);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,width*scale);SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,height*scale);
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_VULKAN_TEXTURE_NUMBER,Sint64(uintptr_t(image)));
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_VULKAN_LAYOUT_NUMBER,VK_IMAGE_LAYOUT_UNDEFINED);
        texture=SDL_CreateTextureWithProperties(renderer,props);SDL_DestroyProperties(props);
        checked(texture && SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE) && SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST),"Wrap native sprite texture");
        auto* target=SDL_GetRenderTarget(renderer);
        checked(SDL_SetRenderTarget(renderer,texture),"Initialize native sprite layout");
        checked(SDL_SetRenderTarget(renderer,nullptr),"Submit native sprite layout");
        checked(SDL_SetRenderTarget(renderer,target),"Restore native sprite destination");
        w=width;h=height;factor=scale;
    }
    void update_descriptors() {
        const std::array<VkDescriptorBufferInfo,6> buffers{{{input.handle,0,input.size},{intermediate.handle,0,intermediate.size},{params.handle,0,params.size},{layers.handle,0,layers.size},{input.handle,0,input.size},{scene_params.handle,0,scene_params.size}}};
        std::array<VkWriteDescriptorSet,7> writes{};
        for(unsigned i=0;i<6;++i){auto& write=writes[i];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=sets[i];write.dstBinding=i==1?1:0;write.descriptorCount=1;write.descriptorType=i==2||i==5?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffers[i];}
        const VkDescriptorImageInfo output{VK_NULL_HANDLE,view,VK_IMAGE_LAYOUT_GENERAL};
        auto& write=writes[6];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=sets[1];write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;write.pImageInfo=&output;
        vkUpdateDescriptorSets(device,uint32_t(writes.size()),writes.data(),0,nullptr);
    }
    void image_barrier(bool before) {
        VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout=before?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout=before?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask=before?VK_ACCESS_SHADER_READ_BIT:VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask=before?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(commands,before?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,before?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    void compute_barrier() {
        VkMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    }
};
NativeVulkanSpriteScaler::NativeVulkanSpriteScaler(SDL_Renderer* renderer):impl(std::make_unique<Impl>(renderer)){}
NativeVulkanSpriteScaler::~NativeVulkanSpriteScaler()=default;
SDL_Texture* NativeVulkanSpriteScaler::compose(std::span<const uint32_t> background,std::span<const uint32_t> sprites,
        unsigned width,unsigned height,SpriteFilter filter,std::span<const uint8_t> protection,
        std::span<const uint32_t> heads,std::span<const Presentation::Fragment> fragments,
        unsigned clouds,unsigned trails,unsigned explosions,unsigned afterburner,bool renderer_reads_submitted) {
    auto& p=*impl;const size_t n=size_t(width)*height;
    if(!width||!height||width>8192||height>8192||background.size()!=n||sprites.size()!=n||
            (!protection.empty()&&protection.size()!=n)||(!heads.empty()&&heads.size()!=n)||
            unsigned(filter)>=sprite_filter_count||clouds>255||trails>255||explosions>255||afterburner>255)
        throw std::invalid_argument("Invalid native Vulkan sprite planes/filter");
    // Slang begin() has already switched to its source target, submitting all
    // earlier SDL reads. Reuse that boundary rather than submitting two more
    // empty SDL batches. Standalone/queued callers retain the explicit drain.
    if(!renderer_reads_submitted)p.drain();else p.wait();
    if(p.w!=width||p.h!=height||p.factor!=sprite_factor(filter))p.resize(width,height,sprite_factor(filter));
    const auto bytes=std::max<size_t>(32,n*4+fragments.size_bytes());
    if(p.layers.size<bytes)p.allocate(p.layers,std::max<VkDeviceSize>(VkDeviceSize(bytes)*2,2*1024*1024),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true);
    auto* data=static_cast<uint32_t*>(p.input.mapped);std::memcpy(data,background.data(),n*4);std::memcpy(data+n,sprites.data(),n*4);
    for(size_t i=0;i<n;++i)data[n*2+i]=protection.empty()?0:protection[i];
    if(!heads.empty()){std::memcpy(p.layers.mapped,heads.data(),n*4);if(!fragments.empty())std::memcpy(static_cast<uint8_t*>(p.layers.mapped)+n*4,fragments.data(),fragments.size_bytes());}
    const int32_t params[]{int32_t(width),int32_t(height),int32_t(filter),int32_t(p.factor)};
    const int32_t scene[]{int32_t(width),int32_t(height),int32_t(clouds),int32_t(trails),int32_t(explosions),int32_t(afterburner),0,0};
    std::memcpy(p.params.mapped,params,sizeof(params));std::memcpy(p.scene_params.mapped,scene,sizeof(scene));p.update_descriptors();
    checked(p.vkResetCommandBuffer(p.commands,0),"Reset native sprite commands");
    VkCommandBufferBeginInfo bi{};bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checked(p.vkBeginCommandBuffer(p.commands,&bi),"Begin native sprite commands");
    VkMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT;
    p.vkCmdPipelineBarrier(p.commands,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&host,0,nullptr,0,nullptr);
    p.image_barrier(true);
    if(!heads.empty()){
        p.vkCmdBindPipeline(p.commands,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipelines[5]);p.vkCmdBindDescriptorSets(p.commands,VK_PIPELINE_BIND_POINT_COMPUTE,p.scene_layout,0,3,p.sets.data()+3,0,nullptr);
        p.vkCmdDispatch(p.commands,(width+7)/8,(height+7)/8,1);p.compute_barrier();
    }
    p.vkCmdBindDescriptorSets(p.commands,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,3,p.sets.data(),0,nullptr);
    for(unsigned i=filter==SpriteFilter::scalefx?0:4;i<5;++i){
        p.vkCmdBindPipeline(p.commands,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipelines[i]);const auto scale=i==4?p.factor:1;
        p.vkCmdDispatch(p.commands,(width*scale+7)/8,(height*scale+7)/8,1);if(i!=4)p.compute_barrier();
    }
    p.image_barrier(false);checked(p.vkEndCommandBuffer(p.commands),"End native sprite commands");checked(p.vkResetFences(p.device,1,&p.fence),"Reset native sprite fence");
    VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&p.commands;
    checked(p.vkQueueSubmit(p.queue,1,&submit,p.fence),"Submit native sprite compute");p.pending=true;
    // SDL samples the storage result later on the same graphics queue. The
    // image barrier orders that read; no completion wait or readback here.
    return p.texture;
}
}

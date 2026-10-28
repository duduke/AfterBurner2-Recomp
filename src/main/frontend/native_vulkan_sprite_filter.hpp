#pragma once
#ifdef _WIN32
#include <SDL3/SDL.h>
#include <memory>
#include "frontend/sprite_filter.hpp"
#include "hwvideo/sprite_scene.hpp"

namespace Frontend {
// The same generated sprite/scene kernels as SDL GPU, on SDL's native Vulkan
// device and queue. Used while Slang owns the native renderer.
class NativeVulkanSpriteScaler {
public:
    explicit NativeVulkanSpriteScaler(SDL_Renderer*);
    ~NativeVulkanSpriteScaler();
    NativeVulkanSpriteScaler(const NativeVulkanSpriteScaler&)=delete;
    NativeVulkanSpriteScaler& operator=(const NativeVulkanSpriteScaler&)=delete;
    SDL_Texture* compose(std::span<const uint32_t> background,std::span<const uint32_t> sprites,
        unsigned width,unsigned height,SpriteFilter filter,std::span<const uint8_t> protection={},
        std::span<const uint32_t> heads={},std::span<const Presentation::Fragment> fragments={},
        unsigned cloud_alpha=255,unsigned trail_alpha=255,unsigned explosion_alpha=255,unsigned afterburner_alpha=255,
        bool renderer_reads_submitted=false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
#endif

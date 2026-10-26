#pragma once
#ifdef __APPLE__
#include <SDL3/SDL.h>
#include <memory>
#include "frontend/sprite_filter.hpp"
#include "hwvideo/sprite_scene.hpp"

namespace Frontend {
// Runs the existing sprite compute kernels with SDL's native Metal renderer.
// The optional Slang runtime needs this renderer instead of SDL GPU.
class NativeMetalSpriteScaler {
public:
    explicit NativeMetalSpriteScaler(SDL_Renderer*);
    ~NativeMetalSpriteScaler();
    NativeMetalSpriteScaler(const NativeMetalSpriteScaler&)=delete;
    NativeMetalSpriteScaler& operator=(const NativeMetalSpriteScaler&)=delete;
    SDL_Texture* compose(std::span<const uint32_t> background,std::span<const uint32_t> sprites,
        unsigned width,unsigned height,SpriteFilter filter,std::span<const uint8_t> protection={},
        std::span<const uint32_t> heads={},std::span<const Presentation::Fragment> fragments={},
        unsigned cloud_alpha=255,unsigned trail_alpha=255,unsigned explosion_alpha=255,unsigned afterburner_alpha=255);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
#endif

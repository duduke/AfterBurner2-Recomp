#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include "frontend/native_settings.hpp"

namespace Frontend {

// Optional display treatment, applied after game/HUD composition and before
// frontend UI. The cached multiplicative texture needs no game-image copy,
// framebuffer readback, render target or changes to the emulated board.
class NativeCrt {
public:
    explicit NativeCrt(SDL_Renderer* renderer):renderer(renderer) {}
    NativeCrt(const NativeCrt&)=delete;
    NativeCrt& operator=(const NativeCrt&)=delete;

    size_t cached_bytes() const {return bytes;}
    unsigned rebuilds() const {return generations;}
    void clear() {overlay.reset();bytes=0;}

    // Caller viewport/clip do not restrict this final game-image pass; all
    // renderer coordinate state is restored before UI drawing/event conversion.
    // Failure retains the last accepted cache and reports an SDL error.
    bool draw(const Settings& settings) {
        if(!renderer || static_cast<unsigned>(settings.crt)>=crt_count ||
           settings.crt_scanlines>100 || settings.crt_mask>100)
            return SDL_SetError("Invalid CRT mode or strength");
        if(settings.crt==Crt::off || settings.crt==Crt::retroarch || (!settings.crt_scanlines && !settings.crt_mask)) {
            clear();return true;
        }
        State state;
        if(!state.capture(renderer))return false;
        SDL_FRect area;
        if(!SDL_GetRenderLogicalPresentationRect(renderer,&area))return false;
        int output_w{},output_h{};
        if(auto* target=SDL_GetRenderTarget(renderer)) {
            float w{},h{};if(!SDL_GetTextureSize(target,&w,&h))return false;
            output_w=int(w);output_h=int(h);
        } else if(!SDL_GetRenderOutputSize(renderer,&output_w,&output_h))return false;
        // A minimized window has no drawable area. Defer until restored.
        if(output_w<=0 || output_h<=0 || area.w<=0 || area.h<=0)return true;
        Key next{area,std::clamp(int(std::floor(area.x)),0,output_w),
            std::clamp(int(std::floor(area.y)),0,output_h),
            std::clamp(int(std::ceil(area.x+area.w)),0,output_w),
            std::clamp(int(std::ceil(area.y+area.h)),0,output_h),
            settings.crt_scanlines,settings.crt_mask};
        if(next.right<=next.left || next.bottom<=next.top)return true;
        if(!overlay || !(next==key))if(!build(next))return false;

        // Use physical output pixels: a mask stripe is exactly one output pixel
        // at any DPI, and scanlines follow the 224-row native raster even with
        // a fractional letterbox scale or a centered wider viewport.
        bool success=SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
        success=SDL_SetRenderScale(renderer,1,1) && success;
        success=SDL_SetRenderViewport(renderer,nullptr) && success;
        success=SDL_SetRenderClipRect(renderer,nullptr) && success;
        const SDL_FRect destination{float(key.left),float(key.top),float(key.right-key.left),float(key.bottom-key.top)};
        if(success)success=SDL_RenderTexture(renderer,overlay.get(),nullptr,&destination);
        const std::string error=success?std::string{}:SDL_GetError();
        const bool restored=state.restore(renderer);
        if(!success)SDL_SetError("CRT display pass: %s",error.c_str());
        return success && restored;
    }

private:
    struct DeleteTexture {void operator()(SDL_Texture* value)const{SDL_DestroyTexture(value);}};
    using Texture=std::unique_ptr<SDL_Texture,DeleteTexture>;
    struct Key {
        SDL_FRect area{};int left{},top{},right{},bottom{};unsigned scanlines{},mask{};
        bool operator==(const Key& other) const {
            return area.x==other.area.x && area.y==other.area.y && area.w==other.area.w && area.h==other.area.h &&
                left==other.left && top==other.top && right==other.right && bottom==other.bottom &&
                scanlines==other.scanlines && mask==other.mask;
        }
    };
    struct State {
        int width{},height{};SDL_RendererLogicalPresentation mode{};
        float scale_x{},scale_y{};SDL_Rect viewport{},clip{};bool has_viewport{},has_clip{};
        bool capture(SDL_Renderer* renderer) {
            has_viewport=SDL_RenderViewportSet(renderer);has_clip=SDL_RenderClipEnabled(renderer);
            return SDL_GetRenderLogicalPresentation(renderer,&width,&height,&mode) &&
                SDL_GetRenderScale(renderer,&scale_x,&scale_y) && SDL_GetRenderViewport(renderer,&viewport) &&
                SDL_GetRenderClipRect(renderer,&clip);
        }
        bool restore(SDL_Renderer* renderer) const {
            bool result=SDL_SetRenderLogicalPresentation(renderer,width,height,mode);
            result=SDL_SetRenderScale(renderer,scale_x,scale_y) && result;
            result=SDL_SetRenderViewport(renderer,has_viewport?&viewport:nullptr) && result;
            result=SDL_SetRenderClipRect(renderer,has_clip?&clip:nullptr) && result;
            return result;
        }
    };
    SDL_Renderer* renderer{};
    Texture overlay;
    Key key;
    size_t bytes{};
    unsigned generations{};
    static constexpr size_t max_bytes=64*1024*1024;

    bool build(const Key& next) {
        const int width=next.right-next.left,height=next.bottom-next.top;
        const size_t required=size_t(width)*height*4;
        if(required>max_bytes)return SDL_SetError("CRT overlay exceeds the 64 MiB display limit");
        Texture replacement(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height));
        if(!replacement || !SDL_SetTextureScaleMode(replacement.get(),SDL_SCALEMODE_NEAREST) ||
           !SDL_SetTextureBlendMode(replacement.get(),SDL_BLENDMODE_MOD))return false;
        void* pixels{};int pitch{};
        if(!SDL_LockTexture(replacement.get(),nullptr,&pixels,&pitch))return false;
        // Presentation policy, not arcade behaviour. Reduce scanline contrast
        // below 2 output pixels per row to reduce aliasing at small window sizes.
        const double amplitude=next.scanlines*.01*std::clamp(next.area.h/224.0-1.0,0.0,1.0);
        const double dim=1.0-next.mask*.01;
        for(int y=0;y<height;++y) {
            auto* row=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels)+size_t(y)*pitch);
            const double physical_y=next.top+y+.5;
            // Align beam/gap phase to output pixel rows. Sampling at pixel
            // centers instead makes both rows equally dark at exactly 2x.
            const double native_y=(physical_y-.5-next.area.y)*224.0/next.area.h;
            const double gain=1.0-amplitude*(.5+.5*std::cos(2.0*std::numbers::pi*native_y));
            const uint32_t bright=uint32_t(std::lround(255*gain)),dark=uint32_t(std::lround(255*gain*dim));
            const uint32_t stripes[]{0xff000000u|(bright<<16)|(dark<<8)|dark,
                0xff000000u|(dark<<16)|(bright<<8)|dark,0xff000000u|(dark<<16)|(dark<<8)|bright};
            for(int x=0;x<width;++x) {
                const double physical_x=next.left+x+.5;
                const bool inside=physical_x>=next.area.x && physical_x<next.area.x+next.area.w &&
                    physical_y>=next.area.y && physical_y<next.area.y+next.area.h;
                row[x]=inside?stripes[(next.left+x)%3]:0xffffffffu;
            }
        }
        SDL_UnlockTexture(replacement.get());
        overlay.swap(replacement);key=next;bytes=required;++generations;
        return true;
    }
};

}

#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "frontend/native_settings.hpp"
#include "frontend/sprite_filter.hpp"
#include "hwvideo/hud_scene.hpp"
#include "roms.hpp"

namespace Frontend {

// Text-plane presentation only. Hardware entries and palette colors come from
// the completed native render; no board state, timing or gameplay is changed.
class NativeHud {
public:
    NativeHud(SDL_Renderer* renderer,const Roms& rom):renderer(renderer) {
        const auto third=rom.tiles.length/3;
        if(!renderer || !rom.tiles.rom || rom.tiles.length%3 || third<512*8)
            throw std::invalid_argument("HUD requires a renderer and 512 planar ROM tiles");
        SpriteScaler scaler;
        const std::array<uint32_t,64> transparent{};
        for(unsigned code=0;code<512;++code) {
            std::array<uint32_t,64> source{};
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
                // Same planar decode as HWTiles::init_gfx: first third is bit 0.
                const auto offset=code*8+y,shift=7-x;
                const uint8_t pen=((rom.tiles.rom[offset]>>shift)&1) |
                    (((rom.tiles.rom[third+offset]>>shift)&1)<<1) |
                    (((rom.tiles.rom[2*third+offset]>>shift)&1)<<2);
                original[code][y*8+x]=pen;
                source[y*8+x]=pen?0xff000000u|pen:0;
                has_original[code]|=pen!=0;
            }
            // Scale categorical pens, then resolve the live palette. This
            // preserves multicolor text and never averages palette indices.
            const auto scaled=scaler.compose(transparent,source,8,8,SpriteFilter::scale3x);
            for(unsigned i=0;i<24*24;++i)sharp[code][i]=scaled[i]&7;
        }
    }
    NativeHud(const NativeHud&)=delete;
    NativeHud& operator=(const NativeHud&)=delete;

    // Decoded ROM/custom pens survive a display-backend change. Release all
    // renderer textures before its owner destroys the old SDL renderer.
    void rebind(SDL_Renderer* next) {
        cache.clear();order.clear();cache_bytes=0;renderer=next;
    }

    bool custom_available() const {return custom_edge!=0;}
    const std::string& custom_error() const {return error;}

    // A 16-column, 32-row RGBA atlas addresses the 512 native text codes.
    // Alpha is coverage; nontransparent R bytes are categorical pens 1..7.
    // Validation and decoding complete before replacing the accepted atlas.
    bool load_custom(const std::filesystem::path& path) {
        error.clear();
        Surface loaded;
#if SDL_VERSION_ATLEAST(3,4,0)
        loaded.reset(SDL_LoadPNG(path.string().c_str()));
#endif
        if(!loaded)loaded.reset(SDL_LoadBMP(path.string().c_str()));
        if(!loaded){error=SDL_GetError();return false;}
        if(loaded->w%16 || loaded->h%32 || loaded->w/16!=loaded->h/32 ||
           loaded->w/16<16 || loaded->w/16>128) {
            error="HUD atlas must contain 16 x 32 square tiles, each 16..128 pixels";return false;
        }
        Surface rgba(SDL_ConvertSurface(loaded.get(),SDL_PIXELFORMAT_RGBA32));
        if(!rgba){error=SDL_GetError();return false;}
        const unsigned edge=rgba->w/16;
        std::vector<CustomPixel> replacement(size_t(512)*edge*edge);
        std::array<bool,512> present{};
        for(unsigned code=0;code<512;++code)for(unsigned y=0;y<edge;++y) {
            const auto* row=static_cast<const uint8_t*>(rgba->pixels)+(code/16*edge+y)*rgba->pitch;
            for(unsigned x=0;x<edge;++x) {
                const auto* pixel=row+(code%16*edge+x)*4;
                if(pixel[3] && (pixel[0]<1 || pixel[0]>7)) {
                    error="HUD atlas nontransparent pixels require red pen values 1..7";return false;
                }
                replacement[(size_t(code)*edge+y)*edge+x]={pixel[3]?pixel[0]:uint8_t(0),pixel[3]};
                present[code]|=pixel[3]!=0;
            }
        }
        custom_pixels.swap(replacement);has_custom=present;custom_edge=edge;
        cache.clear();order.clear();cache_bytes=0;
        return true;
    }

    // SDL's current logical viewport owns the output transform. Glyphs remain
    // in the centered 320x224 safe area, including the two wider viewports.
    // Cropped source/destination rectangles enforce native pixel boundaries
    // without changing the caller's renderer clip, blend, viewport or color.
    void draw(const Presentation::HudScene& scene,Hud mode,unsigned viewport_width) {
        if(mode==Hud::original || scene.clips.empty())return;
        if(static_cast<unsigned>(mode)>static_cast<unsigned>(Hud::custom) ||
           viewport_width<320 || scene.occlusion.size()!=320*224)
            throw std::invalid_argument("invalid HUD presentation scene");
        const float left=float(viewport_width-320)/2;
        for(const auto& clip:scene.clips) {
            if(clip.top>clip.bottom || clip.bottom>=224)
                throw std::invalid_argument("invalid HUD partial-render clip");
            for(unsigned ty=clip.top/8;ty<=clip.bottom/8;++ty)for(unsigned tx=0;tx<40;++tx) {
                const auto entry=clip.entries[ty*40+tx];const unsigned code=entry&0x1ff;
                if(!(mode==Hud::custom && has_custom[code]) && !has_original[code])continue;
                const unsigned first=std::max(clip.top,ty*8),last=std::min(clip.bottom,ty*8+7);
                const auto status=scene.occlusion[first*320+tx*8];bool uniform=status<2;
                for(unsigned y=first;uniform && y<=last;++y)for(unsigned x=0;x<8;++x)
                    if(scene.occlusion[y*320+tx*8+x]!=status){uniform=false;break;}
                if(uniform) {
                    auto& glyph=texture(code,entry,clip.colors,mode,status);
                    render(glyph,left+tx*8,ty*8,0,first-ty*8,8,last-first+1);
                } else for(unsigned y=first;y<=last;++y)for(unsigned x=0;x<8;) {
                    const auto effect=scene.occlusion[y*320+tx*8+x];unsigned end=x+1;
                    while(end<8 && scene.occlusion[y*320+tx*8+end]==effect)++end;
                    if(effect<2) {
                        auto& glyph=texture(code,entry,clip.colors,mode,effect);
                        render(glyph,left+tx*8,ty*8,x,y-ty*8,end-x,1);
                    }
                    x=end;
                }
            }
        }
    }

private:
    struct SurfaceDelete {void operator()(SDL_Surface* p)const{SDL_DestroySurface(p);}};
    struct TextureDelete {void operator()(SDL_Texture* p)const{SDL_DestroyTexture(p);}};
    using Surface=std::unique_ptr<SDL_Surface,SurfaceDelete>;
    using Texture=std::unique_ptr<SDL_Texture,TextureDelete>;
    struct CustomPixel {uint8_t pen,alpha;};
    struct Key {
        unsigned code{},mode{};std::array<uint32_t,8> colors{};
        auto operator<=>(const Key&) const=default;
    };
    struct Glyph {Texture texture;unsigned edge{};};
    SDL_Renderer* renderer;
    std::array<std::array<uint8_t,64>,512> original{};
    std::array<std::array<uint8_t,24*24>,512> sharp{};
    std::array<bool,512> has_original{},has_custom{};
    std::vector<CustomPixel> custom_pixels;
    unsigned custom_edge{};
    std::string error;
    std::map<Key,Glyph> cache;
    std::deque<Key> order;
    size_t cache_bytes{};
    static constexpr size_t max_cache_entries=1024,max_cache_bytes=32*1024*1024;

    Glyph& texture(unsigned code,uint16_t entry,const std::array<uint32_t,128>& colors,Hud mode,unsigned effect) {
        Key key{code,static_cast<unsigned>(mode),{}};
        const auto base=((entry>>9)&7)*8+effect*64;
        std::copy_n(colors.begin()+base,8,key.colors.begin());
        if(auto found=cache.find(key);found!=cache.end())return found->second;
        const bool replacement=mode==Hud::custom && has_custom[code];
        const bool smoothed=mode==Hud::sharp;
        const unsigned edge=replacement?custom_edge:smoothed?24:8;
        Surface image(SDL_CreateSurface(edge,edge,SDL_PIXELFORMAT_ARGB8888));
        if(!image)throw std::runtime_error(SDL_GetError());
        for(unsigned y=0;y<edge;++y) {
            auto* row=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(image->pixels)+y*image->pitch);
            for(unsigned x=0;x<edge;++x) {
                const auto custom=replacement?custom_pixels[(size_t(code)*edge+y)*edge+x]:CustomPixel{};
                const auto pen=replacement?custom.pen:smoothed?sharp[code][y*edge+x]:original[code][y*edge+x];
                uint32_t color=0;
                if(pen) {
                    const unsigned alpha=replacement?custom.alpha:255;color=alpha<<24;
                    // Linear sampling must interpolate premultiplied colors,
                    // including the zero-alpha border, to retain AA coverage.
                    for(unsigned shift:{0u,8u,16u})
                        color|=((((key.colors[pen]>>shift)&255)*alpha+127)/255)<<shift;
                }
                row[x]=color;
            }
        }
        Texture created(SDL_CreateTextureFromSurface(renderer,image.get()));
        if(!created || !SDL_SetTextureBlendMode(created.get(),SDL_BLENDMODE_BLEND_PREMULTIPLIED) ||
           !SDL_SetTextureScaleMode(created.get(),replacement || smoothed?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST))
            throw std::runtime_error(SDL_GetError());
        const size_t bytes=size_t(edge)*edge*4;
        while(!order.empty() && (cache.size()>=max_cache_entries || cache_bytes+bytes>max_cache_bytes)) {
            auto found=cache.find(order.front());cache_bytes-=size_t(found->second.edge)*found->second.edge*4;
            cache.erase(found);order.pop_front();
        }
        order.push_back(key);cache_bytes+=bytes;
        return cache.emplace(std::move(key),Glyph{std::move(created),edge}).first->second;
    }
    void render(const Glyph& glyph,float x,float y,unsigned clip_x,unsigned clip_y,unsigned width,unsigned height) {
        const float factor=float(glyph.edge)/8;
        const SDL_FRect source{clip_x*factor,clip_y*factor,width*factor,height*factor};
        const SDL_FRect destination{x+clip_x,y+clip_y,float(width),float(height)};
        if(!SDL_RenderTexture(renderer,glyph.texture.get(),&source,&destination))
            throw std::runtime_error(SDL_GetError());
    }
};

}

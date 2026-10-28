#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace Frontend {
// Authored full-color atmosphere, independent of native tile/sprite budgets.
class CustomStageClouds {
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture{nullptr,SDL_DestroyTexture};
public:
    unsigned draw(SDL_Renderer* renderer,double horizon,double lateral,double pitch,bool night){
        if(!texture){
            auto* surface=SDL_CreateSurface(384,128,SDL_PIXELFORMAT_RGBA32);if(!surface)throw std::runtime_error(SDL_GetError());
            struct Puff{double x,y,rx,ry;};
            const Puff puffs[]={{.19,.62,.14,.16},{.34,.47,.16,.25},{.50,.37,.15,.27},{.65,.51,.18,.23},{.81,.64,.14,.14}};
            for(int y=0;y<128;++y){auto* row=static_cast<Uint8*>(surface->pixels)+y*surface->pitch;
                for(int x=0;x<384;++x){double density=0;for(auto p:puffs){const double dx=(x/384.-p.x)/p.rx,dy=(y/128.-p.y)/p.ry;density+=std::exp(-2*(dx*dx+dy*dy));}
                    const double fade=std::clamp((density-.025)*1.1,0.,1.);row[x*4]=row[x*4+1]=row[x*4+2]=Uint8(242-22*y/128.);row[x*4+3]=Uint8(100*fade);}}
            texture.reset(SDL_CreateTextureFromSurface(renderer,surface));SDL_DestroySurface(surface);
            if(!texture)throw std::runtime_error(SDL_GetError());
            SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR);
        }
        SDL_SetTextureColorMod(texture.get(),night?105:255,night?125:255,night?165:255);
        const double sky=std::max(55.,horizon-78);unsigned count=0;
        for(unsigned i=0;i<4;++i){const float width=330+float(i%2)*100;
            const float x=float(5+i*252-std::clamp(lateral*1.3,-70.,70.));
            const float y=float(78+sky*(.10+.15*(i%3))+pitch*5);
            // Fade the lower wisps before the horizon, rather than clipping an opaque edge.
            const float height=float(std::min(100.,sky*.68));SDL_FRect dest{x-width*.25f,y,width,height};
            SDL_RenderTexture(renderer,texture.get(),nullptr,&dest);++count;
        }
        return count;
    }
};
}

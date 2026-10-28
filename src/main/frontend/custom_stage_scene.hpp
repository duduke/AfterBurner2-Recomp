#pragma once
#include <SDL3/SDL.h>
#include "frontend/custom_stage.hpp"
#include <memory>
#include <map>

namespace Frontend {
// A direct RGBA scene compositor. ROM art is optional alongside these commands,
// never an allocation scheme, palette restriction, or tile/sprite budget.
class CustomStageScene {
    using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
    SDL_Renderer* renderer;const CustomStageDefinition* definition{};
    std::map<std::filesystem::path,Texture> textures;size_t next{};
public:
    uint64_t drawn{},tiles_drawn{};
    explicit CustomStageScene(SDL_Renderer* r):renderer(r){}
    void load(const CustomStageDefinition& stage){
        definition=&stage;textures.clear();
        for(const auto& layer:stage.layers)for(const auto& e:layer.elements)if(e.type==CustomStageElement::Type::image){
            auto path=(stage.asset_directory/e.asset).lexically_normal();if(textures.contains(path))continue;
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(nullptr,SDL_DestroySurface);
#if SDL_VERSION_ATLEAST(3,4,0)
            image.reset(SDL_LoadPNG(path.string().c_str()));
#endif
            if(!image)image.reset(SDL_LoadBMP(path.string().c_str()));
            if(!image)throw std::runtime_error("Cannot load custom image "+path.string()+": "+SDL_GetError());
            // Full RGBA, without indexed pens, palette remapping, or ROM LODs.
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> rgba(SDL_ConvertSurface(image.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            if(!rgba)throw std::runtime_error(SDL_GetError());Texture texture(SDL_CreateTextureFromSurface(renderer,rgba.get()),SDL_DestroyTexture);
            if(!texture)throw std::runtime_error("Custom texture exceeds host renderer capabilities: "+std::string(SDL_GetError()));
            SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR);
            textures.emplace(std::move(path),std::move(texture));
        }
    }
    void begin(){next=0;drawn=tiles_drawn=0;}
    void draw_before(double order,const StageCamera& camera){
        if(!definition)return;
        while(next<definition->layers.size() && definition->layers[next].order<order){const auto& layer=definition->layers[next++];
            for(const auto& e:layer.elements){double x=e.x,y=e.y,w=e.width,h=e.height;
                if(layer.world){const auto p=camera.project(e.x,e.y,e.z);if(!p.visible)continue;x=p.x;y=p.y;w=camera.span(w,e.z);h=camera.span(h,e.z);}
                if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h) || w<=0 || h<=0)continue;
                // Cull subpixel repeated cells and offscreen cells before
                // iteration. Tile storage/counts have no arcade-sized limit.
                if((e.columns>1 || e.rows>1) && (w<1 || h<1))continue;
                const double pad=e.rotation==0?0:std::hypot(w,h);
                const double left=layer.order<1000?40:0,top=layer.order<1000?78:0,right=layer.order<1000?920:960,bottom=layer.order<1000?470:600;
                const auto first_col=size_t(std::clamp(std::floor((left-pad-x)/w),0.,double(e.columns)));
                const auto last_col=size_t(std::clamp(std::ceil((right+pad-x)/w),0.,double(e.columns)));
                const auto first_row=size_t(std::clamp(std::floor((top-pad-y)/h),0.,double(e.rows)));
                const auto last_row=size_t(std::clamp(std::ceil((bottom+pad-y)/h),0.,double(e.rows)));
                SDL_Texture* texture=nullptr;if(e.type==CustomStageElement::Type::image){texture=textures.at((definition->asset_directory/e.asset).lexically_normal()).get();SDL_SetTextureColorMod(texture,e.color[0],e.color[1],e.color[2]);SDL_SetTextureAlphaMod(texture,e.color[3]);}
                for(size_t row=first_row;row<last_row;++row)for(size_t col=first_col;col<last_col;++col){
                    const double xx=x+col*w,yy=y+row*h;
                    if(std::max({std::abs(xx),std::abs(yy),w,h})>std::numeric_limits<float>::max()/4)continue;
                    const SDL_FRect destination{float(xx),float(yy),float(w),float(h)};
                    if(texture)SDL_RenderTextureRotated(renderer,texture,nullptr,&destination,std::remainder(e.rotation,360.),nullptr,SDL_FLIP_NONE);
                    else{SDL_Vertex vertices[4];unsigned i=0;for(auto corner:{StagePoint{-w/2,-h/2},StagePoint{w/2,-h/2},StagePoint{w/2,h/2},StagePoint{-w/2,h/2}}){const auto p=rotate_stage_point(corner,std::remainder(e.rotation,360.));vertices[i++]={{float(xx+w/2+p.x),float(yy+h/2+p.y)},{e.color[0]/255.f,e.color[1]/255.f,e.color[2]/255.f,e.color[3]/255.f},{0,0}};}const int indices[]={0,1,2,0,2,3};SDL_RenderGeometry(renderer,nullptr,vertices,4,indices,6);}
                    ++drawn;if(e.columns>1 || e.rows>1)++tiles_drawn;
                }
            }
        }
    }
};
}

#pragma once
#include <SDL3/SDL.h>
#include "roms.hpp"
#include "hwvideo/hwpalette.hpp"
#include <array>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>

namespace Frontend {
// Original tile graphics and font, freely placed by the custom-stage UI.
// Native snapshots supply widget entries/palettes; no hardware text layer runs.
class CustomStageHud {
    using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
    SDL_Renderer* renderer;
    std::array<std::array<uint8_t,64>,512> pens{};
    std::array<std::array<uint32_t,8>,8> colors{};
    std::array<uint16_t,40*28> entries{};
    Texture font{nullptr,SDL_DestroyTexture},widgets{nullptr,SDL_DestroyTexture};
    void atlas(bool letters){
        auto* surface=SDL_CreateSurface(128,letters?256:2048,SDL_PIXELFORMAT_ARGB8888);if(!surface)throw std::runtime_error(SDL_GetError());
        for(unsigned group=0;group<(letters?1u:8u);++group)for(unsigned code=0;code<512;++code)for(unsigned y=0;y<8;++y){
            auto* row=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels)+(group*256+code/16*8+y)*surface->pitch);
            for(unsigned x=0;x<8;++x){const auto pen=pens[code][y*8+x];row[code%16*8+x]=!pen?0:letters?(pen==7?0xff000000u:0xffffffffu):colors[group][pen];}}
        auto& texture=letters?font:widgets;texture.reset(SDL_CreateTextureFromSurface(renderer,surface));SDL_DestroySurface(surface);
        if(!texture)throw std::runtime_error(SDL_GetError());SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_NEAREST);
    }
public:
    unsigned tiles_drawn{},letters_drawn{};
    CustomStageHud(SDL_Renderer* r,const Roms& rom):renderer(r){
        const auto third=rom.tiles.length/3;if(!third || third<512*8 || rom.tiles.length%3)throw std::runtime_error("Custom HUD requires original tile ROMs");
        for(unsigned code=0;code<512;++code)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto p=code*8+y,shift=7-x;
            pens[code][y*8+x]=((rom.tiles.rom[p]>>shift)&1)|(((rom.tiles.rom[third+p]>>shift)&1)<<1)|(((rom.tiles.rom[third*2+p]>>shift)&1)<<2);}
        // ROM palette defaults permit isolated asset previews. Gameplay replaces
        // these with the actual native text entries and settled live colors.
        const unsigned sources[]={0x30b74,0x30b34,0x30b94,0x30ba4,0x30b14,0x30bc4,0x30b54,0x30b54};
        HWPalette palette(64);for(unsigned g=0;g<8;++g)for(unsigned p=0;p<8;++p){palette.write(g*8+p,rom.maincpu.read16(sources[g]+p*2));const auto c=palette.pen(g*8+p);colors[g][p]=0xff000000u|(unsigned(c.r)<<16)|(unsigned(c.g)<<8)|c.b;}
        entries.fill(0x20);auto put=[&](unsigned x,unsigned y,unsigned v){entries[y*40+x]=v;};
        for(unsigned i=0;i<7;++i){put(2+i,1,0x8c81);put(2+i,2,0x8c82+i);put(2+i,3,i==0?0x8c91:i==6?0x8c93:0x8c92);}
        for(unsigned i=0;i<4;++i){put(20+i,2,0x8cd1+i);put(17+i,25,0x8d20+i);}
        for(unsigned i=0;i<15;++i)put(1+i,25,0x8f07+i);
        put(25,25,0x8bfd);put(25,26,0x8bfe);put(2,26,0x8bf8);
        atlas(true);atlas(false);
    }
    void native(const std::array<uint16_t,40*28>& snapshot,const HWPalette& palette){entries=snapshot;
        for(unsigned g=0;g<8;++g)for(unsigned p=0;p<8;++p){const auto c=palette.pen(0x1c00+g*8+p);colors[g][p]=0xff000000u|(unsigned(c.r)<<16)|(unsigned(c.g)<<8)|c.b;}
        atlas(false);
    }
    void begin(){tiles_drawn=letters_drawn=0;}
    void text(float x,float y,const std::string& label,SDL_Color color,float size=1){
        SDL_SetTextureColorMod(font.get(),color.r,color.g,color.b);SDL_SetTextureAlphaMod(font.get(),color.a);
        for(unsigned char ch:label){const unsigned code=std::toupper(ch);SDL_FRect src{float(code%16*8),float(code/16*8),8,8},dst{x,y,8*size,8*size};SDL_RenderTexture(renderer,font.get(),&src,&dst);x+=8*size;++letters_drawn;}
    }
    void tile(uint16_t entry,float x,float y,float size=1.5){const auto code=entry&511,palette=(entry>>9)&7;
        SDL_FRect src{float(code%16*8),float(palette*256+code/16*8),8,8},dst{x,y,8*size,8*size};SDL_RenderTexture(renderer,widgets.get(),&src,&dst);++tiles_drawn;
    }
    void region(unsigned column,unsigned row,unsigned width,unsigned height,float x,float y,float size=1.5){
        for(unsigned yy=0;yy<height;++yy)for(unsigned xx=0;xx<width;++xx)tile(entries[(row+yy)*40+column+xx],x+xx*8*size,y+yy*8*size,size);
    }
    uint16_t entry(unsigned column,unsigned row)const{return entries.at(row*40+column);}
};
}

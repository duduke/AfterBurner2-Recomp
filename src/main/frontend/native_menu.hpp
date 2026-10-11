#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <functional>
#include <stdexcept>
#include <string>
#include "roms.hpp"
#include "engine/game_session.hpp"
#include "frontend/native_settings.hpp"

namespace Frontend {
struct Performance {
    double simulation_fps{}, display_fps{}, frame_ms{}, core_ms{}, present_ms{}, filter_ms{};
};
// Frontend-owned asset cache and UI. Reads ROM graphics only; never writes the
// board's RAM, palette, frame pixels, registers or device clocks.
class NativeMenu {
public:
    enum class Action { none, close, start, quit };
    NativeMenu(SDL_Renderer* renderer, const Roms& rom, Settings settings, bool pixel_art)
        : settings(settings), renderer(renderer), scale_count(pixel_art?4:3) {
        auto* surface=SDL_CreateSurface(128,256,SDL_PIXELFORMAT_ARGB8888);
        if (!surface) throw std::runtime_error(SDL_GetError());
        const auto third=rom.tiles.length/3;
        for (unsigned tile=0;tile<512;++tile) for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
            const auto offset=tile*8+y;
            const unsigned value=(((rom.tiles.rom[offset]>>(7-x))&1)) |
                (((rom.tiles.rom[third+offset]>>(7-x))&1)<<1) |
                (((rom.tiles.rom[2*third+offset]>>(7-x))&1)<<2);
            auto* row=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels)+(tile/16*8+y)*surface->pitch);
            // Font pen 7 is the dark outline/letter counter, not foreground.
            const bool ink=value!=0 && value!=7;
            row[tile%16*8+x]=ink?0xffffffffu:0;
        }
        atlas=SDL_CreateTextureFromSurface(renderer,surface); SDL_DestroySurface(surface);
        if (!atlas) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureScaleMode(atlas,SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(atlas,SDL_BLENDMODE_BLEND);
    }
    ~NativeMenu() { SDL_DestroyTexture(atlas); }
    NativeMenu(const NativeMenu&)=delete;
    Settings settings;
    std::string notice;
    std::function<bool(const Settings&)> apply;
    void open(GameSession& game) {
        if (opened) return;
        was_paused=game.paused(); initial=game.info().number==0;
        game.set_paused(true); opened=true; options=false; selected=0;
    }
    void close(GameSession& game) {
        if (!opened) return;
        game.set_paused(was_paused); opened=false;
    }
    bool is_open() const { return opened; }
    bool in_options() const { return options; }
    int selection() const { return selected; }
    bool fullscreen_toggle() {
        auto changed=settings;changed.fullscreen=!changed.fullscreen;
        return change(changed);
    }
    bool stats_toggle() {
        auto changed=settings;changed.stats=static_cast<Stats>((static_cast<int>(changed.stats)+1)%3);
        return change(changed);
    }
    Action key(SDL_Keycode key) {
        if (key==SDLK_ESCAPE || key==SDLK_F1) {
            if (options) { options=false;selected=1;return Action::none; }
            return Action::close;
        }
        const int rows=options?5:3;
        if (key==SDLK_UP) selected=(selected+rows-1)%rows;
        if (key==SDLK_DOWN) selected=(selected+1)%rows;
        if (options && (key==SDLK_LEFT || key==SDLK_RIGHT)) adjust(key==SDLK_LEFT?-1:1);
        if (key!=SDLK_RETURN && key!=SDLK_SPACE && key!=SDLK_KP_ENTER) return Action::none;
        if (options) {
            if (selected==4) {options=false;selected=1;} else adjust(1);
            return Action::none;
        }
        if (selected==0) return initial?Action::start:Action::close;
        if (selected==1) {options=true;selected=0;return Action::none;}
        return Action::quit;
    }
    Action pointer(float x,float y,bool click) {
        const int rows=options?5:3;
        for (int i=0;i<rows;++i) {
            const float top=options?98.f+i*17.f:116.f+i*20.f;
            if(x>=32 && x<=296 && y>=top && y<top+(options?17:18)) {
                selected=i;return click?key(SDLK_RETURN):Action::none;
            }
        }
        return Action::none;
    }
    void draw() {
        SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
        title("AFTER",16,SDL_Color{240,244,255,255});
        title("BURNER II",51,SDL_Color{240,244,255,255});
        SDL_SetRenderDrawColor(renderer,45,164,236,255);
        SDL_RenderLine(renderer,40,94,280,94);
        if (options) {
            const char* labels[]{"UPSCALING","SPRITES","SCREEN","STATS","BACK"};
            const char* scales[]{"NEAREST","SMOOTH","INTEGER","PIXEL ART"};
            const char* stats[]{"OFF","FPS","DETAILED"};
            const char* values[]{scales[static_cast<int>(settings.scaling)],sprite_filter_labels[static_cast<int>(settings.sprites)],settings.fullscreen?"FULLSCREEN":"WINDOWED",stats[static_cast<int>(settings.stats)],""};
            for(int i=0;i<5;++i) {
                const SDL_Color color=i==selected?SDL_Color{255,255,255,255}:SDL_Color{135,157,181,255};
                text(labels[i],48,103+i*17,color);
                text(values[i],192,103+i*17,i==selected?SDL_Color{54,184,255,255}:color);
                if(i==selected) cursor(27,103+i*17);
            }
            centered("UP/DOWN SELECT  LEFT/RIGHT CHANGE",195,{125,153,180,255});
        } else {
            const char* labels[]{initial?"START GAME":"RESUME GAME","OPTIONS","QUIT"};
            for(int i=0;i<3;++i) {
                const auto x=160-static_cast<int>(std::char_traits<char>::length(labels[i]))*4;
                text(labels[i],x,122+i*20,i==selected?SDL_Color{255,255,255,255}:SDL_Color{135,157,181,255});
                if(i==selected) cursor(x-32,122+i*20);
            }
            centered("ENTER SELECT   ESC RETURN",193,{125,153,180,255});
        }
        SDL_SetRenderDrawColor(renderer,41,125,190,255); SDL_RenderLine(renderer,24,210,296,210);
        centered(notice.empty()?"AFTER BURNER II / NATIVE EDITION":notice,215,{73,152,210,255});
    }
    void draw_stats(const Performance& p,bool paused) {
        if(settings.stats==Stats::off) return;
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer,0,0,0,210);
        SDL_FRect panel{4,4,152,settings.stats==Stats::detailed?65.f:25.f};SDL_RenderFillRect(renderer,&panel);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
        char line[48];
        std::snprintf(line,sizeof(line),"SIM %5.1f FPS%s",p.simulation_fps,paused?" P":"");text(line,8,8,{255,255,255,255});
        std::snprintf(line,sizeof(line),"DRAW %5.1f FPS",p.display_fps);text(line,8,18,{55,187,255,255});
        if(settings.stats==Stats::detailed) {
            std::snprintf(line,sizeof(line),"FRAME %5.1f MS",p.frame_ms);text(line,8,28,{255,255,255,255});
            std::snprintf(line,sizeof(line),"CORE  %5.1f MS",p.core_ms);text(line,8,38,{255,255,255,255});
            std::snprintf(line,sizeof(line),"FILTER %5.1f MS",p.filter_ms);text(line,8,48,{255,255,255,255});
            std::snprintf(line,sizeof(line),"PRESENT %4.1f MS",p.present_ms);text(line,8,58,{255,255,255,255});
        }
    }
private:
    SDL_Renderer* renderer;
    SDL_Texture* atlas{};
    int scale_count,selected{};
    bool opened{},options{},was_paused{},initial{};
    bool change(const Settings& changed) {
        if (apply && !apply(changed)) {notice="SETTING UNAVAILABLE";return false;}
        settings=changed;return true;
    }
    void adjust(int direction) {
        auto changed=settings;
        if(selected==0) changed.scaling=static_cast<Scaling>((static_cast<int>(settings.scaling)+scale_count+direction)%scale_count);
        if(selected==1) changed.sprites=static_cast<SpriteFilter>((static_cast<int>(settings.sprites)+sprite_filter_count+direction)%sprite_filter_count);
        if(selected==2) changed.fullscreen=!settings.fullscreen;
        if(selected==3) changed.stats=static_cast<Stats>((static_cast<int>(settings.stats)+3+direction)%3);
        if(selected!=4) change(changed);
    }
    void tile(unsigned code,float x,float y,SDL_Color color,float scale=1) {
        SDL_SetTextureColorMod(atlas,color.r,color.g,color.b);
        SDL_FRect source{float(code%16*8),float(code/16*8),8,8},dest{x,y,8*scale,8*scale};
        SDL_RenderTexture(renderer,atlas,&source,&dest);
    }
    void text(const std::string& value,float x,float y,SDL_Color color,float scale=1) {
        for(unsigned char ch:value) { if(ch!=' ')tile(ch,x,y,color,scale);x+=8*scale; }
    }
    void centered(const std::string& value,float y,SDL_Color color) {text(value,160-value.size()*4,y,color);}
    void title(const std::string& value,float y,SDL_Color color) {
        float x=160-value.size()*12;
        unsigned index=0;
        for(unsigned char ch:value) {
            const auto ink=value=="BURNER II" && index>=7?SDL_Color{255,62,48,255}:color;
            if(ch!=' ') tile(ch,x,y,ink,3);
            x+=24;
            ++index;
        }
    }
    void cursor(float x,float y) {tile('*',x+8,y,{65,183,255,255});}

};
inline bool pixel_art_available() {
#if SDL_VERSION_ATLEAST(3,4,0)
    return SDL_GetVersion()>=SDL_VERSIONNUM(3,4,0);
#else
    return false;
#endif
}
inline bool apply_settings(SDL_Window* window,SDL_Renderer* renderer,SDL_Texture* texture,
                           const Settings& settings) {
    SDL_ScaleMode scale=settings.scaling==Scaling::linear?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST;
#if SDL_VERSION_ATLEAST(3,4,0)
    if(settings.scaling==Scaling::pixel_art) scale=SDL_SCALEMODE_PIXELART;
#endif
    return SDL_SetTextureScaleMode(texture,scale) &&
        SDL_SetRenderLogicalPresentation(renderer,320,224,settings.scaling==Scaling::integer?
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE:SDL_LOGICAL_PRESENTATION_LETTERBOX) &&
        (((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0)==settings.fullscreen ||
         SDL_SetWindowFullscreen(window,settings.fullscreen));
}
}

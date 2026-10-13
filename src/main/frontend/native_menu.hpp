#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <string>
#include "roms.hpp"
#include "engine/game_session.hpp"
#include "frontend/native_settings.hpp"
#include "frontend/native_input.hpp"
#include "frontend/native_music.hpp"

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
    NativeInput* input{};
    NativeMusic* music{};
    std::function<bool(const Settings&)> apply;
    void open(GameSession& game) {
        if (opened) return;
        was_paused=game.paused(); initial=game.info().number==0;
        game.set_paused(true); opened=true; options=false; controls=false;audio_page=0; selected=0;
    }
    void close(GameSession& game) {
        if (!opened) return;
        game.set_paused(was_paused); opened=false;audio_page=0;if(music)music->stop_preview(); if(input)input->cancel();
    }
    bool is_open() const { return opened; }
    bool in_controls() const {return controls;}
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
        if(audio_page && music) {
            if(key==SDLK_ESCAPE || key==SDLK_F1) {
                music->stop_preview();
                if(audio_page==1){audio_page=0;selected=8+(input?1:0);}else {audio_page=1;selected=audio_page_previous;}
                return Action::none;
            }
            const int rows=audio_page==1?6:8;
            if(key==SDLK_UP)selected=(selected+rows-1)%rows;
            if(key==SDLK_DOWN)selected=(selected+1)%rows;
            const bool enter=key==SDLK_RETURN || key==SDLK_KP_ENTER;
            if(audio_page==2 && key==SDLK_SPACE && selected<6) {if(music->previewing())music->stop_preview();else music->preview(selected);return Action::none;}
            if(key==SDLK_LEFT || key==SDLK_RIGHT || enter) {
                const auto delta=key==SDLK_LEFT?-1:1;
                if(audio_page==1) {
                    if(selected<2)music->volume(selected==1,delta);
                    else if(enter && selected<4){audio_page_previous=selected;audio_page=selected==2?2:3;selected=0;music->refresh();}
                    else if(enter && selected==4)music->defaults();
                    else if(enter && selected==5){audio_page=0;selected=8+(input?1:0);}
                } else if(selected<6) {
                    if(audio_page==2)music->cycle(selected,delta);else music->track_volume(selected,delta);
                } else if(enter && selected==6) {if(audio_page==2)music->originals();else {music->settings.track_volume.fill(100);music->track_volume(0,0);}}
                else if(enter && selected==7) {music->stop_preview();audio_page=1;selected=audio_page_previous;}
            }
            return Action::none;
        }
        if(controls && input) {
            if(key==SDLK_ESCAPE || key==SDLK_F1) {input->cancel();controls=false;selected=8;return Action::none;}
            if(input->waiting())return Action::none;
            if(key==SDLK_UP)selected=(selected+16)%17;
            if(key==SDLK_DOWN)selected=(selected+1)%17;
            if(key==SDLK_BACKSPACE && selected>=4 && selected<15)input->clear(selected-4);
            const bool enter=key==SDLK_RETURN || key==SDLK_SPACE || key==SDLK_KP_ENTER;
            if(key==SDLK_LEFT || key==SDLK_RIGHT || enter) {
                const int delta=key==SDLK_LEFT?-1:1;
                if(selected==0)input->cycle(delta);
                else if(selected==1)input->deadzone(delta);
                else if(selected==2)input->sensitivity(delta);
                else if(selected==3)input->throttle_mode();
                else if(enter && selected<15)input->begin(selected-4);
                else if(enter && selected==15)input->reset();
                else if(enter && selected==16) {controls=false;selected=8;}
            }
            return Action::none;
        }
        if (key==SDLK_ESCAPE || key==SDLK_F1) {
            if (options) { options=false;selected=1;return Action::none; }
            return Action::close;
        }
        const int rows=options?option_rows():3;
        if (key==SDLK_UP) selected=(selected+rows-1)%rows;
        if (key==SDLK_DOWN) selected=(selected+1)%rows;
        if (options && (key==SDLK_LEFT || key==SDLK_RIGHT)) adjust(key==SDLK_LEFT?-1:1);
        if (key!=SDLK_RETURN && key!=SDLK_SPACE && key!=SDLK_KP_ENTER) return Action::none;
        if (options) {
            if (selected==option_rows()-1) {options=false;selected=1;} else if(input && selected==8) {controls=true;selected=0;} else if(music && selected==8+(input?1:0)){audio_page=1;selected=0;} else adjust(1);
            return Action::none;
        }
        if (selected==0) return initial?Action::start:Action::close;
        if (selected==1) {options=true;selected=0;return Action::none;}
        return Action::quit;
    }
    Action pointer(float x,float y,bool click) {
        if(audio_page && music) {
            const int rows=audio_page==1?6:8;
            for(int i=0;i<rows;++i)if(x>=24 && x<=304 && y>=61+i*16 && y<77+i*16){selected=i;return click?key(SDLK_RETURN):Action::none;}
            return Action::none;
        }
        if(controls && input) {
            if(input->waiting())return Action::none;
            const int first=selected/6*6;
            for(int i=first;i<std::min(first+6,17);++i)if(x>=24 && x<=296 && y>=64+(i-first)*20 && y<84+(i-first)*20) {
                selected=i;return click?key(SDLK_RETURN):Action::none;
            }
            return Action::none;
        }
        const int rows=options?option_rows():3;
        const int first=options?option_first():0;
        for (int i=first;i<std::min(rows,first+8);++i) {
            const float top=options?103.f+(i-first)*option_spacing():116.f+i*20.f;
            if(x>=32 && x<=296 && y>=top && y<top+(options?option_spacing():18)) {
                selected=i;return click?key(SDLK_RETURN):Action::none;
            }
        }
        return Action::none;
    }
    void draw() {
        SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
        if(audio_page && music) {
            centered(audio_page==1?"AUDIO":audio_page==2?"MUSIC REPLACEMENTS":"TRACK VOLUMES",16,{255,255,255,255});
            centered(audio_page==2?"L/R CHANGE  SPACE PREVIEW":"LEFT/RIGHT ADJUST",36,{125,153,180,255});
            const int rows=audio_page==1?6:8;
            for(int i=0;i<rows;++i) {
                std::string label,value;
                if(audio_page==1) {
                    const char* labels[]{"MUSIC VOLUME","EFFECTS VOLUME","MUSIC TRACKS","TRACK VOLUMES","RESET VOLUMES","BACK"};label=labels[i];
                    if(i<2)value=std::to_string(i?music->settings.effects:music->settings.music)+"%";
                } else if(i<6) {label=music_titles[i];value=audio_page==2?music->selection(i):std::to_string(music->settings.track_volume[i])+"%";}
                else label=i==6?(audio_page==2?"ALL ORIGINAL":"RESET VOLUMES"):"BACK";
                std::transform(value.begin(),value.end(),value.begin(),[](unsigned char ch){return std::toupper(ch);});
                auto color=i==selected?SDL_Color{255,255,255,255}:SDL_Color{135,157,181,255};
                text(label,32,64+i*16,color);text(value.substr(0,15),176,64+i*16,{54,184,255,255});if(i==selected)cursor(8,64+i*16);
            }
            if(audio_page==2 && selected<6) {
                auto filename=music->selection(selected);std::transform(filename.begin(),filename.end(),filename.begin(),[](unsigned char ch){return std::toupper(ch);});
                if(filename.size()>38){filename+="    ";const auto offset=(SDL_GetTicks()/250)%filename.size();filename=filename.substr(offset)+filename.substr(0,offset);}
                centered(filename.substr(0,38),188,{54,184,255,255});
            }
            centered(music->notice.substr(0,38),203,{125,153,180,255});
            return;
        }
        if(controls && input) {
            centered("CONTROLS",16,{255,255,255,255});
            centered("UP/DOWN SCROLL - ENTER REMAP",36,{125,153,180,255});
            const int first=selected/6*6;
            for(int i=first;i<std::min(first+6,17);++i) {
                std::string label,value;
                if(i==0) {label="DEVICE";value=input->device_name();}
                else if(i==1) {label="DEAD ZONE";value=std::to_string(input->profile().deadzone)+"%";}
                else if(i==2) {label="SENSITIVITY";value=std::to_string(input->profile().sensitivity)+"%";}
                else if(i==3) {label="THROTTLE";value=input->raw_device()?(input->profile().full_throttle?"FULL AXIS":"HALF AXES"):"BUTTONS/TRIGGERS";}
                else if(i<15) {label=NativeInput::labels[i-4];value=input->binding_name(i-4);}
                else label=i==15?"RESET DEFAULTS":"BACK";
                auto color=i==selected?SDL_Color{255,255,255,255}:SDL_Color{135,157,181,255};
                std::transform(value.begin(),value.end(),value.begin(),[](unsigned char ch){return std::toupper(ch);});
                text(label,32,64+(i-first)*20,color);text(value.substr(0,18),144,64+(i-first)*20,{54,184,255,255});
                if(i==selected)cursor(8,64+(i-first)*20);
            }
            centered(input->status.empty()?"BACKSPACE CLEAR - ESC RETURN":input->status.substr(0,38),195,{125,153,180,255});
            return;
        }
        title("AFTER",16,SDL_Color{240,244,255,255});
        title("BURNER II",51,SDL_Color{240,244,255,255});
        SDL_SetRenderDrawColor(renderer,45,164,236,255);
        SDL_RenderLine(renderer,40,94,280,94);
        if (options) {
            const char* labels[]{"UPSCALING","SPRITES","CLOUDS","TRAILS","EXPLOSIONS","AFTERBURNER","SCREEN","STATS"};
            const char* scales[]{"NEAREST","SMOOTH","INTEGER","PIXEL ART"};
            const char* stats[]{"OFF","FPS","DETAILED"};
            const char* values[]{scales[static_cast<int>(settings.scaling)],sprite_filter_labels[static_cast<int>(settings.sprites)],settings.clouds?"TRANSLUCENT":"OPAQUE",settings.trails?"TRANSLUCENT":"OPAQUE",settings.explosions?"TRANSLUCENT":"OPAQUE",settings.afterburner?"TRANSLUCENT":"OPAQUE",settings.fullscreen?"FULLSCREEN":"WINDOWED",stats[static_cast<int>(settings.stats)]};
            for(int i=option_first();i<std::min(option_rows(),option_first()+8);++i) {
                const SDL_Color color=i==selected?SDL_Color{255,255,255,255}:SDL_Color{135,157,181,255};
                const std::string label=i<8?labels[i]:i==option_rows()-1?"BACK":input&&i==8?"CONTROLS":"AUDIO";
                text(label,48,103+(i-option_first())*option_spacing(),color);
                if(i<8)text(values[i],192,103+(i-option_first())*option_spacing(),i==selected?SDL_Color{54,184,255,255}:color);
                if(i==selected)cursor(27,103+(i-option_first())*option_spacing());
            }
            centered("UP/DOWN SCROLL  LEFT/RIGHT CHANGE",195,{125,153,180,255});
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
    bool opened{},options{},controls{},was_paused{},initial{};
    int audio_page{},audio_page_previous=2;
    mutable int option_scroll{};
    int option_rows()const{return 9+(input?1:0)+(music?1:0);}
    int option_first()const {
        option_scroll=std::clamp(option_scroll,0,std::max(0,option_rows()-8));
        if(selected<option_scroll)option_scroll=selected;
        if(selected>=option_scroll+8)option_scroll=selected-7;
        return option_scroll;
    }
    float option_spacing()const{return 11.f;}
    bool change(const Settings& changed) {
        if (apply && !apply(changed)) {notice="SETTING UNAVAILABLE";return false;}
        settings=changed;return true;
    }
    void adjust(int direction) {
        auto changed=settings;
        if(selected==0) changed.scaling=static_cast<Scaling>((static_cast<int>(settings.scaling)+scale_count+direction)%scale_count);
        if(selected==1) changed.sprites=static_cast<SpriteFilter>((static_cast<int>(settings.sprites)+sprite_filter_count+direction)%sprite_filter_count);
        if(selected==2) changed.clouds=!settings.clouds;
        if(selected==3) changed.trails=!settings.trails;
        if(selected==4) changed.explosions=!settings.explosions;
        if(selected==5) changed.afterburner=!settings.afterburner;
        if(selected==6) changed.fullscreen=!settings.fullscreen;
        if(selected==7) changed.stats=static_cast<Stats>((static_cast<int>(settings.stats)+3+direction)%3);
        if(selected<8) change(changed);
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
inline bool apply_texture_settings(SDL_Texture* texture,const Settings& settings) {
    SDL_ScaleMode scale=settings.scaling==Scaling::linear?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST;
#if SDL_VERSION_ATLEAST(3,4,0)
    if(settings.scaling==Scaling::pixel_art) scale=SDL_SCALEMODE_PIXELART;
#endif
    return SDL_SetTextureScaleMode(texture,scale);
}
inline bool apply_settings(SDL_Window* window,SDL_Renderer* renderer,SDL_Texture* texture,
                           const Settings& settings) {
    return apply_texture_settings(texture,settings) &&
        SDL_SetRenderLogicalPresentation(renderer,320,224,settings.scaling==Scaling::integer?
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE:SDL_LOGICAL_PRESENTATION_LETTERBOX) &&
        (((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0)==settings.fullscreen ||
         (SDL_SetWindowFullscreen(window,settings.fullscreen) && SDL_SyncWindow(window)));
}
}

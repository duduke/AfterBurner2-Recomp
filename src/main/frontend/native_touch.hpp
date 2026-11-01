#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include "engine/game_controls.hpp"

namespace Frontend {
// Presentation/input adapter only: fingers feed the same physical arcade pins
// as controllers. Each finger owns its original control until released.
class NativeTouch {
public:
    bool enabled{};
    enum Control { stick, cannon, missile, throttle, coin, start, menu, visibility, none };
    struct Target { float x,y,radius; const char* label; };
    // Radius/travel are fractions of display height, so circles stay round on
    // phones and tablets. The joystick cap is approximately thumb-sized on a
    // landscape phone; its larger base retains a comfortable acquisition area.
    static constexpr float stick_travel=.085f, cap_radius=.063f;
    static constexpr float throttle_half=.23f;
    static std::array<Target,8> layout(float aspect) {
        return {{{.18f,.78f,.15f,""},
                 {.18f-.085f/aspect,.53f,.065f,"GUN"},
                 {.18f+.085f/aspect,.53f,.065f,"MSL"},
                 {1.f-.085f/aspect,.63f,.075f,""},
                 {.5f-.15f/aspect,.10f,.055f,"COIN"},
                 {.5f,.10f,.055f,"START"},
                 {.5f+.15f/aspect,.10f,.055f,"MENU"},
                 {.5f+.30f/aspect,.10f,.055f,"HIDE"}}};
    }
    bool visible() const {return show_controls;}
    void clear() { fingers.clear(); }
    bool event(const SDL_Event& e, float aspect) {
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST || e.type==SDL_EVENT_WILL_ENTER_BACKGROUND)clear();
        if(!enabled)return false;
        if(e.type==SDL_EVENT_FINGER_UP || e.type==SDL_EVENT_FINGER_CANCELED) {
            fingers.erase({e.tfinger.touchID,e.tfinger.fingerID});return false;
        }
        if(e.type!=SDL_EVENT_FINGER_DOWN && e.type!=SDL_EVENT_FINGER_MOTION)return false;
        const auto id=std::make_pair(e.tfinger.touchID,e.tfinger.fingerID);
        if(e.type==SDL_EVENT_FINGER_DOWN) {
            const auto c=hit(e.tfinger.x,e.tfinger.y,aspect);
            if(c==visibility){clear();show_controls=!show_controls;return false;}
            if(c==none || !show_controls)return false;
            // A second finger must not steal an active axis control.
            if(c==stick || c==throttle)for(const auto& [key,f]:fingers)if(f.control==c)return false;
            fingers[id]={c,e.tfinger.x,e.tfinger.y};
            return c==menu;
        }
        if(auto i=fingers.find(id);i!=fingers.end()) {i->second.x=e.tfinger.x;i->second.y=e.tfinger.y;}
        return false;
    }
    static Control hit(float x,float y,float aspect) {
        const auto targets=layout(aspect);
        for(int c=0;c<none;++c) {
            const float dx=(x-targets[c].x)*aspect,dy=y-targets[c].y;
            if(c==throttle) {
                if(std::abs(dx)<=targets[c].radius && std::abs(dy)<=throttle_half+cap_radius)return throttle;
            } else if(std::hypot(dx,dy)<=targets[c].radius)return static_cast<Control>(c);
        }
        return none;
    }
    GameControls read(float aspect) const {
        GameControls out;
        if(!enabled || !show_controls)return out;
        const auto targets=layout(aspect);
        for(const auto& [id,f]:fingers)switch(f.control) {
            case stick: {
                float x=(f.x-targets[stick].x)*aspect/stick_travel,y=(targets[stick].y-f.y)/stick_travel;
                const float length=std::hypot(x,y);
                if(length>.10f) {const float gain=std::min(1.f,(length-.10f)/.90f)/length;out.stick_x=x*gain;out.stick_y=y*gain;}
                break;
            }
            case cannon:out.cannon=true;break;
            case missile:out.missile=true;break;
            case throttle: {
                const float value=std::clamp((targets[throttle].y-f.y)/throttle_half,-1.f,1.f);
                out.throttle=std::abs(value)<=.06f?0.f:std::copysign((std::abs(value)-.06f)/.94f,value);
                break;
            }
            case coin:out.coin=true;break;
            case start:out.start=true;break;
            default:break;
        }
        return out;
    }
    void merge(GameControls& input,float aspect) const {
        const auto t=read(aspect);
        if(t.stick_x || t.stick_y){input.stick_x=t.stick_x;input.stick_y=t.stick_y;}
        for(const auto& [id,f]:fingers)if(enabled && f.control==throttle)input.throttle=t.throttle;
        input.cannon|=t.cannon;input.missile|=t.missile;input.coin|=t.coin;input.start|=t.start;
    }
    void draw(SDL_Renderer* renderer) const {
        if(!enabled)return;
        // Overlay coordinates cover the entire drawable, including letterboxes.
        int lw,lh,w,h;SDL_RendererLogicalPresentation mode;
        SDL_GetRenderLogicalPresentation(renderer,&lw,&lh,&mode);
        SDL_Rect viewport;SDL_GetRenderViewport(renderer,&viewport);
        const bool had_viewport=SDL_RenderViewportSet(renderer);
        float sx,sy;SDL_GetRenderScale(renderer,&sx,&sy);
        SDL_BlendMode blend;SDL_GetRenderDrawBlendMode(renderer,&blend);
        Uint8 r,g,b,a;SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);
        SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderScale(renderer,1,1);
        SDL_GetRenderOutputSize(renderer,&w,&h);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        const auto targets=layout(float(w)/h);
        const auto state=read(float(w)/h);
        auto text=[&](float x,float y,const char* label) {
            const float scale=std::max(1.f,h/480.f);
            SDL_SetRenderScale(renderer,scale,scale);
            SDL_RenderDebugText(renderer,x/scale-4*SDL_strlen(label),y/scale-4,label);
            SDL_SetRenderScale(renderer,1,1);
        };
        for(int c=0;c<none;++c) {
            if(!show_controls && c!=visibility)continue;
            bool held=false;for(const auto& [id,f]:fingers)held|=f.control==c;
            const float x=targets[c].x*w,y=targets[c].y*h,radius=targets[c].radius*h;
            if(c==stick) {
                SDL_SetRenderDrawColor(renderer,12,24,35,95);circle(renderer,x,y,radius,true);
                SDL_SetRenderDrawColor(renderer,130,215,255,105);circle(renderer,x,y,radius,false);
                const float tx=x+state.stick_x*stick_travel*h,ty=y-state.stick_y*stick_travel*h;
                const float cap=cap_radius*h;
                SDL_SetRenderDrawColor(renderer,10,22,32,held?230:190);circle(renderer,tx,ty,cap,true);
                SDL_SetRenderDrawColor(renderer,130,215,255,held?245:180);circle(renderer,tx,ty,cap,false);
                SDL_SetRenderDrawColor(renderer,65,100,120,held?230:165);circle(renderer,tx,ty,cap*.82f,true);
                SDL_SetRenderDrawColor(renderer,22,44,60,held?230:185);circle(renderer,tx,ty,cap*.63f,true);
                SDL_SetRenderDrawColor(renderer,150,210,235,150);circle(renderer,tx,ty,cap*.63f,false);
                // Short grip marks on the rim suggest a gamepad analog cap.
                for(int i=0;i<16;++i) {
                    const float angle=i*6.2831853f/16;
                    SDL_RenderLine(renderer,tx+std::cos(angle)*cap*.88f,ty+std::sin(angle)*cap*.88f,
                                   tx+std::cos(angle)*cap*.96f,ty+std::sin(angle)*cap*.96f);
                }
            } else if(c==throttle) {
                const float half=throttle_half*h;
                SDL_SetRenderDrawColor(renderer,12,24,35,110);
                SDL_FRect track{x-.018f*h,y-half,.036f*h,2*half};SDL_RenderFillRect(renderer,&track);
                SDL_SetRenderDrawColor(renderer,130,215,255,155);
                SDL_RenderRect(renderer,&track);SDL_RenderLine(renderer,x-.045f*h,y,x+.045f*h,y);
                text(x,y-half-.095f*h,"BOOST");text(x,y+half+.095f*h,"BRAKE");
                const float knob=y-state.throttle*half;
                SDL_SetRenderDrawColor(renderer,20,50,70,held?230:170);circle(renderer,x,knob,cap_radius*h,true);
                SDL_SetRenderDrawColor(renderer,130,215,255,held?245:180);circle(renderer,x,knob,cap_radius*h,false);
                SDL_RenderLine(renderer,x-.035f*h,knob,x+.035f*h,knob);
            } else {
                SDL_SetRenderDrawColor(renderer,15,35,55,held?190:110);circle(renderer,x,y,radius,true);
                SDL_SetRenderDrawColor(renderer,130,215,255,held?245:180);circle(renderer,x,y,radius,false);
                text(x,y,c==visibility?(show_controls?"HIDE":"SHOW"):targets[c].label);
            }
        }
        SDL_SetRenderLogicalPresentation(renderer,lw,lh,mode);
        SDL_SetRenderViewport(renderer,had_viewport?&viewport:nullptr);SDL_SetRenderScale(renderer,sx,sy);
        SDL_SetRenderDrawBlendMode(renderer,blend);SDL_SetRenderDrawColor(renderer,r,g,b,a);
    }
private:
    bool show_controls{true};
    static void circle(SDL_Renderer* renderer,float x,float y,float radius,bool filled) {
        constexpr int segments=48;
        std::array<SDL_FPoint,segments+1> points;
        for(int i=0;i<=segments;++i) {
            const float angle=i*6.2831853f/segments;
            points[i]={x+std::cos(angle)*radius,y+std::sin(angle)*radius};
        }
        if(!filled){SDL_RenderLines(renderer,points.data(),points.size());return;}
        Uint8 r,g,b,a;SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);
        const SDL_FColor color{r/255.f,g/255.f,b/255.f,a/255.f};
        std::array<SDL_Vertex,segments+2> vertices{};
        std::array<int,segments*3> indices{};
        vertices[0]={{x,y},color,{0,0}};
        for(int i=0;i<=segments;++i)vertices[i+1]={points[i],color,{0,0}};
        for(int i=0;i<segments;++i){indices[i*3]=0;indices[i*3+1]=i+1;indices[i*3+2]=i+2;}
        SDL_RenderGeometry(renderer,nullptr,vertices.data(),vertices.size(),indices.data(),indices.size());
    }
    struct Finger { Control control;float x,y; };
    std::map<std::pair<SDL_TouchID,SDL_FingerID>,Finger> fingers;
};
}

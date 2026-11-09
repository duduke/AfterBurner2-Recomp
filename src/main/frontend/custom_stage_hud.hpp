#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "engine/custom_stage_interactive.hpp"
#include "engine/game_session.hpp"

namespace Frontend {

// Passive SDL3 overlay for interactive guidance symbology (Ace Combat ILS
// localizer/glideslope bars, velocity vector, refueling contact diamonds, and
// service progress). Stage layers are drawn by NativeCustomStage. Never writes
// board state.
class CustomStageHud {
public:
    static void draw(SDL_Renderer* renderer,const GameSession& game) {
        if(!renderer || !game.custom_stage_active())return;
        const auto* def=game.custom_stage_definition();
        const auto* ctrl=game.custom_stage_controller();
        if(!def || !ctrl || !def->hud)return;

        SDL_BlendMode previous_blend=SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer,&previous_blend);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);

        const CustomStage::Camera cam{ctrl->x,ctrl->height,198.0-ctrl->pitch*42.0,480.0,34.0};
        if(!ctrl->combat() && game.custom_stage_in_slot()) {
            draw_guidance_symbology(renderer,*def,*ctrl,cam);
        }

        SDL_SetRenderDrawBlendMode(renderer,previous_blend);
    }

private:
    static void draw_rect(SDL_Renderer* renderer,float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b,uint8_t a=255) {
        SDL_SetRenderDrawColor(renderer,r,g,b,a);
        SDL_FRect rect{x,y,w,h};
        SDL_RenderFillRect(renderer,&rect);
    }

    static void draw_outline(SDL_Renderer* renderer,float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b,uint8_t a=255) {
        SDL_SetRenderDrawColor(renderer,r,g,b,a);
        SDL_FRect rect{x,y,w,h};
        SDL_RenderRect(renderer,&rect);
    }

    static void draw_diamond(SDL_Renderer* renderer,float cx,float cy,float radius,uint8_t r,uint8_t g,uint8_t b,uint8_t a=255) {
        SDL_SetRenderDrawColor(renderer,r,g,b,a);
        SDL_RenderLine(renderer,cx,cy-radius,cx+radius,cy);
        SDL_RenderLine(renderer,cx+radius,cy,cx,cy+radius);
        SDL_RenderLine(renderer,cx,cy+radius,cx-radius,cy);
        SDL_RenderLine(renderer,cx-radius,cy,cx,cy-radius);
    }

    static void draw_guidance_symbology(SDL_Renderer* renderer,const CustomStage::Definition& def,const CustomStage::InteractiveController& ctrl,const CustomStage::Camera& cam) {
        if(ctrl.refuel()) {
            const auto tk=CustomStage::Guidance::tanker(ctrl.x,ctrl.height,ctrl.range,def.start_distance,ctrl.coupled,ctrl.aligned());
            const uint8_t gr=tk.locked?72:255;
            const uint8_t gg=tk.locked?246:196;
            const uint8_t gb=tk.locked?128:64;
            const float r=std::max(4.f,float(tk.radius/3.0));
            draw_diamond(renderer,160.f,112.f,r,110,200,255,190);
            draw_diamond(renderer,160.f+float(tk.offset.x/3.0),112.f+float(tk.offset.y/3.0),r,gr,gg,gb,235);
            if(ctrl.rearm_progress>0.0 && ctrl.rearm_progress<1.0) {
                draw_rect(renderer,110.f,196.f,100.f,6.f,16,28,44,200);
                draw_rect(renderer,111.f,197.f,float(98.0*ctrl.rearm_progress),4.f,64,228,255,240);
            }
            return;
        }

        const auto ld=CustomStage::Guidance::landing(ctrl.x,ctrl.height,ctrl.range,ctrl.lateral,ctrl.vertical,ctrl.speed,def.slope,cam.horizon,cam.focal);
        const bool ok=ld.localizer_centered() && ld.glideslope_centered();
        const uint8_t gr=ok?72:255;
        const uint8_t gg=ok?246:196;
        const uint8_t gb=ok?128:64;

        if(ctrl.phase==CustomStage::InteractiveController::Phase::approach) {
            SDL_SetRenderDrawColor(renderer,120,210,255,180);
            SDL_RenderLine(renderer,154.f,112.f,166.f,112.f);
            SDL_RenderLine(renderer,160.f,106.f,160.f,118.f);

            const float loc_x=float(ld.cross.x/3.0);
            const float gs_y=float(ld.cross.y*224.0/600.0);
            SDL_SetRenderDrawColor(renderer,gr,gg,gb,230);
            SDL_RenderLine(renderer,loc_x,88.f,loc_x,136.f);
            SDL_RenderLine(renderer,134.f,gs_y,186.f,gs_y);

            const float vx=std::clamp(float(ld.velocity.x/3.0),32.f,288.f);
            const float vy=std::clamp(float(ld.velocity.y*224.0/600.0),24.f,196.f);
            draw_outline(renderer,vx-4.f,vy-4.f,8.f,8.f,gr,gg,gb,235);
            SDL_RenderLine(renderer,vx-8.f,vy,vx-4.f,vy);
            SDL_RenderLine(renderer,vx+4.f,vy,vx+8.f,vy);
            SDL_RenderLine(renderer,vx,vy-8.f,vx,vy-4.f);
        }

        if(ctrl.servicing()) {
            draw_rect(renderer,110.f,196.f,100.f,6.f,16,28,44,200);
            draw_rect(renderer,111.f,197.f,float(98.0*ctrl.service_progress()),4.f,64,228,255,240);
        }

        if(ctrl.phase==CustomStage::InteractiveController::Phase::ready && ((ctrl.ticks/15)%2==0)) {
            draw_outline(renderer,104.f,184.f,112.f,10.f,255,212,64,230);
            draw_rect(renderer,106.f,186.f,float(108.0*std::clamp(ctrl.power,0.0,1.0)),6.f,255,180,48,220);
        }
    }
};

} // namespace Frontend

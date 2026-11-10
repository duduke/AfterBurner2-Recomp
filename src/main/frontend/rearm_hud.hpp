#pragma once

// Guidance for the interactive landing and tanker sequences
// (engine/interactive_rearm.hpp). Drawn in the 320x224 safe area after the
// game picture, before the menu. Read-only: it never touches the board.

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include "engine/interactive_rearm.hpp"

namespace Frontend {

class RearmHud {
public:
    using Text = std::function<void(const std::string&, float, float, SDL_Color, float)>;

    static void draw(SDL_Renderer* renderer, const Rearm::Hud& hud, unsigned, const Text& text) {
        if (!renderer || hud.kind == Rearm::Kind::none && hud.grade == Rearm::Grade::none) return;
        SDL_BlendMode previous = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &previous);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        const bool showing_result = hud.grade != Rearm::Grade::none && hud.result_age < 3.2f;
        if (!showing_result) {
            if (hud.kind == Rearm::Kind::landing) landing(renderer, hud, text);
            else if (hud.kind == Rearm::Kind::refuel) refuel(renderer, hud, text);
        } else {
            result(renderer, hud, text);
        }
        SDL_SetRenderDrawBlendMode(renderer, previous);
    }

private:
    static constexpr float cx = 160.f, cy = 100.f;
    static constexpr SDL_Color green{96, 255, 140, 255}, amber{255, 196, 64, 255},
                               red{255, 80, 64, 255}, white{235, 245, 255, 255}, cyan{120, 220, 255, 255};

    static void color(SDL_Renderer* r, SDL_Color c, uint8_t a) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a); }
    static void line(SDL_Renderer* r, float x0, float y0, float x1, float y1) { SDL_RenderLine(r, x0, y0, x1, y1); }
    static void thick(SDL_Renderer* r, float x0, float y0, float x1, float y1) {
        line(r, x0, y0, x1, y1);
        if (x0 == x1) line(r, x0 + 1, y0, x1 + 1, y1); else line(r, x0, y0 + 1, x1, y1 + 1);
    }
    static void box(SDL_Renderer* r, float x, float y, float w, float h) { SDL_FRect f{x, y, w, h}; SDL_RenderRect(r, &f); }
    static void fill(SDL_Renderer* r, float x, float y, float w, float h) { SDL_FRect f{x, y, w, h}; SDL_RenderFillRect(r, &f); }
    static void diamond(SDL_Renderer* r, float x, float y, float s) {
        line(r, x, y - s, x + s, y); line(r, x + s, y, x, y + s);
        line(r, x, y + s, x - s, y); line(r, x - s, y, x, y - s);
    }
    static void centered(const Text& text, const std::string& s, float y, SDL_Color c, float scale = 1.f) {
        text(s, cx - float(s.size()) * 4.f * scale, y, c, scale);
    }
    static void banner(SDL_Renderer* r, float y, float h) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
        fill(r, 40.f, y, 240.f, h);
    }
    static bool blink(const Rearm::Hud& h, unsigned period = 16) { return (h.frame / period) % 2 == 0; }

    static void landing(SDL_Renderer* r, const Rearm::Hud& h, const Text& text) {
        using P = Rearm::Phase;
        if (h.phase == P::service || h.phase == P::departure || h.phase == P::rollout) {
            if (*h.prompt) { banner(r, 150.f, 22.f); centered(text, h.prompt, 152.f, cyan); centered(text, h.detail, 162.f, white); }
            return;
        }
        // Instrument landing box: needles point to where the glide path is.
        const bool on_loc = std::abs(h.localizer) < 0.35f, on_gs = std::abs(h.glideslope) < 0.35f;
        const float half = 26.f;
        color(r, white, 110);
        box(r, cx - half, cy - half, half * 2, half * 2);
        for (int i = -2; i <= 2; ++i) {
            if (!i) continue;
            fill(r, cx + i * half / 2.5f - 0.5f, cy + half + 2, 1.5f, 1.5f);
            fill(r, cx + half + 2, cy + i * half / 2.5f - 0.5f, 1.5f, 1.5f);
        }
        // Waterline symbol: where the aircraft is.
        color(r, white, 200);
        line(r, cx - 10, cy, cx - 4, cy); line(r, cx - 4, cy, cx - 2, cy + 3);
        line(r, cx - 2, cy + 3, cx, cy); line(r, cx, cy, cx + 2, cy + 3);
        line(r, cx + 2, cy + 3, cx + 4, cy); line(r, cx + 4, cy, cx + 10, cy);
        const float lx = cx - std::clamp(h.localizer, -1.2f, 1.2f) * half;
        color(r, on_loc ? green : amber, 235);
        thick(r, lx, cy - half + 3, lx, cy + half - 3);
        if (h.phase != P::approach) {
            const float gy = cy + std::clamp(h.glideslope, -1.2f, 1.2f) * half;
            color(r, on_gs ? green : amber, 235);
            thick(r, cx - half + 3, gy, cx + half - 3, gy);
        }
        text("LOC", cx - half - 2, cy - half - 10, on_loc ? green : amber, 1);
        if (h.phase != P::approach) text("G/S", cx + half - 22, cy - half - 10, on_gs ? green : amber, 1);

        // Airspeed with its landing band.
        const bool slow = h.airspeed < h.airspeed_low, fast = h.airspeed > h.airspeed_high;
        char spd[16];
        std::snprintf(spd, sizeof spd, "SPD %3d", int(std::lround(h.airspeed)));
        text(spd, 18.f, 64.f, fast || slow ? amber : green, 1);
        text(fast ? "THR BACK" : slow ? "ADD THR" : "IN BAND", 18.f, 74.f, fast || slow ? amber : green, 1);
        // Crosswind arrow.
        text(h.wind < 0 ? "<WIND" : "WIND>", 18.f, 90.f, white, 1);

        if (h.phase == P::flare) {
            // Sink meter: keep it out of the red.
            const float top = 70.f, height = 56.f;
            color(r, white, 110); box(r, 280.f, top, 10.f, height);
            color(r, red, 120); fill(r, 281.f, top + 1, 8.f, height * 0.25f);
            const float k = std::clamp(h.sink, 0.f, 1.4f) / 1.4f;
            color(r, k > 0.71f ? red : green, 230);
            fill(r, 281.f, top + height - 1 - (height - 2) * k, 8.f, (height - 2) * k);
            text("SINK", 272.f, top + height + 3, white, 1);
        }
        if (*h.prompt) {
            banner(r, 150.f, 22.f);
            const bool urgent = h.phase == P::flare;
            centered(text, h.prompt, 152.f, urgent && blink(h, 8) ? amber : cyan);
            centered(text, h.detail, 162.f, white);
        }
        if (h.carrier) text("CARRIER", 18.f, 106.f, cyan, 1);
    }

    static void refuel(SDL_Renderer* r, const Rearm::Hud& h, const Text& text) {
        using P = Rearm::Phase;
        if (h.phase == P::complete) return;
        // Fixed boom marker and the plane's receptacle, AC-style.
        const float scale = 22.f;
        const float bx = cx, by = 96.f;
        const float px = bx + std::clamp(h.offset_x, -2.f, 2.f) * scale;
        const float py = by - std::clamp(h.offset_y, -2.f, 2.f) * scale;
        const SDL_Color c = h.connected ? green : amber;
        color(r, white, 120);
        diamond(r, bx, by, scale * 0.55f);
        color(r, white, 60);
        diamond(r, bx, by, scale);
        color(r, c, 240);
        line(r, px - 7, py, px - 3, py); line(r, px + 3, py, px + 7, py);
        line(r, px, py - 7, px, py - 3); line(r, px, py + 3, px, py + 7);
        box(r, px - 2, py - 2, 4, 4);
        if (!h.connected) { color(r, amber, 140); line(r, bx, by, px, py); }

        if (h.phase == P::transfer) {
            // Missiles transferred and time on station.
            const float w = 96.f, x = 14.f, y = 44.f;
            const float done = h.owed ? std::clamp(float(h.delivered) / float(h.owed), 0.f, 1.f) : 1.f;
            color(r, white, 110); box(r, x, y, w, 6);
            color(r, cyan, 220); fill(r, x + 1, y + 1, (w - 2) * done, 4);
            color(r, white, 110); box(r, x, y + 9, w, 4);
            color(r, h.time_left < 0.25f ? red : amber, 200); fill(r, x + 1, y + 10, (w - 2) * h.time_left, 2);
            char msl[24];
            std::snprintf(msl, sizeof msl, "MSL +%u/%u", h.delivered, h.owed);
            text(msl, x, y - 10, cyan, 1);
        }
        if (*h.prompt) {
            banner(r, 176.f, 22.f);
            centered(text, h.prompt, 178.f, h.connected ? green : (blink(h, 10) ? amber : white));
            centered(text, h.detail, 188.f, white);
        }
    }

    static void result(SDL_Renderer* r, const Rearm::Hud& h, const Text& text) {
        using G = Rearm::Grade;
        const SDL_Color c = h.grade == G::perfect || h.grade == G::full ? green :
                            h.grade == G::good ? cyan :
                            h.grade == G::crash || h.grade == G::missed ? red : amber;
        banner(r, 84.f, 40.f);
        const std::string title = h.prompt;
        const float scale = title.size() <= 14 ? 2.f : 1.f;
        centered(text, title, 88.f, c, scale);
        centered(text, h.detail, 88.f + 10.f * scale + 4.f, white);
    }
};

} // namespace Frontend

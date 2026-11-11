#pragma once

// Presentation for CustomStage::BossFight. The aircraft, its parts and the
// fire are painted into the board's background plane, so the player's plane,
// enemies, missiles and explosions still draw in front of it; lock boxes,
// health and banners are an SDL overlay in the 320x224 safe area.

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include "engine/custom_stage_assets.hpp"
#include "engine/custom_stage_boss.hpp"

namespace Frontend {

struct BossPainter {
    using View = CustomStage::BossFight::View;

    // World points sit at R(-roll) about the screen center, like the backdrop.
    static std::pair<double, double> to_screen(const View& v, double x, double y, double origin) {
        const double dx = x - 160.0, dy = y - 112.0, c = std::cos(v.roll), s = std::sin(v.roll);
        return {origin + 160.0 + dx * c + dy * s, 112.0 - dx * s + dy * c};
    }

    static void paint(const CustomStage::StagePackage& package, const View& v, std::span<uint32_t> px,
                      unsigned width, unsigned height) {
        const double origin = (double(width) - 320.0) / 2.0;
        const double c = std::cos(v.roll), s = std::sin(v.roll);
        for (const auto& sprite : v.sprites) {
            const auto image = sprite.asset ? package.image(*sprite.asset) : nullptr;
            if (!image || sprite.width < 1 || sprite.height < 1 || sprite.alpha <= 0) continue;
            const auto [cx, cy] = to_screen(v, sprite.x, sprite.y, origin);
            const double hw = sprite.width / 2, hh = sprite.height / 2, reach = std::hypot(hw, hh);
            const int x0 = std::max(0, int(cx - reach)), x1 = std::min(int(width) - 1, int(cx + reach));
            const int y0 = std::max(0, int(cy - reach)), y1 = std::min(int(height) - 1, int(cy + reach));
            const unsigned flash = unsigned(std::clamp(sprite.flash, 0.f, 1.f) * 150.f);
            const double cb = std::cos(sprite.angle), sb = std::sin(sprite.angle);
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const double ox = x + 0.5 - cx, oy = y + 0.5 - cy;
                    const double rx = ox * c - oy * s, ry = ox * s + oy * c;   // undo the horizon roll
                    const double lx = rx * cb + ry * sb, ly = -rx * sb + ry * cb; // and the sprite's own bank
                    if (std::abs(lx) >= hw || std::abs(ly) >= hh) continue;
                    const unsigned sx = std::min(image->width - 1u, unsigned((lx + hw) / sprite.width * image->width));
                    const unsigned sy = std::min(image->height - 1u, unsigned((ly + hh) / sprite.height * image->height));
                    const uint8_t* p = image->rgba.data() + (size_t(sy) * image->width + sx) * 4u;
                    const unsigned a = unsigned(p[3] * sprite.alpha);
                    if (!a) continue;
                    unsigned r = p[0], g = p[1], b = p[2];
                    if (flash) { r += (255 - r) * flash / 255; g += (255 - g) * flash / 255; b += (255 - b) * flash / 255; }
                    blend(px[size_t(y) * width + size_t(x)], r, g, b, a);
                }
            }
        }
        for (const auto& d : v.disks) {
            if (d.alpha <= 0 || d.radius <= 0) continue;
            const auto [cx, cy] = to_screen(v, d.x, d.y, origin);
            const int x0 = std::max(0, int(cx - d.radius)), x1 = std::min(int(width) - 1, int(cx + d.radius));
            const int y0 = std::max(0, int(cy - d.radius)), y1 = std::min(int(height) - 1, int(cy + d.radius));
            const double r2 = d.radius * d.radius;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const double ox = x + 0.5 - cx, oy = y + 0.5 - cy, q = (ox * ox + oy * oy) / r2;
                    if (q > 1) continue;
                    const unsigned a = unsigned(255.0 * std::clamp(double(d.alpha), 0.0, 1.0) * (q > 0.6 ? (1 - q) / 0.4 : 1.0));
                    blend(px[size_t(y) * width + size_t(x)], d.r, d.g, d.b, a);
                }
        }
    }

    static void blend(uint32_t& pixel, unsigned r, unsigned g, unsigned b, unsigned a) {
        a = std::min(a, 255u);
        const unsigned inv = 255u - a;
        const unsigned nr = (r * a + ((pixel >> 16) & 255u) * inv) / 255u;
        const unsigned ng = (g * a + ((pixel >> 8) & 255u) * inv) / 255u;
        const unsigned nb = (b * a + (pixel & 255u) * inv) / 255u;
        pixel = 0xff000000u | nr << 16 | ng << 8 | nb;
    }
};

class BossHud {
public:
    using View = CustomStage::BossFight::View;
    using Text = std::function<void(const std::string&, float, float, SDL_Color, float)>;

    static void draw(SDL_Renderer* r, const View* v, const Text& text) {
        if (!r || !v) return;
        using P = CustomStage::BossFight::Phase;
        SDL_BlendMode previous = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(r, &previous);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const SDL_Color red{255, 80, 64, 255}, amber{255, 196, 64, 255}, green{96, 255, 140, 255}, white{235, 245, 255, 255};
        if (v->phase == P::battle || v->phase == P::approach) {
            // Health and time.
            const float x = 60, y = 44, w = 200;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 140);
            SDL_FRect back{x - 2, y - 2, w + 4, 12};
            SDL_RenderFillRect(r, &back);
            SDL_SetRenderDrawColor(r, 255, 80, 64, 230);
            SDL_FRect bar{x, y, w * v->health, 5};
            SDL_RenderFillRect(r, &bar);
            SDL_SetRenderDrawColor(r, 255, 196, 64, 200);
            SDL_FRect time{x, y + 6, w * v->time_left, 2};
            SDL_RenderFillRect(r, &time);
            if (v->name) text(*v->name, x, y + 10, red, 1);
            // Lock boxes on live parts.
            for (const auto& part : v->parts) {
                if (!part.alive || v->phase != P::battle) continue;
                const auto [sx, sy] = BossPainter::to_screen(*v, part.x, part.y, 0.0);
                const float hw = float(std::max(7.0, part.width * 0.5)), hh = float(std::max(6.0, part.height * 0.5));
                const SDL_Color c = part.shielded ? white : part.locked ? red : amber;
                SDL_SetRenderDrawColor(r, c.r, c.g, c.b, part.shielded ? 90 : 220);
                corners(r, float(sx), float(sy), hw, hh);
                if (part.locked) {
                    text("LOCK", float(sx) - 16, float(sy) - hh - 10, red, 1);
                    SDL_FRect hp{float(sx) - hw, float(sy) + hh + 2, 2 * hw * part.health, 2};
                    SDL_RenderFillRect(r, &hp);
                }
            }
            // Gun sight.
            const auto [ax, ay] = BossPainter::to_screen(*v, v->aim_x, v->aim_y, 0.0);
            SDL_SetRenderDrawColor(r, 96, 255, 140, 200);
            const float x0 = float(ax), y0 = float(ay);
            SDL_RenderLine(r, x0 - 9, y0, x0 - 4, y0); SDL_RenderLine(r, x0 + 4, y0, x0 + 9, y0);
            SDL_RenderLine(r, x0, y0 - 9, x0, y0 - 4); SDL_RenderLine(r, x0, y0 + 4, x0, y0 + 9);
        }
        threats(r, *v);
        if (v->hit_flash > 0.f) {
            // A red flash on the screen edges when the player is hit.
            const uint8_t a = uint8_t(150 * v->hit_flash);
            SDL_SetRenderDrawColor(r, 255, 40, 30, a);
            for (float k = 0; k < 6; ++k) { SDL_FRect e{k, k, 320 - 2 * k, 224 - 2 * k}; SDL_RenderRect(r, &e); }
        }
        if (*v->alert && v->alert_age < 1.6f && (int(v->alert_age * 6) % 2 == 0)) {
            const std::string a = v->alert;
            const bool hit = a.rfind("HIT", 0) == 0;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
            SDL_FRect box{160 - float(a.size()) * 4 - 6, 150, float(a.size()) * 8 + 12, 12};
            SDL_RenderFillRect(r, &box);
            text(a, 160 - float(a.size()) * 4, 152, hit ? red : amber, 1);
        }
        if (*v->banner && v->banner_age < 3.5f) {
            const bool warn = v->phase == P::approach;
            if (warn && (int(v->banner_age * 4) % 2)) { SDL_SetRenderDrawBlendMode(r, previous); return; }
            SDL_SetRenderDrawColor(r, 0, 0, 0, 130);
            SDL_FRect band{40, 84, 240, 36};
            SDL_RenderFillRect(r, &band);
            const std::string title = v->banner;
            const float scale = title.size() <= 14 ? 2.f : 1.f;
            const SDL_Color c = warn ? red : v->phase == P::destroyed ? green : v->phase == P::escaped ? amber : red;
            text(title, 160 - float(title.size()) * 4 * scale, 88, c, scale);
            const std::string detail = v->detail;
            text(detail, 160 - float(detail.size()) * 4, 88 + 10 * scale + 4, white, 1);
        }
        SDL_SetRenderDrawBlendMode(r, previous);
    }

private:
    // Everything that can hit the player, drawn last so nothing hides it.
    static void threats(SDL_Renderer* r, const View& v) {
        using K = CustomStage::BossFight::Threat::Kind;
        for (const auto& t : v.threats) {
            const auto [x, y] = BossPainter::to_screen(v, t.x, t.y, 0.0);
            switch (t.kind) {
            case K::tracer: {
                const auto [x2, y2] = BossPainter::to_screen(v, t.x2, t.y2, 0.0);
                // A long hot streak with a bright head.
                SDL_SetRenderDrawColor(r, 255, 120, 30, 170);
                for (float o = -1.f; o <= 1.f; o += 1.f) {
                    SDL_RenderLine(r, float(x2) + o, float(y2), float(x) + o, float(y));
                    SDL_RenderLine(r, float(x2), float(y2) + o, float(x), float(y) + o);
                }
                SDL_SetRenderDrawColor(r, 255, 200, 80, 255);
                SDL_RenderLine(r, float(x2), float(y2), float(x), float(y));
                SDL_SetRenderDrawColor(r, 255, 255, 220, 255);
                SDL_FRect head{float(x) - 2.f, float(y) - 2.f, 4, 4};
                SDL_RenderFillRect(r, &head);
                break;
            }
            case K::flak: {
                // A closing ring marks where a shell will burst: leave it.
                const float k = std::clamp(t.progress, 0.f, 1.f);
                const float radius = float(t.radius) * (2.2f - 1.2f * k);
                const bool blink = int(k * 14) % 2 == 0;
                SDL_SetRenderDrawColor(r, 255, 60, 40, uint8_t(blink ? 230 : 120));
                ring(r, float(x), float(y), radius);
                ring(r, float(x), float(y), radius - 1);
                SDL_SetRenderDrawColor(r, 255, 60, 40, 90);
                ring(r, float(x), float(y), float(t.radius));
                SDL_RenderLine(r, float(x) - 3, float(y), float(x) + 3, float(y));
                SDL_RenderLine(r, float(x), float(y) - 3, float(x), float(y) + 3);
                break;
            }
            case K::missile: {
                const auto [x2, y2] = BossPainter::to_screen(v, t.x2, t.y2, 0.0);
                SDL_SetRenderDrawColor(r, 200, 200, 210, 140);
                SDL_RenderLine(r, float(x2), float(y2), float(x), float(y));
                SDL_RenderLine(r, float(x2), float(y2) + 1, float(x), float(y) + 1);
                SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
                SDL_FRect body{float(x) - 2.5f, float(y) - 2.5f, 5, 5};
                SDL_RenderFillRect(r, &body);
                SDL_SetRenderDrawColor(r, 255, 60, 40, 255);
                ring(r, float(x), float(y), 6);
                break;
            }
            case K::burst: {
                const float k = std::clamp(t.progress, 0.f, 1.f);
                SDL_SetRenderDrawColor(r, 255, uint8_t(200 - 120 * k), 60, uint8_t(255 * (1 - k)));
                for (float rr = 2; rr < float(t.radius) * (0.6f + 0.6f * k); rr += 2) ring(r, float(x), float(y), rr);
                break;
            }
            }
        }
    }

    static void ring(SDL_Renderer* r, float x, float y, float radius) {
        if (radius <= 0) return;
        const int n = std::max(10, int(radius * 1.5f));
        float px = x + radius, py = y;
        for (int i = 1; i <= n; ++i) {
            const float a = float(i) / float(n) * 6.2831853f;
            const float nx = x + radius * std::cos(a), ny = y + radius * std::sin(a);
            SDL_RenderLine(r, px, py, nx, ny);
            px = nx; py = ny;
        }
    }

    static void corners(SDL_Renderer* r, float x, float y, float hw, float hh) {
        const float a = std::min(hw, hh) * 0.6f;
        for (int sx : {-1, 1})
            for (int sy : {-1, 1}) {
                const float cx = x + sx * hw, cy = y + sy * hh;
                SDL_RenderLine(r, cx, cy, cx - sx * a, cy);
                SDL_RenderLine(r, cx, cy, cx, cy - sy * a);
            }
    }
};

} // namespace Frontend

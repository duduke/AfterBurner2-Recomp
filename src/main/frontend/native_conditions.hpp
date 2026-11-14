#pragma once

// Presentation for CustomStage::ConditionsDirector (mission / conditions /
// formations). Scenery-side effects are painted into the board's background
// plane so the arcade's sprites stay in front; night dims both planes; what can
// hit the player, weather and the mission tracker are an SDL overlay.

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include "engine/custom_stage_assets.hpp"
#include "engine/custom_stage_conditions.hpp"
#include "frontend/native_boss.hpp"

namespace Frontend {

struct ConditionsPainter {
    using View = CustomStage::ConditionsDirector::View;

    static std::pair<double, double> to_screen(const View& v, double x, double y, double origin) {
        const double dx = x - 160.0, dy = y - 112.0, c = std::cos(v.roll), s = std::sin(v.roll);
        return {origin + 160.0 + dx * c + dy * s, 112.0 - dx * s + dy * c};
    }

    static void darken(std::span<uint32_t> px, double night, bool keep_black = false) {
        if (night <= 0.01) return;
        const unsigned k = unsigned(256.0 * std::clamp(1.0 - 0.82 * night, 0.1, 1.0));
        const unsigned blue = unsigned(18.0 * night);
        for (auto& p : px) {
            if (keep_black && !(p & 0xff000000u)) continue;
            const unsigned r = ((p >> 16) & 255u) * k >> 8, g = ((p >> 8) & 255u) * k >> 8, b = std::min(255u, ((p & 255u) * k >> 8) + blue);
            p = (p & 0xff000000u) | r << 16 | g << 8 | b;
        }
    }

    // The background plane: sea, night, beams, bolt, squadrons, fire.
    static void paint(const CustomStage::StagePackage& package, const View& v, std::span<uint32_t> px, unsigned width, unsigned height) {
        const auto& c = package.definition.conditions;
        const double origin = (double(width) - 320.0) / 2.0;
        if (v.sea && !c.water_art.empty()) sea(package, v, px, width, height);
        darken(px, v.night);
        // Searchlights: pale cones fanning up from the ground.
        for (const auto& b : v.beams) {
            const auto [bx, by] = to_screen(v, b.base_x, 250.0, origin);
            const double a = b.angle - v.roll;
            const double ca = std::cos(a), sa = std::sin(a);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const double dx = x + 0.5 - bx, dy = y + 0.5 - by;
                    const double along = dx * ca + dy * sa;
                    if (along <= 0) continue;
                    const double across = std::abs(-dx * sa + dy * ca);
                    const double half = 6.0 + along * b.width * 0.18;
                    if (across >= half) continue;
                    const double k = (1.0 - across / half) * std::max(0.0, 1.0 - along / 420.0);
                    const unsigned alpha = unsigned((b.on_player ? 150.0 : 95.0) * k);
                    BossPainter::blend(px[size_t(y) * width + x], 235, 240, 210, alpha);
                }
        }
        // Lightning bolt in the sky.
        for (size_t i = 1; i < v.bolt.size(); ++i) {
            const auto [x0, y0] = to_screen(v, v.bolt[i - 1].first, v.bolt[i - 1].second, origin);
            const auto [x1, y1] = to_screen(v, v.bolt[i].first, v.bolt[i].second, origin);
            const int steps = int(std::max(std::abs(x1 - x0), std::abs(y1 - y0))) + 1;
            for (int k = 0; k <= steps; ++k) {
                const int x = int(x0 + (x1 - x0) * k / steps), y = int(y0 + (y1 - y0) * k / steps);
                for (int o = -1; o <= 1; ++o)
                    if (x + o >= 0 && x + o < int(width) && y >= 0 && y < int(height))
                        BossPainter::blend(px[size_t(y) * width + size_t(x + o)], 235, 240, 255, o ? 160u : 255u);
            }
        }
        // Squadrons and ships, far to near, dimmed with the night but showing their lights.
        const auto fighter = c.fighter_art.empty() ? nullptr : package.image(c.fighter_art);
        const auto ship = c.ship_art.empty() ? nullptr : package.image(c.ship_art);
        for (const auto& u : v.units) {
            const auto image = u.kind == CustomStage::ConditionsDirector::UnitKind::ship ? ship : fighter;
            if (!image) continue;
            const double w = (u.kind == CustomStage::ConditionsDirector::UnitKind::ship ? 90.0 : 46.0) * u.scale;
            const double h = w * image->height / image->width;
            const auto [cx, cy] = to_screen(v, u.x, u.y, origin);
            sprite(*image, px, width, height, cx, cy, w, h, u.angle - v.roll, v.night, u.flash);
            if (v.night > 0.2 && int(v.time * 2.0 + u.x) % 2 == 0) {
                light(px, width, height, cx - w * 0.45, cy, 255, 60, 50);
                light(px, width, height, cx + w * 0.45, cy, 80, 255, 120);
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
                    const double q = ((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy)) / r2;
                    if (q <= 1) BossPainter::blend(px[size_t(y) * width + x], d.r, d.g, d.b, unsigned(255.0 * d.alpha * (q > 0.6 ? (1 - q) / 0.4 : 1.0)));
                }
        }
    }

private:
    static void light(std::span<uint32_t> px, unsigned width, unsigned height, double x, double y, unsigned r, unsigned g, unsigned b) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = int(x) + dx, yy = int(y) + dy;
                if (xx >= 0 && yy >= 0 && xx < int(width) && yy < int(height))
                    BossPainter::blend(px[size_t(yy) * width + size_t(xx)], r, g, b, dx || dy ? 140u : 255u);
            }
    }

    static void sprite(const CustomStage::DecodedImage& image, std::span<uint32_t> px, unsigned width, unsigned height,
                       double cx, double cy, double w, double h, double angle, double night, float flash) {
        if (w < 1 || h < 1) return;
        const double c = std::cos(angle), s = std::sin(angle), reach = std::hypot(w, h) / 2;
        const int x0 = std::max(0, int(cx - reach)), x1 = std::min(int(width) - 1, int(cx + reach));
        const int y0 = std::max(0, int(cy - reach)), y1 = std::min(int(height) - 1, int(cy + reach));
        const double dim = std::clamp(1.0 - 0.75 * night, 0.2, 1.0);
        const unsigned lit = unsigned(std::clamp(double(flash), 0.0, 1.0) * 150.0);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const double ox = x + 0.5 - cx, oy = y + 0.5 - cy;
                const double lx = ox * c + oy * s, ly = -ox * s + oy * c;
                if (std::abs(lx) >= w / 2 || std::abs(ly) >= h / 2) continue;
                const unsigned u = std::min(image.width - 1u, unsigned((lx + w / 2) / w * image.width));
                const unsigned v = std::min(image.height - 1u, unsigned((ly + h / 2) / h * image.height));
                const uint8_t* p = image.rgba.data() + (size_t(v) * image.width + u) * 4u;
                if (p[3] < 100) continue;
                unsigned r = unsigned(p[0] * dim), g = unsigned(p[1] * dim), b = unsigned(p[2] * dim);
                if (lit) { r += (255 - r) * lit / 255; g += (255 - g) * lit / 255; b += (255 - b) * lit / 255; }
                px[size_t(y) * width + x] = 0xff000000u | r << 16 | g << 8 | b;
            }
    }

    // Animated water over the board's ground, from the first ground row down.
    static void sea(const CustomStage::StagePackage& package, const View& v, std::span<uint32_t> px, unsigned width, unsigned height) {
        const auto water = package.image(package.definition.conditions.water_art);
        if (!water) return;
        const uint32_t ground = px[size_t(height - 1) * width + 4];
        int horizon = int(height);
        for (unsigned y = 0; y < height; ++y) if (px[size_t(y) * width + 3u] == ground) { horizon = int(y); break; }
        if (horizon >= int(height) - 2) return;
        const double f = 150.0, h = 22.0, cx = width / 2.0, t = v.time;
        for (int y = horizon + 1; y < int(height); ++y) {
            const double z = f * h / (y - horizon + 0.5);
            const double along = z + t * 120.0;
            const double distant = std::clamp(z / 260.0, 0.0, 1.0), mist = std::clamp(z / 1400.0, 0.0, 0.8);
            uint32_t* row = px.data() + size_t(y) * width;
            for (unsigned x = 0; x < width; ++x) {
                const double xs = (x + 0.5 - cx) * z / f;
                auto sample = [&](double su, double sv) {
                    long iu = long(std::floor(su)) % long(water->width), iv = long(std::floor(sv)) % long(water->height);
                    if (iu < 0) iu += water->width;
                    if (iv < 0) iv += water->height;
                    const uint8_t* p = water->rgba.data() + (size_t(iv) * water->width + size_t(iu)) * 4u;
                    return 0xff000000u | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
                };
                const uint32_t a = sample(xs * 6.0 + t * 4.0, -along * 3.2 + t * 18.0);
                const uint32_t b = sample(xs * 4.2 - t * 2.8 + 37.0, -along * 2.3 + t * 10.0 + 91.0);
                uint32_t c = ((a >> 8) & 255u) + (a & 255u) > ((b >> 8) & 255u) + (b & 255u) ? a : b;
                const auto mix = [](uint32_t p, uint32_t q, double k) {
                    const auto ch = [&](int s) { return unsigned(double((p >> s) & 255u) * (1 - k) + double((q >> s) & 255u) * k); };
                    return 0xff000000u | ch(16) << 16 | ch(8) << 8 | ch(0);
                };
                c = mix(c, ground, distant * 0.6);
                row[x] = mix(c, px[size_t(horizon) * width + 3u], mist);
            }
        }
    }
};

class ConditionsHud {
public:
    using View = CustomStage::ConditionsDirector::View;
    using Text = std::function<void(const std::string&, float, float, SDL_Color, float)>;

    static void draw(SDL_Renderer* r, const View* v, const Text& text) {
        if (!r || !v) return;
        SDL_BlendMode previous = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(r, &previous);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const SDL_Color red{255, 80, 64, 255}, amber{255, 196, 64, 255}, green{96, 255, 140, 255}, white{235, 245, 255, 255};
        // Weather first, so the instruments stay readable above it.
        if (v->fog > 0.01) {
            for (int y = 0; y < 224; ++y) {
                const double k = v->fog * (0.25 + 0.5 * std::exp(-std::abs(y - 100) / 60.0));
                SDL_SetRenderDrawColor(r, 200, 206, 214, uint8_t(std::min(200.0, 200.0 * k)));
                SDL_FRect band{0, float(y), 320, 1};
                SDL_RenderFillRect(r, &band);
            }
        }
        if (v->rain > 0.01) {
            SDL_SetRenderDrawColor(r, 190, 205, 230, uint8_t(90 + 80 * v->rain));
            const int drops = int(120 * v->rain);
            for (int i = 0; i < drops; ++i) {
                const uint32_t h = uint32_t(i) * 2654435761u;
                const double speed = 260 + double(h % 140);
                const double x = double(h % 340) - 10 + std::sin(v->time * 0.3) * 6;
                const double y = std::fmod(double((h >> 8) % 224) + v->time * speed, 236.0) - 12;
                SDL_RenderLine(r, float(x), float(y), float(x - 3), float(y + 9));
            }
        }
        if (v->lightning > 0.05f) {
            SDL_SetRenderDrawColor(r, 235, 240, 255, uint8_t(150 * v->lightning));
            SDL_FRect all{0, 0, 320, 224};
            SDL_RenderFillRect(r, &all);
        }
        BossHud::threats_of(r, v->threats, *v);
        // Lock boxes and the gun sight.
        for (const auto& u : v->units) {
            if (u.x < -20 || u.x > 340 || u.y < -20 || u.y > 244) continue;
            const auto [sx, sy] = ConditionsPainter::to_screen(*v, u.x, u.y, 0.0);
            const float hw = float(std::max(6.0, 22.0 * u.scale)), hh = float(std::max(5.0, 12.0 * u.scale));
            SDL_SetRenderDrawColor(r, u.locked ? red.r : amber.r, u.locked ? red.g : amber.g, u.locked ? red.b : amber.b, u.locked ? 230 : 120);
            corners(r, float(sx), float(sy), hw, hh);
            if (u.locked) text("LOCK", float(sx) - 16, float(sy) - hh - 10, red, 1);
        }
        if (!v->units.empty()) {
            const auto [ax, ay] = ConditionsPainter::to_screen(*v, v->aim_x, v->aim_y, 0.0);
            SDL_SetRenderDrawColor(r, 96, 255, 140, 200);
            const float x0 = float(ax), y0 = float(ay);
            SDL_RenderLine(r, x0 - 9, y0, x0 - 4, y0); SDL_RenderLine(r, x0 + 4, y0, x0 + 9, y0);
            SDL_RenderLine(r, x0, y0 - 9, x0, y0 - 4); SDL_RenderLine(r, x0, y0 + 4, x0, y0 + 9);
        }
        // Mission: a briefing card at the start, then a tracker.
        if (v->mission && v->briefing) {
            if (v->briefing_age < 4.0f) {
                SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
                SDL_FRect card{40, 70, 240, 40};
                SDL_RenderFillRect(r, &card);
                const std::string& b = *v->briefing;
                const float scale = b.size() <= 14 ? 2.f : 1.f;
                text(b, 160 - float(b.size()) * 4 * scale, 74, amber, scale);
                if (v->detail) text(*v->detail, 160 - float(v->detail->size()) * 4, 74 + 10 * scale + 4, white, 1);
            }
            char line[40];
            // The arcade font has no slash or colon.
            if (v->target) std::snprintf(line, sizeof line, "KILLS %u OF %u", std::min(v->kills, v->target), v->target);
            else std::snprintf(line, sizeof line, "SURVIVE  KILLS %u", v->kills);
            text(line, 14, 64, v->target && v->kills >= v->target ? green : white, 1);
            const int left = std::max(0, int(v->time_limit - v->mission_time));
            std::snprintf(line, sizeof line, "TIME %d", left);
            text(line, 14, 74, left < 15 ? red : white, 1);
            if (*v->result && v->result_age < 3.5f) {
                SDL_SetRenderDrawColor(r, 0, 0, 0, 140);
                SDL_FRect card{40, 84, 240, 26};
                SDL_RenderFillRect(r, &card);
                const std::string res = v->result;
                text(res, 160 - float(res.size()) * 8, 89, res == "MISSION COMPLETE" ? green : red, 2);
            }
        }
        if (v->hit_flash > 0.f) {
            SDL_SetRenderDrawColor(r, 255, 40, 30, uint8_t(150 * v->hit_flash));
            for (float k = 0; k < 6; ++k) { SDL_FRect e{k, k, 320 - 2 * k, 224 - 2 * k}; SDL_RenderRect(r, &e); }
        }
        if (*v->alert && v->alert_age < 1.6f && int(v->alert_age * 6) % 2 == 0) {
            const std::string a = v->alert;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
            SDL_FRect box{160 - float(a.size()) * 4 - 6, 150, float(a.size()) * 8 + 12, 12};
            SDL_RenderFillRect(r, &box);
            text(a, 160 - float(a.size()) * 4, 152, a.rfind("HIT", 0) == 0 || a == "SPOTTED" ? red : amber, 1);
        }
        SDL_SetRenderDrawBlendMode(r, previous);
    }

private:
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

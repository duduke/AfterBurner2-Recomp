#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include "engine/custom_stage_assets.hpp"
#include "engine/game_session.hpp"
#include "frontend/music_decoder.hpp"
#include "frontend/native_boss.hpp"

namespace Frontend {

// PNG and BMP, through SDL so the engine stays independent of it. Used for every
// image a stage loads, from a folder or from inside a ZIP.
inline CustomStage::DecodedImage decode_stage_image(std::span<const uint8_t> bytes, const std::string& name) {
    struct SurfaceDelete { void operator()(SDL_Surface* s) const { SDL_DestroySurface(s); } };
    using Surface = std::unique_ptr<SDL_Surface, SurfaceDelete>;
    const auto open = [&] { return SDL_IOFromConstMem(bytes.data(), bytes.size()); };
    Surface loaded;
#if SDL_VERSION_ATLEAST(3,4,0)
    if (auto* io = open()) loaded.reset(SDL_LoadPNG_IO(io, true));
#endif
    if (!loaded) if (auto* io = open()) loaded.reset(SDL_LoadBMP_IO(io, true));
    if (!loaded) throw std::runtime_error("Cannot decode image " + name + " (PNG or BMP required)");
    if (loaded->w < 1 || loaded->h < 1 || loaded->w > 8192 || loaded->h > 8192)
        throw std::runtime_error("Image " + name + " must be at most 8192x8192");
    Surface rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));
    if (!rgba) throw std::runtime_error(std::string("Cannot convert image ") + name + ": " + SDL_GetError());
    CustomStage::DecodedImage image;
    image.width = unsigned(rgba->w);
    image.height = unsigned(rgba->h);
    image.rgba.resize(size_t(image.width) * image.height * 4u);
    for (unsigned y = 0; y < image.height; ++y)
        std::copy_n(static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * rgba->pitch, image.width * 4u,
                    image.rgba.data() + size_t(y) * image.width * 4u);
    return image;
}

inline std::shared_ptr<CustomStage::StagePackage> load_stage_package(const std::string& source,
                                                                     const std::vector<std::string>& extra_assets) {
    return CustomStage::StagePackage::load(source, extra_assets, decode_stage_image);
}

inline std::shared_ptr<const DecodedMusic> decode_stage_music(const CustomStage::StagePackage& package) {
    if (!package.music_bytes) return {};
    std::string error;
    auto music = decode_music_memory(package.music_bytes->data(), package.music_bytes->size(), error);
    if (!music) throw std::runtime_error("Stage music " + *package.definition.music_file + ": " + error);
    return music;
}

// Presents a stage's external images. Overlay layers (order >= 0) draw on top of
// the finished frame as GPU textures at output resolution. The sky backdrop and
// layers ordered below zero are composited on the CPU into the native planes,
// where the sprites are still separate, so they appear behind gameplay.
// Never writes to the board.
class NativeCustomStage {
public:
    explicit NativeCustomStage(SDL_Renderer* target) : renderer(target) {}
    ~NativeCustomStage() { release_textures(); }
    NativeCustomStage(const NativeCustomStage&) = delete;
    NativeCustomStage& operator=(const NativeCustomStage&) = delete;

    void set_package(std::shared_ptr<const CustomStage::StagePackage> next) {
        release_textures();
        package = std::move(next);
        elapsed_frames = 0;
    }
    const CustomStage::StagePackage* current() const { return package.get(); }

    // --- Sky compositing ---

    struct Planes {
        std::span<const uint32_t> background, sprites;
        std::span<const uint32_t> heads;
        std::span<const Presentation::Fragment> fragments;
    };

    // Returns true when `planes` was redirected to composited copies. Key-colored
    // sprite pixels (the horizon strips) are removed so the image shows through.
    bool composite_sky(const GameSession& game, Planes& planes, unsigned width, unsigned height) {
        const bool sky = composite_keyed_sky(game, planes, width, height);
        const auto* boss = package ? game.boss_view() : nullptr;
        const bool deck = carrier_visible(game);
        if (!boss && !deck) return sky;
        // Painted into the background plane so sprites stay in front.
        if (planes.background.data() != out_background.data())
            out_background.assign(planes.background.begin(), planes.background.end());
        if (deck) paint_carrier(game.rearm_hud().deck, out_background, width, height);
        if (boss) BossPainter::paint(*package, *boss, out_background, width, height);
        planes.background = out_background;
        return true;
    }

    bool composite_keyed_sky(const GameSession& game, Planes& planes, unsigned width, unsigned height) {
        const auto sky_state = game.custom_stage_sky();
        if (!sky_state.active || !package) return false;
        paint_sky(game, sky_state, width, height);
        const uint32_t key = argb(sky_state.key), blending = argb(sky_state.blending_key);
        out_background.assign(planes.background.begin(), planes.background.end());
        for (size_t i = 0; i < out_background.size(); ++i)
            if (out_background[i] == key || out_background[i] == blending) out_background[i] = sky_pixels[i];
        out_sprites.assign(planes.sprites.begin(), planes.sprites.end());
        for (size_t i = 0; i < out_sprites.size(); ++i)
            if (out_sprites[i] == key || out_sprites[i] == blending) { out_sprites[i] = 0; out_background[i] = sky_pixels[i]; }
        planes.background = out_background;
        planes.sprites = out_sprites;
        if (!planes.heads.empty() && !planes.fragments.empty()) {
            out_fragments.assign(planes.fragments.begin(), planes.fragments.end());
            const auto background_kind = uint32_t(Presentation::Effect::background);
            const auto shadow_kind = uint32_t(Presentation::Effect::shadow);
            const auto opaque_kind = uint32_t(Presentation::Effect::opaque);
            for (size_t i = 0; i < planes.heads.size(); ++i) {
                uint32_t head = planes.heads[i];
                while (head != UINT32_MAX) {
                    auto& fragment = out_fragments[head];
                    if (fragment.kind == background_kind || fragment.kind == shadow_kind) break;
                    if (fragment.color == key || fragment.color == blending) {
                        // The compositor stops at a background fragment and shows the plane.
                        fragment.kind = background_kind;
                        out_background[i] = sky_pixels[i];
                        break;
                    }
                    if (fragment.kind == opaque_kind) break;
                    head = fragment.previous;
                }
            }
            planes.fragments = out_fragments;
        }
        return true;
    }

    // The plain native raster has no separate planes. Keying its finished pixels
    // covers the "everything off" display path.
    void composite_sky_raster(const GameSession& game, std::span<uint32_t> pixels, unsigned width, unsigned height) {
        const auto sky_state = game.custom_stage_sky();
        if (sky_state.active && package) {
            paint_sky(game, sky_state, width, height);
            const uint32_t key = argb(sky_state.key), blending = argb(sky_state.blending_key);
            for (size_t i = 0; i < pixels.size(); ++i)
                if (pixels[i] == key || pixels[i] == blending) pixels[i] = sky_pixels[i];
        }
        if (carrier_visible(game)) paint_carrier(game.rearm_hud().deck, pixels, width, height);
        if (const auto* boss = package ? game.boss_view() : nullptr)
            BossPainter::paint(*package, *boss, pixels, width, height);
    }

    // --- Overlay layers ---

    // Drawn in logical viewport coordinates. Clears the safe-area viewport so a
    // layer can reach the side margins of a widescreen view.
    void draw_overlays(const GameSession& game, unsigned viewport_width) {
        if (!package || !game.custom_stage_in_slot()) return;
        const auto& def = package->definition;
        if (std::all_of(def.layers.begin(), def.layers.end(), [](const auto& l) { return l.order < 0.0; })) return;
        SDL_Rect previous{};
        const bool had_viewport = SDL_RenderViewportSet(renderer);
        SDL_GetRenderViewport(renderer, &previous);
        SDL_SetRenderViewport(renderer, nullptr);
        SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &previous_blend);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        const float origin = (float(viewport_width) - 320.f) / 2.f;
        const auto camera = stage_camera(game);
        const uint16_t tick = game.custom_stage_tick();
        for (const auto& layer : def.layers) {
            if (layer.order < 0.0) continue;
            for (const auto& element : layer.elements) {
                if (!element_visible(element, tick)) continue;
                const auto placed = place(layer, element, camera, game);
                if (!placed.visible) continue;
                draw_element(element, placed, origin);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, previous_blend);
        SDL_SetRenderViewport(renderer, had_viewport ? &previous : nullptr);
    }

private:
    SDL_Renderer* renderer;
    std::shared_ptr<const CustomStage::StagePackage> package;
    std::map<std::string, SDL_Texture*> textures;
    std::vector<uint32_t> sky_pixels, out_background, out_sprites;
    std::vector<Presentation::Fragment> out_fragments;
    uint64_t elapsed_frames{}, last_painted_frame{UINT64_MAX};
    unsigned painted_width{};

    static uint32_t argb(RGB888 color) {
        return 0xff000000u | uint32_t(color.r) << 16 | uint32_t(color.g) << 8 | color.b;
    }

    bool carrier_visible(const GameSession& game) const {
        return package && package->definition.carrier_art && game.custom_stage_in_slot() &&
               game.rearm_hud().deck.active;
    }

    // The carrier in perspective, in three planes: the flight deck, the hull
    // (transom and sides) between deck and waterline, and the sea below it.
    // The horizon is the first row of the board's own sea, so everything sits
    // under the arcade's sky; the runway underneath is painted out by the stage.
    void paint_carrier(const Rearm::Hud::Deck& d, std::span<uint32_t> px, unsigned width, unsigned height) {
        const auto& art = *package->definition.carrier_art;
        const auto deck = package->image(art.deck);
        if (!deck || width < 8 || height < 8) return;
        // Sea: the commonest color along the bottom row of the board's picture.
        const uint32_t* bottom = px.data() + size_t(height - 1) * width;
        uint32_t sea = bottom[4], alt = sea;
        unsigned count = 0;
        for (unsigned x = 0; x < width; x += 3) {
            unsigned n = 0;
            for (unsigned k = 0; k < width; k += 3) n += bottom[k] == bottom[x];
            if (n > count) { count = n; sea = bottom[x]; }
        }
        for (unsigned x = 0; x < width; x += 3) if (bottom[x] != sea) { alt = bottom[x]; break; }
        int horizon = int(height);
        for (unsigned y = 0; y < height; ++y) {
            const uint32_t c = px[size_t(y) * width + 3u];
            if (c == sea || c == alt) { horizon = int(y); break; }
        }
        if (horizon >= int(height) - 2) return;

        const double f = 150.0, cx = width / 2.0;
        const double hd = d.height, hs = d.height + d.hull;      // camera above deck / sea
        const double wt = d.half_width * 1.25, hw = d.half_width * 0.92;
        const auto* tex = deck->rgba.data();
        const unsigned tw = deck->width, th = deck->height;
        const auto water = art.water.empty() ? nullptr : package->image(art.water);
        const double t = double(elapsed_frames) / 60.0;
        const uint32_t haze = px[size_t(horizon) * width + 3u];
        const double zt = -d.along;                               // to the stern face, when astern
        auto rgb = [](unsigned r, unsigned g, unsigned b) { return 0xff000000u | r << 16 | g << 8 | b; };
        auto mix = [](uint32_t a, uint32_t b, double k) {
            const auto ch = [&](int s) { return unsigned(double((a >> s) & 255u) * (1 - k) + double((b >> s) & 255u) * k); };
            return 0xff000000u | ch(16) << 16 | ch(8) << 8 | ch(0);
        };
        for (int y = horizon + 1; y < int(height); ++y) {
            const double dy = y - horizon + 0.5;
            const double zd = f * hd / dy, zs = f * hs / dy;
            const double along_d = d.along + zd, along_s = d.along + zs;
            const bool deck_row = along_d >= 0.0 && along_d < d.length;
            const unsigned v = deck_row ? std::min(th - 1u, unsigned((1.0 - along_d / d.length) * th)) : 0u;
            const bool transom_row = zt > 0.5 && dy >= f * hd / zt && dy <= f * hs / zt;
            const double depth = transom_row ? (dy * zt / f - hd) / d.hull : 0.0; // 0 deck edge .. 1 waterline
            const double distant = std::clamp(zs / 260.0, 0.0, 1.0), mist = std::clamp(zs / 1400.0, 0.0, 0.85);
            uint32_t* row = px.data() + size_t(y) * width;
            for (unsigned x = 0; x < width; ++x) {
                const double rx = (x + 0.5 - cx) / f;
                // 1. Flight deck.
                if (deck_row) {
                    const double xd = d.lateral + rx * zd;
                    if (std::abs(xd) < wt) {
                        const unsigned u = std::min(tw - 1u, unsigned((xd + wt) / (2.0 * wt) * tw));
                        const uint8_t* p = tex + (size_t(v) * tw + u) * 4u;
                        if (p[3] >= 128) { row[x] = rgb(p[0], p[1], p[2]); continue; }
                    }
                }
                // 2. Transom: the stern face below the deck edge.
                if (transom_row && std::abs(d.lateral + rx * zt) < hw) {
                    row[x] = depth > 0.82 ? rgb(150, 40, 36) : depth > 0.76 ? rgb(30, 30, 34)
                                          : rgb(unsigned(96 - 30 * depth), unsigned(100 - 30 * depth), unsigned(110 - 28 * depth));
                    continue;
                }
                // 3. Hull sides, lit on the port side, shaded to starboard.
                bool side = false;
                for (const double s : {-1.0, 1.0}) {
                    if (rx * s <= 1e-6) continue;
                    const double z = (s * hw - d.lateral) / rx;
                    const double drop = z * dy / f;
                    if (z > 0.5 && drop >= hd && drop <= hs && d.along + z >= 0.0 && d.along + z <= d.length * 0.9) {
                        const double k = (drop - hd) / d.hull;
                        const unsigned base = s < 0 ? 104u : 74u;
                        row[x] = k > 0.82 ? rgb(150, 40, 36) : rgb(unsigned(base - 24 * k), unsigned(base + 4 - 24 * k), unsigned(base + 12 - 20 * k));
                        side = true;
                        break;
                    }
                }
                if (side) continue;
                // 4. The sea: arcade-style swell, drifting, fading into the haze far out.
                const double xs = d.lateral + rx * zs;
                uint32_t c = sea;
                if (water) {
                    const auto sample = [&](double su, double sv) {
                        const long iu = long(std::floor(su)) % long(water->width), iv = long(std::floor(sv)) % long(water->height);
                        const uint8_t* p = water->rgba.data() + (size_t(iv < 0 ? iv + water->height : iv) * water->width +
                                                                  size_t(iu < 0 ? iu + water->width : iu)) * 4u;
                        return rgb(p[0], p[1], p[2]);
                    };
                    const uint32_t a = sample(xs * 6.0 + t * 4.0, -along_s * 3.2 + t * 18.0);
                    const uint32_t b = sample(xs * 4.2 - t * 2.8 + 37.0, -along_s * 2.3 + t * 10.0 + 91.0);
                    c = ((a >> 8) & 255u) + (a & 255u) > ((b >> 8) & 255u) + (b & 255u) ? a : b; // brighter crest shows
                    c = mix(c, sea, distant * 0.75);
                }
                // The carrier's own wake astern, churned white and widening.
                if (along_s < 0.0 && along_s > -360.0) {
                    const double spread = d.half_width * 0.5 + (-along_s) * 0.06;
                    const double k = std::abs(xs) / spread;
                    if (k < 1.0) {
                        const unsigned n = (unsigned(along_s * 5.0 + t * 30.0) * 2654435761u ^ unsigned(xs * 11.0 + 999) * 40503u) >> 27;
                        c = mix(c, rgb(232, 244, 250), std::min(1.0, (1.0 + along_s / 360.0) * (1.0 - k) * (0.45 + n * 0.02)));
                    }
                }
                row[x] = mix(c, haze, mist);
            }
        }

        // Wake disturbance after liftoff: churned spray spots under the plane.
        for (const auto& w : d.wake) {
            const double z = w.along - d.along;
            if (z < 2.0) continue;
            const double sy = horizon + f * hs / z;
            const double scale = f / z;
            const double sx = cx + (w.lateral - d.lateral) * scale;
            // Churned water, elongated along the track so the spots read as one streak.
            const double rx = (1.6 + 4.5 * w.age) * scale, ry = rx * 0.9;
            const double fade = w.strength * std::max(0.0, 1.0 - w.age / 1.8);
            if (fade <= 0.02 || sy - ry > height) continue;
            const int x0 = std::max(0, int(sx - rx)), x1 = std::min(int(width) - 1, int(sx + rx));
            const int y0 = std::max(horizon + 1, int(sy - ry)), y1 = std::min(int(height) - 1, int(sy + ry));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const double q = std::hypot((x + 0.5 - sx) / rx, (y + 0.5 - sy) / ry);
                    if (q >= 1.0) continue;
                    const unsigned n = (unsigned(x * 7 + int(t * 24.0)) * 2654435761u ^ unsigned(y * 13) * 40503u) >> 26;
                    const double a = fade * std::sqrt(1.0 - q) * (0.35 + 0.65 * double(n) / 63.0) * 200.0;
                    BossPainter::blend(px[size_t(y) * width + size_t(x)], 236, 246, 252, unsigned(std::min(255.0, a)));
                }
        }

        // Things standing on the deck, drawn far to near: the island and parked jets.
        struct Standing { const CustomStage::DecodedImage* image; double along, lateral, height; bool flip; };
        std::vector<Standing> standing;
        const auto island = art.island.empty() ? nullptr : package->image(art.island);
        if (island) standing.push_back({island.get(), d.length * 0.52, d.half_width * 0.86, 34.0, false});
        if (const auto jet = art.jet.empty() ? nullptr : package->image(art.jet)) {
            static constexpr std::array<std::pair<double, double>, 9> spots{{
                {0.60, 0.80}, {0.65, 0.80}, {0.70, 0.80}, {0.40, 0.84},
                {0.78, -0.66}, {0.83, -0.66}, {0.88, -0.60}, {0.92, 0.30}, {0.35, -0.62}}};
            for (const auto& [a, x] : spots)
                standing.push_back({jet.get(), d.length * a, d.half_width * x, 4.6, x < 0});
        }
        std::sort(standing.begin(), standing.end(), [&](const Standing& a, const Standing& b) { return a.along > b.along; });
        for (const auto& s : standing) {
            const double z = s.along - d.along;
            if (z <= 3.0) continue;
            const double base_y = horizon + f * hd / z;
            const double sx = cx + (s.lateral - d.lateral) * f / z;
            const double h = s.height * f / z, w = h * s.image->width / s.image->height;
            const int x0 = std::max(0, int(sx - w / 2)), x1 = std::min(int(width), int(sx + w / 2));
            const int y0 = std::max(0, int(base_y - h)), y1 = std::min(int(height), int(base_y));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    unsigned u = std::min(s.image->width - 1u, unsigned((x - (sx - w / 2)) / w * s.image->width));
                    if (s.flip) u = s.image->width - 1u - u;
                    const unsigned vv = std::min(s.image->height - 1u, unsigned((y - (base_y - h)) / h * s.image->height));
                    const uint8_t* p = s.image->rgba.data() + (size_t(vv) * s.image->width + u) * 4u;
                    if (p[3] >= 128) px[size_t(y) * width + x] = rgb(p[0], p[1], p[2]);
                }
        }
    }

    void release_textures() {
        for (auto& [name, texture] : textures) SDL_DestroyTexture(texture);
        textures.clear();
    }

    static bool element_visible(const CustomStage::LayerElement& element, uint16_t tick) {
        return !element.tick_begin || (tick >= *element.tick_begin && tick <= *element.tick_end);
    }

    static CustomStage::Camera stage_camera(const GameSession& game) {
        const auto* controller = game.custom_stage_controller();
        if (!controller) return {};
        return {controller->x, controller->height, 198.0 - controller->pitch * 42.0, 480.0, 34.0};
    }

    // Layouts use a 960x600 canvas, whatever the window size: element x/y are the
    // center of the (first) tile. World layers project through the stage camera.
    struct Placed { float x{}, y{}, width{}, height{}; bool visible{}; };
    static Placed place(const CustomStage::SceneLayer& layer, const CustomStage::LayerElement& element,
                        const CustomStage::Camera& camera, const GameSession& game) {
        if (layer.space == CustomStage::SceneLayer::Space::screen)
            return {float(element.x / 3.0), float(element.y * 224.0 / 600.0),
                    std::max(1.f, float(element.width / 3.0)), std::max(1.f, float(element.height * 224.0 / 600.0)), true};
        const auto* controller = game.custom_stage_controller();
        const double z = element.z + (controller ? controller->surface_start() : 0.0);
        const auto projected = camera.project(element.x, element.y, z);
        if (!projected.visible) return {};
        return {projected.x / 3.f, projected.y * 224.f / 600.f,
                std::max(1.f, float(camera.span(element.width, z) / 3.0)),
                std::max(1.f, float(camera.span(element.height, z) * 224.0 / 600.0)), true};
    }

    SDL_Texture* texture_for(const std::string& asset, bool nearest) {
        const auto key = CustomStage::lowercase_ascii(asset) + (nearest ? "|n" : "|l");
        const auto found = textures.find(key);
        if (found != textures.end()) return found->second;
        const auto image = package->image(asset);
        if (!image) throw std::runtime_error("Stage image was not loaded: " + asset);
        auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                          int(image->width), int(image->height));
        if (!texture || !SDL_UpdateTexture(texture, nullptr, image->rgba.data(), int(image->width) * 4) ||
            !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
            !SDL_SetTextureScaleMode(texture, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR)) {
            const std::string error = SDL_GetError();
            SDL_DestroyTexture(texture);
            throw std::runtime_error("Cannot create texture for " + asset + ": " + error);
        }
        textures.emplace(key, texture);
        return texture;
    }

    void draw_element(const CustomStage::LayerElement& element, const Placed& placed, float origin) {
        SDL_Texture* texture = nullptr;
        if (element.type == CustomStage::LayerElement::Type::image) {
            texture = texture_for(element.asset, element.nearest);
            SDL_SetTextureColorMod(texture, element.color[0], element.color[1], element.color[2]);
            SDL_SetTextureAlphaMod(texture, element.color[3]);
        } else {
            SDL_SetRenderDrawColor(renderer, element.color[0], element.color[1], element.color[2], element.color[3]);
        }
        // Anything outside the widest supported view cannot appear, so very large
        // repeat grids cost only the tiles actually on screen.
        constexpr float margin = 320.f;
        for (unsigned row = 0; row < element.rows; ++row) {
            const float top = placed.y + placed.height * (float(row) - .5f);
            if (top > 224.f + margin) break;
            if (top + placed.height < -margin) continue;
            for (unsigned column = 0; column < element.columns; ++column) {
                const float left = origin + placed.x + placed.width * (float(column) - .5f);
                if (left > origin + 320.f + margin) break;
                if (left + placed.width < origin - margin) continue;
                const SDL_FRect rect{left, top, placed.width, placed.height};
                if (texture) SDL_RenderTextureRotated(renderer, texture, nullptr, &rect, element.rotation, nullptr, SDL_FLIP_NONE);
                else SDL_RenderFillRect(renderer, &rect);
            }
        }
    }

    // --- CPU sky ---

    void paint_sky(const GameSession& game, const GameSession::SkyState& state, unsigned width, unsigned height) {
        const auto frame = game.info().number;
        if (frame == last_painted_frame && width == painted_width && sky_pixels.size() == size_t(width) * height) return;
        last_painted_frame = frame;
        painted_width = width;
        ++elapsed_frames;
        sky_pixels.resize(size_t(width) * height);
        const auto& def = package->definition;
        if (def.backdrop) paint_backdrop(*def.backdrop, state, width, height);
        else std::fill(sky_pixels.begin(), sky_pixels.end(), argb(state.fill));
        const auto camera = stage_camera(game);
        const uint16_t tick = game.custom_stage_tick();
        const float origin = (float(width) - 320.f) / 2.f;
        for (const auto& layer : def.layers) {
            if (layer.order >= 0.0) continue;
            for (const auto& element : layer.elements) {
                if (!element_visible(element, tick)) continue;
                const auto placed = place(layer, element, camera, game);
                if (placed.visible) blend_element(element, placed, origin, width, height);
            }
        }
        if (state.brightness < 1.f) dim(state.brightness);
    }

    void dim(float brightness) {
        const auto scale = uint32_t(std::lround(brightness * 256.f));
        for (auto& pixel : sky_pixels) {
            const uint32_t rb = ((pixel & 0xff00ffu) * scale >> 8) & 0xff00ffu;
            const uint32_t g = ((pixel & 0x00ff00u) * scale >> 8) & 0x00ff00u;
            pixel = 0xff000000u | rb | g;
        }
    }

    // The image scrolls horizontally without end and is scaled so its height spans
    // a little more than the screen, leaving room to move vertically with the
    // aircraft. It tilts about the screen center so it stays aligned with the
    // horizon, which the board rolls as the aircraft banks.
    void paint_backdrop(const CustomStage::Backdrop& backdrop, const GameSession::SkyState& state,
                        unsigned width, unsigned height) {
        const auto image = package->image(backdrop.asset);
        if (!image) throw std::runtime_error("Stage image was not loaded: " + backdrop.asset);
        constexpr double overscan = 1.4;
        const double scale = double(image->height) / (height * overscan);
        const double seconds = double(elapsed_frames) * GameSession::frame_seconds;
        const double shift_x = state.camera_x * backdrop.parallax_x + backdrop.drift_x * seconds;
        const double shift_y = state.camera_y * backdrop.parallax_y + backdrop.drift_y * seconds;
        constexpr double tau = 6.283185307179586;
        const double angle = backdrop.follow_roll ? double(int8_t(state.roll)) * tau / 256.0 : 0.0;
        const double cosine = std::cos(angle), sine = std::sin(angle);
        const double cx = width / 2.0, cy = height / 2.0;
        const double image_cy = image->height / 2.0;
        const auto image_w = int64_t(image->width), image_h = int64_t(image->height);
        const auto* source = image->rgba.data();
        size_t out = 0;
        for (unsigned y = 0; y < height; ++y) {
            const double dy = y + 0.5 - cy;
            for (unsigned x = 0; x < width; ++x, ++out) {
                const double dx = x + 0.5 - cx;
                const double rx = dx * cosine - dy * sine, ry = dx * sine + dy * cosine;
                const double u = (rx + shift_x) * scale + image_w / 2.0;
                const double v = (ry + shift_y) * scale + image_cy;
                auto sx = int64_t(std::floor(u)) % image_w;
                if (sx < 0) sx += image_w;
                const auto sy = std::clamp<int64_t>(int64_t(std::floor(v)), 0, image_h - 1);
                const auto* p = source + (size_t(sy) * size_t(image_w) + size_t(sx)) * 4u;
                sky_pixels[out] = 0xff000000u | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
            }
        }
    }

    void blend_element(const CustomStage::LayerElement& element, const Placed& placed, float origin,
                       unsigned width, unsigned height) {
        const auto image = element.type == CustomStage::LayerElement::Type::image ? package->image(element.asset) : nullptr;
        constexpr double tau = 6.283185307179586;
        const double angle = element.rotation * tau / 360.0;
        const double cosine = std::cos(angle), sine = std::sin(angle);
        for (unsigned row = 0; row < element.rows; ++row) {
            for (unsigned column = 0; column < element.columns; ++column) {
                const double cx = double(origin) + placed.x + placed.width * double(column);
                const double cy = double(placed.y) + placed.height * double(row);
                const double half_w = placed.width / 2.0, half_h = placed.height / 2.0;
                const double reach = std::hypot(half_w, half_h);
                if (cx + reach < 0 || cy + reach < 0 || cx - reach >= width || cy - reach >= height) continue;
                const int x0 = std::max(0, int(cx - reach)), x1 = std::min(int(width) - 1, int(cx + reach));
                const int y0 = std::max(0, int(cy - reach)), y1 = std::min(int(height) - 1, int(cy + reach));
                for (int y = y0; y <= y1; ++y) {
                    for (int x = x0; x <= x1; ++x) {
                        const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
                        const double lx = dx * cosine + dy * sine, ly = -dx * sine + dy * cosine;
                        if (std::abs(lx) > half_w || std::abs(ly) > half_h) continue;
                        std::array<unsigned, 4> color{element.color[0], element.color[1], element.color[2], element.color[3]};
                        if (image) {
                            const auto sx = std::min(image->width - 1u, unsigned((lx + half_w) / placed.width * image->width));
                            const auto sy = std::min(image->height - 1u, unsigned((ly + half_h) / placed.height * image->height));
                            const auto* p = image->rgba.data() + (size_t(sy) * image->width + sx) * 4u;
                            for (unsigned c = 0; c < 4u; ++c) color[c] = color[c] * p[c] / 255u;
                        }
                        if (!color[3]) continue;
                        auto& pixel = sky_pixels[size_t(y) * width + size_t(x)];
                        const unsigned inverse = 255u - color[3];
                        const unsigned r = (color[0] * color[3] + ((pixel >> 16) & 255u) * inverse) / 255u;
                        const unsigned g = (color[1] * color[3] + ((pixel >> 8) & 255u) * inverse) / 255u;
                        const unsigned b = (color[2] * color[3] + (pixel & 255u) * inverse) / 255u;
                        pixel = 0xff000000u | r << 16 | g << 8 | b;
                    }
                }
            }
        }
    }
};

} // namespace Frontend

#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
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

    // A carrier floor in perspective. The horizon is the first row of the
    // board's own sea, so the deck sits on the same water the arcade draws;
    // the runway underneath is painted sea-colored by the stage.
    void paint_carrier(const Rearm::Hud::Deck& d, std::span<uint32_t> px, unsigned width, unsigned height) {
        const auto& art = *package->definition.carrier_art;
        const auto deck = package->image(art.deck);
        if (!deck || width < 8 || height < 8) return;
        // Sea: the commonest color along the bottom row.
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
        const double wt = d.half_width * 1.25;             // texture spans the overhangs
        const auto* tex = deck->rgba.data();
        const unsigned tw = deck->width, th = deck->height;
        for (int y = horizon + 1; y < int(height); ++y) {
            const double dy = y - horizon + 0.5;
            const double z = f * d.height / dy;
            const double along = d.along + z;
            uint32_t* row = px.data() + size_t(y) * width;
            const double step = z / f;
            if (along >= 0.0 && along < d.length) {
                const unsigned v = std::min(th - 1u, unsigned((1.0 - along / d.length) * th));
                double world = d.lateral + (0.5 - cx) * step;
                for (unsigned x = 0; x < width; ++x, world += step) {
                    if (world <= -wt || world >= wt) continue;
                    const unsigned u = std::min(tw - 1u, unsigned((world + wt) / (2.0 * wt) * tw));
                    const uint8_t* p = tex + (size_t(v) * tw + u) * 4u;
                    if (p[3] < 128) continue;
                    row[x] = 0xff000000u | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
                }
            } else if (along < 0.0 && along > -8.0) {
                // The transom below the stern edge.
                double world = d.lateral + (0.5 - cx) * step;
                for (unsigned x = 0; x < width; ++x, world += step)
                    if (std::abs(world) < d.half_width * 0.9) row[x] = 0xff3a3e48u;
            } else if (along <= -8.0 && along > -320.0) {
                // Churned wake astern, widening and fading.
                const double spread = d.half_width * 0.45 + (-along) * 0.06;
                const double fade = 1.0 + along / 320.0;
                double world = d.lateral + (0.5 - cx) * step;
                for (unsigned x = 0; x < width; ++x, world += step) {
                    const double a = std::abs(world) / spread;
                    if (a >= 1.0) continue;
                    const unsigned n = (unsigned(along * 7.0) * 2654435761u ^ unsigned(world * 13.0 + 999) * 40503u) >> 27;
                    const unsigned k = unsigned(fade * (1.0 - a) * (120 + n * 3));
                    BossPainter::blend(row[x], 235, 245, 250, std::min(255u, k));
                }
            }
        }
        // Jet-wash rings spreading on the water under the departing plane. Each is
        // an ellipse in perspective, anchored where the water under it would be,
        // kept just inside the bottom edge like the plane's own shadow.
        for (const auto& r : d.ripples) {
            const double z = std::max(4.0, r.along - d.along);
            const double sy = std::min(double(height) - 8.0, horizon + f * d.height / z);
            const double scale = std::min(f / z, 14.0);
            const double sx = cx + (r.lateral - d.lateral) * scale;
            const double rx = (3.0 + 20.0 * r.age) * scale, ry = rx * 0.26;
            const double fade = r.strength * std::max(0.0, 1.0 - r.age / 2.2);
            if (rx < 1.0 || fade <= 0.0) continue;
            const double band = 0.10 + 0.05 / (1.0 + r.age);
            const int x0 = std::max(0, int(sx - rx * 1.2)), x1 = std::min(int(width) - 1, int(sx + rx * 1.2));
            const int y0 = std::max(horizon + 1, int(sy - ry * 1.2)), y1 = std::min(int(height) - 1, int(sy + ry * 1.2));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const double q = std::hypot((x + 0.5 - sx) / rx, (y + 0.5 - sy) / ry);
                    double a = 0.0;
                    if (std::abs(q - 1.0) < band) a = (1.0 - std::abs(q - 1.0) / band) * 175.0 * fade;
                    if (r.age < 0.5 && q < 0.45) a = std::max(a, 200.0 * fade * (1.0 - r.age / 0.5) * (1.0 - q / 0.45));
                    if (a > 1.0) BossPainter::blend(px[size_t(y) * width + size_t(x)], 236, 246, 252, unsigned(a));
                }
        }
        // The island stands on the starboard edge, a third of the way up from the stern.
        const auto island = art.island.empty() ? nullptr : package->image(art.island);
        const double zi = d.length * 0.52 - d.along;
        if (island && zi > 3.0) {
            const double base_y = horizon + f * d.height / zi;
            const double sx = cx + (d.half_width * 0.86 - d.lateral) * f / zi;
            const double h = 34.0 * f / zi, w = h * island->width / island->height;
            const int x0 = std::max(0, int(sx - w / 2)), x1 = std::min(int(width), int(sx + w / 2));
            const int y0 = std::max(0, int(base_y - h)), y1 = std::min(int(height), int(base_y));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const unsigned u = std::min(island->width - 1u, unsigned((x - (sx - w / 2)) / w * island->width));
                    const unsigned v = std::min(island->height - 1u, unsigned((y - (base_y - h)) / h * island->height));
                    const uint8_t* p = island->rgba.data() + (size_t(v) * island->width + u) * 4u;
                    if (p[3] >= 128) px[size_t(y) * width + x] = 0xff000000u | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
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

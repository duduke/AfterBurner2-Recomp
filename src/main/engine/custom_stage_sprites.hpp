#pragma once

#include "engine/custom_stage_json.hpp"
#include "roms.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

// Writes external images into a cloned sprite ROM so every renderer (native
// raster, widescreen, filters, shaders) draws them as ordinary hardware sprites.
// The banks are almost entirely full, so frames are replaced in place at their
// original dimensions; the image is scaled to each frame and reduced to the
// fourteen drawable pens of an X-Board sprite palette.
namespace CustomStage {

struct SpriteFrame {
    uint32_t address{};
    unsigned width{}, height{}, bank{}, offset{}, pitch{};
};

namespace SpriteDetail {

inline std::array<uint8_t, 3> color_from_word(uint16_t word) {
    const unsigned r = ((word >> 12) & 1u) | ((word << 1) & 0x1eu);
    const unsigned g = ((word >> 13) & 1u) | ((word >> 3) & 0x1eu);
    const unsigned b = ((word >> 14) & 1u) | ((word >> 7) & 0x1eu);
    return {uint8_t(r * 255u / 31u), uint8_t(g * 255u / 31u), uint8_t(b * 255u / 31u)};
}

inline uint16_t word_from_color(const std::array<uint8_t, 3>& c) {
    return RGBColor{c[0], c[1], c[2]}.rgb555();
}

// Sub ROM record layout, as consumed by Sub $7B8..$9C8 and tools/assets/extract_sprites.py.
inline bool read_frame(const Roms& rom, uint32_t address, SpriteFrame& out) {
    if (address + 16u > rom.subcpu.length) return false;
    const auto word = [&](unsigned at) {
        return unsigned(rom.subcpu.rom[address + at]) << 8 | rom.subcpu.rom[address + at + 1u];
    };
    const unsigned width = word(0), last_row = word(2), bank_word = word(4), pitch = word(8);
    if (!width || width > 1024u || last_row >= 4096u || (bank_word & ~0xe00u) ||
        !pitch || pitch > 127u || width > pitch * 8u || word(12) || word(14))
        return false;
    out = {address, width, last_row + 1u, bank_word >> 9, word(6), pitch};
    return true;
}

inline const uint8_t* pixel(const DecodedImage& image, unsigned x, unsigned y) {
    return image.rgba.data() + (size_t(y) * image.width + x) * 4u;
}

// Area-averaged scale to the frame's box. Alpha decides coverage, so a sprite's
// silhouette stays crisp and transparent pixels never bleed color into it.
struct Scaled {
    unsigned width{}, height{};
    std::vector<std::array<uint8_t, 4>> pixels;
};

inline Scaled scale_to(const DecodedImage& image, unsigned width, unsigned height, bool stretch) {
    Scaled out{width, height, std::vector<std::array<uint8_t, 4>>(size_t(width) * height, {0, 0, 0, 0})};
    double draw_width = width, draw_height = height;
    if (!stretch) {
        const double scale = std::min(double(width) / image.width, double(height) / image.height);
        draw_width = std::max(1.0, image.width * scale);
        draw_height = std::max(1.0, image.height * scale);
    }
    const double left = (width - draw_width) / 2.0, top = (height - draw_height) / 2.0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const double u0 = (x - left) / draw_width * image.width, u1 = (x + 1 - left) / draw_width * image.width;
            const double v0 = (y - top) / draw_height * image.height, v1 = (y + 1 - top) / draw_height * image.height;
            if (u1 <= 0 || v1 <= 0 || u0 >= image.width || v0 >= image.height) continue;
            const unsigned x0 = unsigned(std::max(0.0, u0)), y0 = unsigned(std::max(0.0, v0));
            const unsigned x1 = std::min(image.width, std::max(x0 + 1u, unsigned(std::ceil(std::min<double>(u1, image.width)))));
            const unsigned y1 = std::min(image.height, std::max(y0 + 1u, unsigned(std::ceil(std::min<double>(v1, image.height)))));
            uint64_t alpha{}, count{};
            std::array<uint64_t, 3> sum{};
            for (unsigned sy = y0; sy < y1; ++sy) {
                for (unsigned sx = x0; sx < x1; ++sx) {
                    const auto* p = pixel(image, sx, sy);
                    alpha += p[3];
                    for (unsigned c = 0; c < 3; ++c) sum[c] += uint64_t(p[c]) * p[3];
                    ++count;
                }
            }
            if (!alpha) continue;
            auto& dst = out.pixels[size_t(y) * width + x];
            for (unsigned c = 0; c < 3; ++c) dst[c] = uint8_t(sum[c] / alpha);
            dst[3] = uint8_t(alpha / count);
        }
    }
    return out;
}

// Median cut over the image's opaque pixels, for stages that bring their own colors.
inline std::vector<std::array<uint8_t, 3>> median_cut(const DecodedImage& image, unsigned count) {
    std::vector<std::array<uint8_t, 3>> samples;
    samples.reserve(size_t(image.width) * image.height);
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x) {
            const auto* p = pixel(image, x, y);
            if (p[3] >= 128) samples.push_back({p[0], p[1], p[2]});
        }
    if (samples.empty()) return {{0, 0, 0}};
    using Iterator = std::vector<std::array<uint8_t, 3>>::iterator;
    struct Box { Iterator begin, end; };
    std::vector<Box> boxes{{samples.begin(), samples.end()}};
    const auto spread = [](const Box& box, unsigned& channel) {
        unsigned best = 0;
        for (unsigned c = 0; c < 3; ++c) {
            uint8_t low = 255, high = 0;
            for (auto it = box.begin; it != box.end; ++it) {
                low = std::min(low, (*it)[c]);
                high = std::max(high, (*it)[c]);
            }
            if (unsigned(high - low) >= best) { best = high - low; channel = c; }
        }
        return best;
    };
    while (boxes.size() < count) {
        size_t chosen = boxes.size();
        unsigned chosen_channel = 0, widest = 0;
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2) continue;
            unsigned channel = 0;
            const auto width = spread(boxes[i], channel);
            if (width > widest || chosen == boxes.size()) { widest = width; chosen = i; chosen_channel = channel; }
        }
        if (chosen == boxes.size() || !widest) break;
        auto box = boxes[chosen];
        std::sort(box.begin, box.end, [&](const auto& a, const auto& b) { return a[chosen_channel] < b[chosen_channel]; });
        const auto middle = box.begin + (box.end - box.begin) / 2;
        boxes[chosen] = {box.begin, middle};
        boxes.push_back({middle, box.end});
    }
    std::vector<std::array<uint8_t, 3>> colors;
    for (const auto& box : boxes) {
        std::array<uint64_t, 3> sum{};
        for (auto it = box.begin; it != box.end; ++it)
            for (unsigned c = 0; c < 3; ++c) sum[c] += (*it)[c];
        const auto size = uint64_t(box.end - box.begin);
        colors.push_back({uint8_t(sum[0] / size), uint8_t(sum[1] / size), uint8_t(sum[2] / size)});
    }
    return colors;
}

inline void write_row_word(Roms& rom, const SpriteFrame& frame, unsigned word_index, uint32_t value) {
    const size_t offset = size_t(frame.bank) * 0x40000u + (word_index & 0xffffu) * 4u;
    if (offset + 4u > rom.sprites.length) return;
    rom.sprites.rom[offset] = uint8_t(value);
    rom.sprites.rom[offset + 1u] = uint8_t(value >> 8);
    rom.sprites.rom[offset + 2u] = uint8_t(value >> 16);
    rom.sprites.rom[offset + 3u] = uint8_t(value >> 24);
}

// Row format, observed across all 2,931 frame records: padding words left of the
// art are 0x0F000000 and right of it 0x000000F0. Pen 15 in those positions is the
// terminator the scaler stops on when reading the row backwards and forwards.
// Word 0 and the final word always carry a terminator, so those pen positions can
// never hold art. `pens` holds width*height values, 0 = transparent.
inline void write_frame(Roms& rom, const SpriteFrame& frame, std::span<const uint8_t> pens) {
    constexpr uint32_t left_pad = 0x0f000000u, right_pad = 0x000000f0u;
    for (unsigned y = 0; y < frame.height; ++y) {
        const unsigned row = frame.offset + y * frame.pitch;
        std::vector<uint32_t> words(frame.pitch, 0u);
        int first = -1, last = -1;
        for (unsigned w = 0; w < frame.pitch; ++w) {
            uint32_t value = 0;
            for (unsigned k = 0; k < 8u; ++k) {
                const unsigned x = w * 8u + k;
                if (x >= frame.width) break;
                if ((w == 0u && k == 1u) || (w + 1u == frame.pitch && k == 6u)) continue;
                value |= uint32_t(pens[size_t(y) * frame.width + x] & 15u) << ((7u - k) * 4u);
            }
            words[w] = value;
            if (value) { if (first < 0) first = int(w); last = int(w); }
        }
        if (first < 0) {
            // Empty row: every word terminates in both directions.
            for (unsigned w = 0; w < frame.pitch; ++w) {
                const uint32_t value = (w + 1u < frame.pitch ? left_pad : 0u) | (w ? right_pad : 0u);
                write_row_word(rom, frame, row + w, value);
            }
            continue;
        }
        for (unsigned w = 0; w < frame.pitch; ++w) {
            uint32_t value = words[w];
            if (int(w) < first || w == 0u) value |= left_pad;
            if (int(w) > last || w + 1u == frame.pitch) value |= right_pad;
            write_row_word(rom, frame, row + w, value);
        }
    }
}

} // namespace SpriteDetail

// Authoring aid: names the frame records the game is drawing, so an author can
// see what is on screen in a stage and which frames to replace.
class SpriteFrameLog {
public:
    struct Seen { uint32_t frame{}; unsigned palette{}, width{}, height{}; };

    // Returns frame/palette pairs not reported before.
    std::vector<Seen> observe(std::span<const uint8_t> descriptors, const Roms& rom) {
        if (frames.empty())
            for (uint32_t address = 0x10000u; address < 0x20000u; address += 16u) {
                SpriteFrame frame;
                if (SpriteDetail::read_frame(rom, address, frame)) frames.push_back(frame);
            }
        std::vector<Seen> fresh;
        for (size_t slot = 0; slot + 16u <= descriptors.size(); slot += 16u) {
            std::array<uint16_t, 8> d{};
            for (unsigned i = 0; i < 8u; ++i)
                d[i] = uint16_t(descriptors[slot + i * 2u]) << 8 | descriptors[slot + i * 2u + 1u];
            if (d[0] & 0x8000u) break;
            if (d[0] & 0x5000u) continue;
            const unsigned bank = (d[0] >> 9) & 7u, palette = d[6] & 255u;
            const unsigned pitch = unsigned(std::abs(int16_t((d[2] >> 1) | ((d[4] & 0x1000u) << 3)) >> 8));
            const uint64_t key = uint64_t(bank) << 40 | uint64_t(pitch) << 32 | uint64_t(palette) << 16 | d[1];
            if (!checked.insert(key).second) continue;
            const SpriteFrame* best = nullptr;
            for (const auto& frame : frames)
                if (frame.bank == bank && frame.pitch == pitch && d[1] >= frame.offset &&
                    d[1] < frame.offset + frame.pitch * frame.height &&
                    (!best || frame.height < best->height))
                    best = &frame;
            if (best && reported.insert(uint64_t(best->address) << 8 | palette).second)
                fresh.push_back({best->address, palette, best->width, best->height});
        }
        return fresh;
    }

private:
    std::vector<SpriteFrame> frames;
    std::set<uint64_t> checked, reported;
};

class SpriteOverrides {
public:
    using PaletteWords = std::array<uint16_t, 16>;
    // Live hardware palette access, supplied by the session at first sight.
    struct PaletteAccess {
        std::function<PaletteWords(unsigned)> read;
        std::function<void(unsigned, const PaletteWords&)> write;
    };

    // Master sprite palette table in Main ROM, copied to palette RAM by $13B1C.
    static constexpr uint32_t master_palette_table = 0x2edf4u;
    static constexpr unsigned master_palette_count = 232u;

    void prepare(Roms& rom, const std::vector<SpriteOverride>& overrides) {
        entries.clear();
        for (const auto& source : overrides) {
            if (!source.image)
                throw std::runtime_error("Sprite override has no decoded image: " + source.asset);
            Entry entry;
            entry.source = source;
            SpriteFrame root;
            if (!SpriteDetail::read_frame(rom, source.frame, root))
                throw std::runtime_error("Not a sprite frame record: " + source.asset);
            entry.family.push_back(root);
            // Sub $9E4's level-of-detail table steps through four smaller records.
            // Stop at anything that is not a smaller frame: the next family begins.
            if (source.lods) {
                for (unsigned lod = 1; lod < 5u; ++lod) {
                    SpriteFrame next;
                    const auto& previous = entry.family.back();
                    if (!SpriteDetail::read_frame(rom, source.frame + lod * 16u, next) ||
                        next.width > previous.width || next.height > previous.height)
                        break;
                    entry.family.push_back(next);
                }
            }
            // Blank until the palette is known. A frame shown before then is
            // briefly invisible instead of flashing the original artwork.
            for (const auto& frame : entry.family) {
                const std::vector<uint8_t> blank(size_t(frame.width) * frame.height, 0u);
                SpriteDetail::write_frame(rom, frame, blank);
            }
            entries.push_back(std::move(entry));
        }
    }

    bool pending() const {
        return std::any_of(entries.begin(), entries.end(), [](const Entry& e) { return !e.resolved; });
    }

    // Scans the published sprite list. A frame is rewritten the first time the
    // game draws it, because that request is what names its palette. Returns true
    // when sprite pixels changed, so the renderer's decoded copy can be refreshed.
    bool observe(std::span<const uint8_t> descriptors, Roms& rom, const PaletteAccess& palette) {
        if (!pending()) return false;
        bool changed = false;
        // A named master palette needs no draw request to be known.
        for (auto& entry : entries) {
            if (!entry.resolved && entry.source.palette && *entry.source.palette < master_palette_count) {
                resolve(entry, *entry.source.palette, rom, palette);
                changed = true;
            }
        }
        for (size_t slot = 0; slot + 16u <= descriptors.size() && pending(); slot += 16u) {
            std::array<uint16_t, 8> d{};
            for (unsigned i = 0; i < 8u; ++i)
                d[i] = uint16_t(descriptors[slot + i * 2u]) << 8 | descriptors[slot + i * 2u + 1u];
            if (d[0] & 0x8000u) break;
            if (d[0] & 0x5000u) continue;
            const unsigned bank = (d[0] >> 9) & 7u;
            const unsigned pitch = unsigned(std::abs(int16_t((d[2] >> 1) | ((d[4] & 0x1000u) << 3)) >> 8));
            for (auto& entry : entries) {
                if (entry.resolved) continue;
                for (const auto& frame : entry.family) {
                    if (frame.bank == bank && frame.pitch == pitch && d[1] >= frame.offset &&
                        d[1] < frame.offset + frame.pitch * frame.height) {
                        resolve(entry, entry.source.palette.value_or(uint8_t(d[6] & 255u)), rom, palette);
                        changed = true;
                        break;
                    }
                }
            }
        }
        return changed;
    }

private:
    struct Entry {
        SpriteOverride source;
        std::vector<SpriteFrame> family;
        bool resolved{};
    };
    std::vector<Entry> entries;

    static void patch_master_palette(Roms& rom, unsigned index, const PaletteWords& words) {
        if (index >= master_palette_count) return;
        const uint32_t base = master_palette_table + index * 32u;
        if (base + 32u > rom.maincpu.length) return;
        for (unsigned i = 0; i < 16u; ++i) {
            rom.maincpu.rom[base + i * 2u] = uint8_t(words[i] >> 8);
            rom.maincpu.rom[base + i * 2u + 1u] = uint8_t(words[i]);
        }
    }

    static PaletteWords master_palette(const Roms& rom, unsigned index) {
        PaletteWords words{};
        const uint32_t base = master_palette_table + index * 32u;
        for (unsigned i = 0; i < 16u; ++i)
            words[i] = uint16_t(rom.maincpu.rom[base + i * 2u]) << 8 | rom.maincpu.rom[base + i * 2u + 1u];
        return words;
    }

    static void resolve(Entry& entry, unsigned palette_index, Roms& rom, const PaletteAccess& palette) {
        entry.resolved = true;
        // The master table is immune to fades in progress; the palettes above it
        // are loaded per stage and only exist in palette RAM.
        auto words = palette_index < master_palette_count ? master_palette(rom, palette_index) : palette.read(palette_index);
        std::vector<std::array<uint8_t, 3>> colors;
        std::vector<uint8_t> pens;
        if (entry.source.own_colors) {
            colors = SpriteDetail::median_cut(*entry.source.image, 14u);
            for (size_t i = 0; i < colors.size(); ++i) {
                words[i + 1u] = SpriteDetail::word_from_color(colors[i]);
                pens.push_back(uint8_t(i + 1u));
            }
            patch_master_palette(rom, palette_index, words);
            palette.write(palette_index, words);
        } else {
            for (unsigned pen = 1; pen <= 14u; ++pen) {
                colors.push_back(SpriteDetail::color_from_word(words[pen]));
                pens.push_back(uint8_t(pen));
            }
        }
        std::vector<uint8_t> out;
        for (const auto& frame : entry.family) {
            const auto scaled = SpriteDetail::scale_to(*entry.source.image, frame.width, frame.height, entry.source.stretch);
            out.assign(size_t(frame.width) * frame.height, 0u);
            for (size_t i = 0; i < out.size(); ++i) {
                const auto& p = scaled.pixels[i];
                if (p[3] < 128) continue;
                unsigned best = 0;
                long best_distance = std::numeric_limits<long>::max();
                for (size_t c = 0; c < colors.size(); ++c) {
                    const long dr = long(p[0]) - colors[c][0], dg = long(p[1]) - colors[c][1], db = long(p[2]) - colors[c][2];
                    const long distance = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
                    if (distance < best_distance) { best_distance = distance; best = pens[c]; }
                }
                out[i] = uint8_t(best);
            }
            SpriteDetail::write_frame(rom, frame, out);
        }
    }
};

} // namespace CustomStage

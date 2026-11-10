#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CustomStage {

// Strict JSON parser for custom-stage definitions. Rejects duplicate keys,
// trailing commas, comments, leading zeros, non-finite numbers and invalid UTF-8.
struct Json {
    enum class Kind { null_value, boolean, number, string, array, object };
    Kind kind = Kind::null_value;
    bool boolean{};
    double number{};
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    const Json* find(const std::string& key) const {
        const auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    void keys(std::initializer_list<std::string_view> allowed) const {
        if (kind != Kind::object)
            throw std::runtime_error("Expected JSON object");
        for (const auto& [key, value] : object) {
            bool known = false;
            for (const auto candidate : allowed)
                known |= (candidate == key);
            if (!known)
                throw std::runtime_error("Unknown JSON field: " + key);
        }
    }

    std::string text() const {
        if (kind != Kind::string)
            throw std::runtime_error("Expected JSON string");
        return string;
    }

    double numeric() const {
        if (kind != Kind::number)
            throw std::runtime_error("Expected JSON number");
        return number;
    }

    int integer(int low, int high) const {
        const double value = numeric();
        const double rounded = std::trunc(value);
        if (value != rounded || rounded < low || rounded > high)
            throw std::runtime_error("JSON integer out of range");
        return static_cast<int>(rounded);
    }

    bool flag() const {
        if (kind != Kind::boolean)
            throw std::runtime_error("Expected JSON boolean");
        return boolean;
    }

    static Json parse(std::string_view source) {
        struct Parser {
            std::string_view s;
            size_t p{};

            [[noreturn]] void error() const {
                throw std::runtime_error("Invalid JSON at byte " + std::to_string(p));
            }

            void ws() {
                while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
                    ++p;
            }

            bool take(char c) {
                if (p < s.size() && s[p] == c) {
                    ++p;
                    return true;
                }
                return false;
            }

            unsigned hex() {
                unsigned v = 0;
                for (int i = 0; i < 4; ++i) {
                    if (p == s.size()) error();
                    const char c = s[p++];
                    const unsigned n = (c >= '0' && c <= '9') ? unsigned(c - '0') :
                                       (c >= 'a' && c <= 'f') ? unsigned(c - 'a' + 10) :
                                       (c >= 'A' && c <= 'F') ? unsigned(c - 'A' + 10) : 16u;
                    if (n == 16u) error();
                    v = v * 16u + n;
                }
                return v;
            }

            std::string str() {
                if (!take('"')) error();
                std::string out;
                while (p < s.size()) {
                    const unsigned char c = static_cast<unsigned char>(s[p++]);
                    if (c == '"') return out;
                    if (c < 32) error();
                    if (c != '\\') {
                        if (c < 128) {
                            out += static_cast<char>(c);
                            continue;
                        }
                        const unsigned count = (c >= 0xc2 && c <= 0xdf) ? 1u :
                                               (c >= 0xe0 && c <= 0xef) ? 2u :
                                               (c >= 0xf0 && c <= 0xf4) ? 3u : 0u;
                        if (!count || p + count > s.size()) error();
                        unsigned code = c & ((1u << (6u - count)) - 1u);
                        std::string bytes(1, static_cast<char>(c));
                        for (unsigned n = 0; n < count; ++n) {
                            const unsigned char next = static_cast<unsigned char>(s[p++]);
                            if ((next & 0xc0u) != 0x80u) error();
                            code = code * 64u + (next & 63u);
                            bytes += static_cast<char>(next);
                        }
                        if (code < (count == 1u ? 0x80u : count == 2u ? 0x800u : 0x10000u) ||
                            code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu))
                            error();
                        out += bytes;
                        continue;
                    }
                    if (p == s.size()) error();
                    const char e = s[p++];
                    switch (e) {
                    case '"': case '\\': case '/': out += e; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned code = hex();
                        if (code >= 0xd800u && code <= 0xdbffu) {
                            if (!take('\\') || !take('u')) error();
                            const unsigned low = hex();
                            if (low < 0xdc00u || low > 0xdfffu) error();
                            code = 0x10000u + (code - 0xd800u) * 1024u + (low - 0xdc00u);
                        } else if (code >= 0xdc00u && code <= 0xdfffu) {
                            error();
                        }
                        if (code < 128u) {
                            out += static_cast<char>(code);
                        } else if (code < 2048u) {
                            out += static_cast<char>(0xc0u | (code >> 6));
                            out += static_cast<char>(0x80u | (code & 63u));
                        } else if (code < 65536u) {
                            out += static_cast<char>(0xe0u | (code >> 12));
                            out += static_cast<char>(0x80u | ((code >> 6) & 63u));
                            out += static_cast<char>(0x80u | (code & 63u));
                        } else {
                            out += static_cast<char>(0xf0u | (code >> 18));
                            out += static_cast<char>(0x80u | ((code >> 12) & 63u));
                            out += static_cast<char>(0x80u | ((code >> 6) & 63u));
                            out += static_cast<char>(0x80u | (code & 63u));
                        }
                        break;
                    }
                    default:
                        error();
                    }
                }
                error();
            }

            Json value(unsigned depth = 0) {
                if (depth > 64u)
                    throw std::runtime_error("JSON nesting exceeds parser safety limit");
                ws();
                if (p == s.size()) error();
                Json v;
                if (s[p] == '"') {
                    v.kind = Kind::string;
                    v.string = str();
                    return v;
                }
                if (take('{')) {
                    v.kind = Kind::object;
                    ws();
                    if (take('}')) return v;
                    do {
                        ws();
                        auto key = str();
                        ws();
                        if (!take(':')) error();
                        auto child = value(depth + 1u);
                        if (!v.object.emplace(std::move(key), std::move(child)).second)
                            throw std::runtime_error("Duplicate JSON field");
                        ws();
                        if (take('}')) return v;
                    } while (take(','));
                    error();
                }
                if (take('[')) {
                    v.kind = Kind::array;
                    ws();
                    if (take(']')) return v;
                    do {
                        v.array.push_back(value(depth + 1u));
                        ws();
                        if (take(']')) return v;
                    } while (take(','));
                    error();
                }
                for (const auto literal : {std::string_view("true"), std::string_view("false"), std::string_view("null")}) {
                    if (s.substr(p, literal.size()) == literal) {
                        p += literal.size();
                        v.kind = literal == "null" ? Kind::null_value : Kind::boolean;
                        v.boolean = (literal == "true");
                        return v;
                    }
                }
                const size_t begin = p;
                take('-');
                if (!take('0')) {
                    if (p == s.size() || s[p] < '1' || s[p] > '9') error();
                    while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
                }
                auto digits = [&] {
                    const size_t start = p;
                    while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
                    if (p == start) error();
                };
                if (take('.')) digits();
                if (take('e') || take('E')) {
                    if (!take('+')) take('-');
                    digits();
                }
                v.kind = Kind::number;
                const auto result = std::from_chars(s.data() + begin, s.data() + p, v.number);
                if (result.ec != std::errc{} || result.ptr != s.data() + p || !std::isfinite(v.number))
                    error();
                return v;
            }
        } parser{source};
        auto root = parser.value();
        parser.ws();
        if (parser.p != source.size()) parser.error();
        return root;
    }

    static Json load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open custom stage JSON: " + path);
        return parse(std::string(std::istreambuf_iterator<char>(in), {}));
    }
};

struct RGBColor {
    uint8_t r{}, g{}, b{};
    constexpr bool operator==(const RGBColor&) const = default;
    // SEGA X-Board 16-bit palette word: sBGR BBBB GGGG RRRR (bit 12 = R0,
    // bit 13 = G0, bit 14 = B0, bits 8..11 = B4..B1, bits 4..7 = G4..G1,
    // bits 0..3 = R4..R1), matching HWPalette::write_changed and OMainPalette::blend.
    constexpr uint16_t rgb555() const {
        const uint16_t r5 = (uint16_t(r) >> 3) & 0x1fu;
        const uint16_t g5 = (uint16_t(g) >> 3) & 0x1fu;
        const uint16_t b5 = (uint16_t(b) >> 3) & 0x1fu;
        return uint16_t(((r5 & 1u) << 12) |
                        ((g5 & 1u) << 13) |
                        ((b5 & 1u) << 14) |
                        ((b5 >> 1) << 8) |
                        ((g5 >> 1) << 4) |
                        (r5 >> 1));
    }
};

struct OceanPalette {
    RGBColor sky{0, 170, 255};
    RGBColor haze{220, 250, 255};
    RGBColor water{0, 0, 255};
    std::optional<RGBColor> water_band;   // Second ground color, for scrolling bands.

    RGBColor sky_at(double t) const {
        t = std::clamp(t, 0.0, 1.0);
        if (t <= 0.82) return sky;
        const double blend = (t - 0.82) / 0.18;
        return {
            uint8_t(std::lround(sky.r + (haze.r - sky.r) * blend)),
            uint8_t(std::lround(sky.g + (haze.g - sky.g) * blend)),
            uint8_t(std::lround(sky.b + (haze.b - sky.b) * blend)),
        };
    }

    RGBColor sea_at(double t) const {
        t = std::clamp(t, 0.0, 1.0);
        if (t >= 0.15) return water;
        const double blend = t / 0.15;
        return {
            uint8_t(std::lround(haze.r + (water.r - haze.r) * blend)),
            uint8_t(std::lround(haze.g + (water.g - haze.g) * blend)),
            uint8_t(std::lround(haze.b + (water.b - haze.b) * blend)),
        };
    }
};

// Decoded external image, top-down RGBA8. Produced by the frontend so the engine
// stays free of SDL and shared between definition copies on stage retries.
struct DecodedImage {
    unsigned width{}, height{};
    std::vector<uint8_t> rgba;
};

struct LayerElement {
    enum class Type { rectangle, image };
    Type type = Type::rectangle;
    std::string asset;
    double x{}, y{}, z{};
    double width{32.0}, height{32.0}, rotation{};
    unsigned columns{1}, rows{1};
    std::array<uint8_t, 4> color{255, 255, 255, 255};
    bool nearest{};                         // Pixel-art sampling instead of smooth.
    std::optional<uint16_t> tick_begin, tick_end; // Stage-timer visibility window.
};

// External image shown wherever the stage's sky would be, behind every sprite.
struct Backdrop {
    std::string asset;
    double parallax_x{0.25}, parallax_y{0.5};
    double drift_x{}, drift_y{};            // Constant scrolling, native pixels per second.
    bool follow_roll{true};                 // Tilt with the aircraft like the horizon does.
};

// A 16x16 grid of ground objects, in the format $83A8 reads through commands 5..8 of
// the stream at descriptor+8. One cell spawns per $40 of distance flown, left to right
// and top to bottom, so a grid lasts about 992 ticks; its last row then repeats until
// the next pattern. The grid's column is the object's lateral position.
struct ScenePattern {
    uint16_t tick{};
    uint8_t command{8};                     // 5..8 select how $841C flips the cell index.
    std::array<uint8_t, 144> data{};        // 128 bytes of nibbles, then a 16-entry object table.
};

// Replaces a ROM sprite frame (and its smaller LOD frames) with an external image.
struct SpriteOverride {
    uint32_t frame{};                       // Sub CPU frame record, e.g. 0x16500.
    std::string asset;
    std::optional<uint8_t> palette;         // Empty = use the palette the game draws it with.
    bool own_colors{};                      // Rewrite that palette with the image's colors.
    bool lods{true};                        // Also rewrite the four smaller LOD frames.
    bool stretch{};                         // Fill the frame instead of keeping aspect ratio.
    std::shared_ptr<const DecodedImage> image;
};

// Overwrites part of a cloned graphics or sample ROM region with external bytes.
struct RomPatch {
    enum class Region { tiles, sprites, road, pcm };
    Region region = Region::road;
    uint32_t offset{};
    std::string asset;
    std::shared_ptr<const std::vector<uint8_t>> data;
};

// One color word in a stage's palette blocks. "gradient" is the 64-byte
// sky/ground block ($140DA: words 1..14 initial, 17..30 settled; the last
// few are the ground), "road" the road/runway colors ($141F2). The other
// three blocks are reachable by their pointer table address.
struct PalettePatch {
    static constexpr uint32_t primary = 1u, alternate = 2u; // descriptor blocks
    uint32_t table{};
    unsigned word{};
    RGBColor color{};
};

// A native boss: a large aircraft drawn by the frontend from pack images and
// fought with the player's own guns and missiles (custom_stage_boss.hpp).
// Positions and sizes are in 320x224 screen pixels at full size.
struct BossPart {
    enum class Kind { pod, turret, core };
    Kind kind = Kind::pod;
    std::string name, asset, broken_asset;
    double x{}, y{}, width{}, height{};
    int hp{100};
};

struct BossDefinition {
    std::string name = "BOSS";
    std::string hull_asset;
    double width{300}, height{110};
    std::vector<BossPart> parts;
    unsigned appear_tick{4000}, hold_tick{6000};
    double time_limit{150};
    unsigned bonus{100000};
};

struct SceneLayer {
    std::string name;
    double order{};
    enum class Space { screen, world };
    Space space = Space::screen;
    std::vector<LayerElement> elements;
};

// Native 8-byte X-Board timed stream record ($DE68..$DE8C at descriptor+16, or
// $8122..$82FA enemy wave stream at descriptor+8).
// Bytes 0..1: 16-bit stage timer tick (compared against $29C006 >> 4).
// Byte 2: command index.
//   For stage control events ($DE68 -> JSR $DE90(PC,D0.W)):
//     0  = music_transition    ($DE90 -> $DEE4: JMP $DEEC(PC,D0.W), word 4(A0) in {0,4,8,12})
//     1  = clear_horizon_flags ($DE94 -> $DF64: BCLR #0,$29C201; BCLR #0,$FFFF8D36)
//     2  = UNPORTED / UNUSED   ($DE98: not used by canonical ROM; rejected)
//     3  = clear_horizon_flags_alt ($DE9C -> $DF64: BCLR #0,$29C201; BCLR #0,$FFFF8D36)
//     4  = set_horizon_flag    ($DEA0 -> $DF74: BSET #0,$29C201)
//     5  = nop_5               ($DEA4 -> $DF94: RTS)
//     6  = nop_6               ($DEA8 -> $DF96: RTS)
//     7  = set_horizon_target  ($DEAC -> $DF98: MOVE.W 4(A0),$29C07C)
//     8  = set_ground_flag     ($DEB0 -> $DFA2: BSET #1,$FFFF8B6F)
//     9  = clear_ground_flag   ($DEB4 -> $DFAA: BCLR #1,$FFFF8B6F)
//     10 = set_scroll_state    ($DEB8 -> $DFB2: MOVE.B 3(A0),$29C0A1)
//     11 = set_cloud_control   ($DEBC -> $DFBC: MOVE.B 3(A0),$FFFF8D6A)
//     12 = stage_advance       ($DEC0 -> $DFC4: CLR.L $29C006; ADDQ.W #1,$29C060; ...)
//     13 = set_camera_bounds   ($DEC4 -> $E022: 3(A0)->$29C0EF, 4(A0)->$29C0E6, 6(A0)->$29C0E8)
//     14 = refuel              ($DEC8 -> $E03C: CLR.W $29C0CC)
//     15 = set_stage_mode      ($DECC -> $E044: MOVE.W 4(A0),$29C0CC)
//     16 = special_road_exit   ($DED0 -> $E062: MOVE.W #$0402,$29C320; BSET #5,$29C200)
//     17 = special_road_enter  ($DED4 -> $E074: MOVE.W #$0480,$29C320; BCLR #5,$29C200)
//     18 = preload_next_stage  ($DED8 -> $E086: reserved for boundary palette installation)
//     19 = set_bank_drift_flag ($DEDC -> $E0A6: MOVE.B 3(A0),$FFFF8D7E)
//     20 = set_horizon_scroll  ($DEE0 -> $DF7E: BSET #0,$29C201; BSET #0,$FFFF8D36; MOVE.W 4(A0),$FFFF8D38)
// Bytes 3..7: command-specific parameters (3(A0)..7(A0)).
struct EventRecord {
    uint16_t tick{};
    uint8_t command{};
    std::array<uint8_t, 5> params{};

    constexpr std::array<uint8_t, 8> encode() const {
        return {
            uint8_t(tick >> 8), uint8_t(tick & 0xffu),
            command,
            params[0], params[1], params[2], params[3], params[4]
        };
    }

    constexpr uint16_t word4() const {
        return static_cast<uint16_t>((uint16_t(params[1]) << 8) | uint16_t(params[2]));
    }

    constexpr uint16_t word6() const {
        return static_cast<uint16_t>((uint16_t(params[3]) << 8) | uint16_t(params[4]));
    }

    void validate_control() const {
        if (tick == 0xffffu)
            throw std::runtime_error("Event tick 0xFFFF is reserved for the stream sentinel");
        if (command == 2u)
            throw std::runtime_error("Stage control command 2 ($DE98) is unused in canonical ROM and unported");
        if (command == 18u)
            throw std::runtime_error("Event command 18 is reserved for boundary palette installation");
        if (command > 20u)
            throw std::runtime_error("Stage control command out of $DE90 jump table range (0..20)");
        if (command == 0u) {
            const uint16_t sub = word4();
            if (sub != 0u && sub != 4u && sub != 8u && sub != 12u)
                throw std::runtime_error("music_transition (command 0) requires word 4(A0) in {0, 4, 8, 12}");
        }
    }

    void validate_enemy_wave() const {
        if (tick == 0xffffu)
            throw std::runtime_error("Enemy wave tick 0xFFFF is reserved for the stream sentinel");
        if (command > 2u)
            throw std::runtime_error("Enemy wave command out of supported $815C range (0..2)");
    }

    static uint8_t parse_control_command(const std::string& c) {
        if (c == "music_transition") return 0u;
        if (c == "clear_horizon_flags") return 1u;
        if (c == "clear_horizon_flags_alt") return 3u;
        if (c == "set_horizon_flag") return 4u;
        if (c == "nop_5") return 5u;
        if (c == "nop_6") return 6u;
        if (c == "set_horizon_target") return 7u;
        if (c == "set_ground_flag") return 8u;
        if (c == "clear_ground_flag") return 9u;
        if (c == "set_scroll_state") return 10u;
        if (c == "set_cloud_control") return 11u;
        if (c == "stage_advance") return 12u;
        if (c == "set_camera_bounds") return 13u;
        if (c == "refuel") return 14u;
        if (c == "set_stage_mode" || c == "landing") return 15u;
        if (c == "special_road_exit") return 16u;
        if (c == "special_road_enter") return 17u;
        if (c == "set_bank_drift_flag") return 19u;
        if (c == "set_horizon_scroll") return 20u;
        throw std::runtime_error("Unknown stage control event command: " + c);
    }

    static std::string_view control_command_name(uint8_t cmd) {
        switch (cmd) {
        case 0u:  return "music_transition";
        case 1u:  return "clear_horizon_flags";
        case 3u:  return "clear_horizon_flags_alt";
        case 4u:  return "set_horizon_flag";
        case 5u:  return "nop_5";
        case 6u:  return "nop_6";
        case 7u:  return "set_horizon_target";
        case 8u:  return "set_ground_flag";
        case 9u:  return "clear_ground_flag";
        case 10u: return "set_scroll_state";
        case 11u: return "set_cloud_control";
        case 12u: return "stage_advance";
        case 13u: return "set_camera_bounds";
        case 14u: return "refuel";
        case 15u: return "set_stage_mode";
        case 16u: return "special_road_exit";
        case 17u: return "special_road_enter";
        case 18u: return "preload_next_stage";
        case 19u: return "set_bank_drift_flag";
        case 20u: return "set_horizon_scroll";
        default:  return "unknown";
        }
    }
};

struct Definition {
    enum class Objective { combat, landing, takeoff, refuel };
    enum class Environment { carrier, runway, jungle, sea, canyon, night };

    std::string name = "CARRIER / LANDING";
    Objective objective = Objective::landing;
    Environment environment = Environment::carrier;

    // Native ROM compilation targets:
    unsigned slot = 1;                        // 1-based stage slot in the ROM progression (1..22)
    unsigned source_stage = 0;                // 0 = auto-select donor stage from environment/objective
    std::optional<unsigned> enemy_stage;      // Optional donor stage (1..23) for descriptor+8 ($FFFF8A00 enemy stream)
    std::optional<unsigned> scenery_stage;    // Optional donor stage (1..23) for descriptor+12 ($FFFF8A04 scenery stream)
    std::optional<uint8_t> music_cmd;         // Optional override for $2B604 music command byte
    std::optional<uint16_t> road_setup;       // Optional override for $32196 road word
    std::optional<uint16_t> sky_setup;        // Optional override for $32324 setup word
    std::optional<uint16_t> curve_setup;      // Optional override for $32452 setup word
    bool replace_events = false;              // True = replace donor $DE68 events; False = splice
    std::vector<EventRecord> custom_events;
    bool replace_enemy_waves = false;         // True = replace donor $8122 enemy waves; False = splice
    std::vector<EventRecord> custom_enemy_waves;

    // Interactive flight/sequence parameters:
    double length = 300.0;
    double width = 70.0;
    double start_distance = 1200.0;
    double start_height = 76.0;
    double speed = 80.0;
    double slope = 0.06;
    double tanker_span = 50.0;
    bool seamless_takeoff = true;
    // Host-driven flare on the ROM airbase landing object ($1134A state 4): the
    // player's stick pitch offsets the ROM's per-frame +36(A5) descent.
    bool player_landing = false;
    // Interactive landings in this stage use carrier rules: a tight deck,
    // a wire window and bolters instead of long landings.
    bool carrier_landing = false;

    // Presentation & layer options:
    bool rom_environment = true;
    bool player_shadow = true;
    bool clouds = true;
    bool hud = true;
    bool custom_ocean = false;
    OceanPalette ocean{};
    unsigned tanker_variant = 0; // 0 = random cycle across {3, 9, 11, 16, 19, 21}
    std::vector<SceneLayer> layers;

    // External content (see custom_stage_assets.hpp):
    std::vector<std::string> asset_packs;     // Extra folders/ZIPs, relative to the stage file.
    std::optional<std::string> music_file;    // WAV/FLAC played instead of the stage's track.
    std::optional<Backdrop> backdrop;
    std::vector<SpriteOverride> sprites;
    std::vector<RomPatch> rom_patches;
    std::vector<PalettePatch> palette_patches;
    std::optional<BossDefinition> boss;
    std::vector<ScenePattern> scenery_patterns;

    // Layers ordered below zero render behind gameplay, inside the sky.
    bool has_sky_layers() const {
        return std::any_of(layers.begin(), layers.end(), [](const SceneLayer& l) { return l.order < 0.0; });
    }
    // The sky is replaced by keying it to a reserved palette color.
    bool sky_keyed() const { return backdrop.has_value() || has_sky_layers(); }

    // Resolve the canonical donor stage (1..23) used by the ROM-view compiler.
    unsigned donor_stage() const {
        if (source_stage) return source_stage;
        if (objective == Objective::refuel)
            return tanker_variant ? tanker_variant : 3u;
        switch (environment) {
        case Environment::carrier:
            return objective == Objective::landing ? 13u : 1u;
        case Environment::runway:
        case Environment::jungle:
            return 5u;
        case Environment::sea:
            return 3u;
        case Environment::canyon:
            return 8u;
        case Environment::night:
            return 4u;
        }
        return 1u;
    }

    // Default 1-based stage slot (1..22) when omitted from JSON or presets.
    // Stage 5 ($29C060 == 4) is checked explicitly by $012540/$013090 for the
    // land airbase runway; Stage 13 ($29C060 == 12) is the carrier landing slot;
    // Stage 8 ($29C060 == 7) is the canyon combat slot.
    unsigned default_slot() const {
        if (objective == Objective::refuel)
            return 3u;
        switch (environment) {
        case Environment::carrier:
            return objective == Objective::landing ? 13u : 1u;
        case Environment::runway:
        case Environment::jungle:
            return 5u;
        case Environment::sea:
            return 3u;
        case Environment::canyon:
            return 8u;
        case Environment::night:
            return 4u;
        }
        return 1u;
    }

    static std::vector<SceneLayer> parse_layers(const Json& list) {
        if (list.kind != Json::Kind::array)
            throw std::runtime_error("Expected JSON array for layers");
        std::vector<SceneLayer> out;
        out.reserve(list.array.size());
        for (const auto& item : list.array) {
            item.keys({"name", "order", "space", "elements"});
            const auto* elements_node = item.find("elements");
            if (!elements_node || elements_node->kind != Json::Kind::array)
                throw std::runtime_error("Layer requires an elements array");
            SceneLayer layer;
            if (const auto* n = item.find("name")) layer.name = n->text();
            if (const auto* o = item.find("order")) layer.order = o->numeric();
            if (const auto* sp = item.find("space")) {
                const auto s = sp->text();
                if (s == "screen") layer.space = SceneLayer::Space::screen;
                else if (s == "world") layer.space = SceneLayer::Space::world;
                else throw std::runtime_error("Invalid layer space: " + s);
            }
            for (const auto& elem_node : elements_node->array) {
                elem_node.keys({"type", "asset", "x", "y", "z", "width", "height", "rotation", "color", "repeat", "filter", "ticks"});
                const auto* type_node = elem_node.find("type");
                if (!type_node) throw std::runtime_error("Layer element requires type");
                const auto type_str = type_node->text();
                LayerElement elem;
                if (type_str == "rectangle") {
                    elem.type = LayerElement::Type::rectangle;
                    if (elem_node.find("asset"))
                        throw std::runtime_error("Rectangle element cannot specify asset");
                } else if (type_str == "image") {
                    elem.type = LayerElement::Type::image;
                    const auto* asset_node = elem_node.find("asset");
                    if (!asset_node || asset_node->text().empty())
                        throw std::runtime_error("Image element requires non-empty asset");
                    elem.asset = asset_node->text();
                } else {
                    throw std::runtime_error("Unsupported element type: " + type_str);
                }
                if (const auto* x = elem_node.find("x")) elem.x = x->numeric();
                if (const auto* y = elem_node.find("y")) elem.y = y->numeric();
                if (const auto* z = elem_node.find("z")) elem.z = z->numeric();
                if (const auto* w = elem_node.find("width")) {
                    elem.width = w->numeric();
                    if (elem.width <= 0.0) throw std::runtime_error("Element width must be positive");
                }
                if (const auto* h = elem_node.find("height")) {
                    elem.height = h->numeric();
                    if (elem.height <= 0.0) throw std::runtime_error("Element height must be positive");
                }
                if (const auto* r = elem_node.find("rotation")) elem.rotation = r->numeric();
                if (const auto* c = elem_node.find("color")) {
                    if (c->kind != Json::Kind::array || c->array.size() != 4u)
                        throw std::runtime_error("Element color requires 4 RGBA integers");
                    for (size_t i = 0; i < 4u; ++i)
                        elem.color[i] = static_cast<uint8_t>(c->array[i].integer(0, 255));
                }
                if (const auto* rep = elem_node.find("repeat")) {
                    if (rep->kind != Json::Kind::array || rep->array.size() != 2u)
                        throw std::runtime_error("Element repeat requires [columns, rows]");
                    elem.columns = static_cast<unsigned>(rep->array[0].integer(1, 16384));
                    elem.rows = static_cast<unsigned>(rep->array[1].integer(1, 16384));
                }
                if (const auto* f = elem_node.find("filter")) {
                    const auto mode = f->text();
                    if (mode == "nearest") elem.nearest = true;
                    else if (mode != "linear") throw std::runtime_error("Element filter must be nearest or linear");
                }
                if (const auto* t = elem_node.find("ticks")) {
                    if (t->kind != Json::Kind::array || t->array.size() != 2u)
                        throw std::runtime_error("Element ticks requires [begin, end]");
                    elem.tick_begin = static_cast<uint16_t>(t->array[0].integer(0, 0xffff));
                    elem.tick_end = static_cast<uint16_t>(t->array[1].integer(0, 0xffff));
                    if (*elem.tick_end < *elem.tick_begin)
                        throw std::runtime_error("Element ticks end precedes its begin");
                }
                layer.elements.push_back(std::move(elem));
            }
            out.push_back(std::move(layer));
        }
        std::stable_sort(out.begin(), out.end(), [](const SceneLayer& a, const SceneLayer& b) {
            return a.order < b.order;
        });
        return out;
    }

    // Accepts 0x16500, 91392 or the sprite extractor's "sub_016500" id.
    static uint32_t parse_frame_address(const Json& node) {
        uint32_t address{};
        if (node.kind == Json::Kind::number) {
            address = static_cast<uint32_t>(node.integer(0, 0x3ffff));
        } else {
            auto text = node.text();
            int base = 10;
            if (text.starts_with("sub_")) { text.erase(0, 4); base = 16; }
            else if (text.starts_with("0x") || text.starts_with("0X")) { text.erase(0, 2); base = 16; }
            const auto result = std::from_chars(text.data(), text.data() + text.size(), address, base);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || text.empty())
                throw std::runtime_error("Invalid sprite frame: " + node.string);
        }
        if (address < 0x10000u || address >= 0x20000u || address % 16u)
            throw std::runtime_error("Sprite frame must be a 16-byte record in Sub ROM $10000..$1FFFF");
        return address;
    }

    static uint8_t parse_byte(const Json& node) {
        if (node.kind == Json::Kind::number) return static_cast<uint8_t>(node.integer(0, 255));
        auto text = node.text();
        int base = 10;
        if (text.starts_with("0x") || text.starts_with("0X")) { text.erase(0, 2); base = 16; }
        unsigned value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || text.empty() || value > 255u)
            throw std::runtime_error("Expected a byte value: " + node.string);
        return static_cast<uint8_t>(value);
    }

    static unsigned pattern_cell(char c) {
        if (c == '.' || c == '0') return 0u;
        if (c >= '1' && c <= '9') return unsigned(c - '0');
        if (c >= 'a' && c <= 'f') return unsigned(c - 'a') + 10u;
        if (c >= 'A' && c <= 'F') return unsigned(c - 'A') + 10u;
        throw std::runtime_error(std::string("Scenery pattern cell must be '.' or 1..F, not: ") + c);
    }

    static ScenePattern parse_pattern(const Json& node) {
        node.keys({"tick", "rows", "objects", "flip"});
        const auto* tick = node.find("tick");
        const auto* rows = node.find("rows");
        const auto* objects = node.find("objects");
        if (!tick || !rows || !objects)
            throw std::runtime_error("Scenery pattern requires tick, rows and objects");
        if (rows->kind != Json::Kind::array || rows->array.empty() || rows->array.size() > 16u)
            throw std::runtime_error("Scenery pattern requires 1 to 16 rows");
        ScenePattern pattern;
        pattern.tick = static_cast<uint16_t>(tick->integer(0, 0xfffeu));
        if (const auto* flip = node.find("flip")) {
            const auto mode = flip->text();
            pattern.command = mode == "none" ? 8u : mode == "columns" ? 6u : mode == "rows" ? 7u :
                              mode == "both" ? 5u : 0u;
            if (!pattern.command) throw std::runtime_error("Scenery pattern flip must be none, columns, rows or both");
        }
        if (objects->kind != Json::Kind::object)
            throw std::runtime_error("Scenery pattern objects must be a JSON object");
        for (const auto& [key, value] : objects->object) {
            if (key.size() != 1u || !pattern_cell(key[0]))
                throw std::runtime_error("Scenery pattern object keys must be 1..F: " + key);
            pattern.data[128u + pattern_cell(key[0])] = parse_byte(value);
        }
        // Fewer than 16 rows means the last row continues, as it would at runtime anyway.
        for (unsigned row = 0; row < 16u; ++row) {
            const auto& text = rows->array[std::min<size_t>(row, rows->array.size() - 1u)].text();
            if (text.size() != 16u) throw std::runtime_error("Scenery pattern rows need 16 cells: " + text);
            for (unsigned column = 0; column < 16u; ++column) {
                const unsigned cell = pattern_cell(text[column]);
                if (cell && !pattern.data[128u + cell])
                    throw std::runtime_error(std::string("Scenery pattern uses a cell with no object: ") + text[column]);
                pattern.data[row * 8u + column / 2u] |= uint8_t(column % 2u ? cell : cell << 4);
            }
        }
        return pattern;
    }

    static void parse_external_content(const Json& doc, Definition& def) {
        if (const auto* patterns = doc.find("scenery_patterns")) {
            if (patterns->kind != Json::Kind::array)
                throw std::runtime_error("scenery_patterns must be a JSON array");
            for (const auto& node : patterns->array) def.scenery_patterns.push_back(parse_pattern(node));
        }
        if (const auto* packs = doc.find("asset_packs")) {
            if (packs->kind != Json::Kind::array)
                throw std::runtime_error("asset_packs must be a JSON array");
            for (const auto& pack : packs->array) {
                if (pack.text().empty()) throw std::runtime_error("asset_packs entries must not be empty");
                def.asset_packs.push_back(pack.text());
            }
        }
        if (const auto* music = doc.find("music_file")) {
            if (music->text().empty()) throw std::runtime_error("music_file must not be empty");
            def.music_file = music->text();
        }
        if (const auto* list = doc.find("sprites")) {
            if (list->kind != Json::Kind::array)
                throw std::runtime_error("sprites must be a JSON array");
            for (const auto& node : list->array) {
                node.keys({"frame", "asset", "palette", "own_colors", "lods", "fit"});
                const auto* frame = node.find("frame");
                const auto* asset = node.find("asset");
                if (!frame || !asset || asset->text().empty())
                    throw std::runtime_error("Sprite override requires frame and asset");
                SpriteOverride sprite;
                sprite.frame = parse_frame_address(*frame);
                sprite.asset = asset->text();
                if (const auto* p = node.find("palette")) {
                    if (p->kind == Json::Kind::string) {
                        if (p->text() != "auto") throw std::runtime_error("Sprite palette must be a number or \"auto\"");
                    } else {
                        sprite.palette = static_cast<uint8_t>(p->integer(0, 255));
                    }
                }
                if (const auto* o = node.find("own_colors")) sprite.own_colors = o->flag();
                if (const auto* l = node.find("lods")) sprite.lods = l->flag();
                if (const auto* f = node.find("fit")) {
                    const auto mode = f->text();
                    if (mode == "stretch") sprite.stretch = true;
                    else if (mode != "contain") throw std::runtime_error("Sprite fit must be contain or stretch");
                }
                def.sprites.push_back(std::move(sprite));
            }
        }
        if (const auto* node = doc.find("boss")) {
            node->keys({"name", "hull", "size", "parts", "appear_tick", "hold_tick", "time_limit", "bonus"});
            BossDefinition boss;
            if (const auto* n = node->find("name")) boss.name = n->text();
            const auto* hull = node->find("hull");
            if (!hull || hull->text().empty()) throw std::runtime_error("Boss requires a hull image");
            boss.hull_asset = hull->text();
            auto pair = [](const Json* v, const char* what) {
                if (!v || v->kind != Json::Kind::array || v->array.size() != 2u)
                    throw std::runtime_error(std::string("Boss ") + what + " requires two numbers");
                return std::pair{v->array[0].numeric(), v->array[1].numeric()};
            };
            if (const auto* size = node->find("size")) std::tie(boss.width, boss.height) = pair(size, "size");
            if (const auto* t = node->find("appear_tick")) boss.appear_tick = unsigned(t->integer(0, 65000));
            if (const auto* t = node->find("hold_tick")) boss.hold_tick = unsigned(t->integer(0, 65000));
            if (boss.hold_tick < boss.appear_tick) throw std::runtime_error("Boss hold_tick must not be before appear_tick");
            if (const auto* t = node->find("time_limit")) boss.time_limit = t->numeric();
            if (const auto* b = node->find("bonus")) boss.bonus = unsigned(b->integer(0, 9999990));
            const auto* parts = node->find("parts");
            if (!parts || parts->kind != Json::Kind::array || parts->array.empty())
                throw std::runtime_error("Boss requires a parts array");
            for (const auto& p : parts->array) {
                p.keys({"name", "kind", "asset", "broken", "at", "size", "hp"});
                BossPart part;
                if (const auto* n = p.find("name")) part.name = n->text();
                const auto* kind = p.find("kind");
                const auto k = kind ? kind->text() : std::string("pod");
                part.kind = k == "core" ? BossPart::Kind::core : k == "turret" ? BossPart::Kind::turret :
                            k == "pod" ? BossPart::Kind::pod : throw std::runtime_error("Boss part kind must be pod, turret or core");
                const auto* asset = p.find("asset");
                if (!asset || asset->text().empty()) throw std::runtime_error("Boss part requires an asset");
                part.asset = asset->text();
                if (const auto* b = p.find("broken")) part.broken_asset = b->text();
                std::tie(part.x, part.y) = pair(p.find("at"), "part position");
                std::tie(part.width, part.height) = pair(p.find("size"), "part size");
                if (const auto* hp = p.find("hp")) part.hp = int(hp->integer(1, 100000));
                boss.parts.push_back(std::move(part));
            }
            if (std::none_of(boss.parts.begin(), boss.parts.end(), [](const BossPart& p) { return p.kind == BossPart::Kind::core; }))
                throw std::runtime_error("Boss requires one core part");
            def.boss = std::move(boss);
        }
        if (const auto* list = doc.find("palette_patches")) {
            if (list->kind != Json::Kind::array)
                throw std::runtime_error("palette_patches must be a JSON array");
            for (const auto& node : list->array) {
                node.keys({"table", "word", "words", "color"});
                const auto* table = node.find("table");
                const auto* color = node.find("color");
                if (!table || !color)
                    throw std::runtime_error("Palette patch requires table and color");
                const auto name = table->text();
                const uint32_t base = name == "primary" ? PalettePatch::primary :
                                      name == "alternate" ? PalettePatch::alternate :
                                      name == "gradient" ? 0x140dau : name == "road" ? 0x141f2u :
                                      name == "1413a" ? 0x1413au : name == "14196" ? 0x14196u :
                                      name == "1424e" ? 0x1424eu : 0u;
                if (!base) throw std::runtime_error("Palette patch table must be primary, alternate, gradient, road, 1413a, 14196 or 1424e");
                const unsigned limit = base == PalettePatch::primary ? 4u : base == PalettePatch::alternate ? 12u :
                                       base == 0x140dau ? 32u : 128u;
                if (color->kind != Json::Kind::array || color->array.size() != 3u)
                    throw std::runtime_error("Palette patch color requires 3 RGB integers");
                RGBColor rgb{uint8_t(color->array[0].integer(0, 255)), uint8_t(color->array[1].integer(0, 255)),
                             uint8_t(color->array[2].integer(0, 255))};
                std::vector<unsigned> words;
                if (const auto* w = node.find("word")) words.push_back(unsigned(w->integer(0, limit - 1)));
                if (const auto* ws = node.find("words")) {
                    if (ws->kind != Json::Kind::array) throw std::runtime_error("Palette patch words must be an array");
                    for (const auto& w : ws->array) words.push_back(unsigned(w.integer(0, limit - 1)));
                }
                if (words.empty()) throw std::runtime_error("Palette patch requires word or words");
                for (const unsigned w : words) def.palette_patches.push_back({base, w, rgb});
            }
        }
        if (const auto* list = doc.find("rom_patches")) {
            if (list->kind != Json::Kind::array)
                throw std::runtime_error("rom_patches must be a JSON array");
            for (const auto& node : list->array) {
                node.keys({"region", "offset", "asset"});
                const auto* region = node.find("region");
                const auto* asset = node.find("asset");
                if (!region || !asset || asset->text().empty())
                    throw std::runtime_error("ROM patch requires region and asset");
                RomPatch patch;
                const auto name = region->text();
                if (name == "tiles") patch.region = RomPatch::Region::tiles;
                else if (name == "sprites") patch.region = RomPatch::Region::sprites;
                else if (name == "road") patch.region = RomPatch::Region::road;
                else if (name == "pcm") patch.region = RomPatch::Region::pcm;
                else throw std::runtime_error("ROM patch region must be tiles, sprites, road or pcm");
                if (const auto* offset = node.find("offset"))
                    patch.offset = static_cast<uint32_t>(offset->integer(0, 0x7fffffff));
                patch.asset = asset->text();
                def.rom_patches.push_back(std::move(patch));
            }
        }
    }

    static Backdrop parse_backdrop(const Json& node) {
        node.keys({"asset", "parallax", "drift", "follow_roll"});
        const auto* asset = node.find("asset");
        if (!asset || asset->text().empty())
            throw std::runtime_error("Backdrop requires an asset");
        Backdrop backdrop;
        backdrop.asset = asset->text();
        auto pair = [](const Json& value, double& x, double& y, const char* field) {
            if (value.kind != Json::Kind::array || value.array.size() != 2u)
                throw std::runtime_error(std::string("Backdrop ") + field + " requires [x, y]");
            x = value.array[0].numeric();
            y = value.array[1].numeric();
        };
        if (const auto* p = node.find("parallax")) pair(*p, backdrop.parallax_x, backdrop.parallax_y, "parallax");
        if (const auto* d = node.find("drift")) pair(*d, backdrop.drift_x, backdrop.drift_y, "drift");
        if (const auto* r = node.find("follow_roll")) backdrop.follow_roll = r->flag();
        return backdrop;
    }

    static Definition from_json(const Json& doc) {
        doc.keys({
            "$schema", "version", "name", "objective", "environment",
            "slot", "source_stage", "enemy_stage", "scenery_stage",
            "music", "road_setup", "sky_setup", "curve_setup",
            "replace_events", "events", "replace_enemy_waves", "enemy_waves",
            "length", "width", "start_distance", "start_height",
            "speed", "glideslope", "tanker_span", "seamless_takeoff", "player_landing", "carrier_landing",
            "presentation", "layers",
            "asset_packs", "music_file", "sprites", "rom_patches", "scenery_patterns", "palette_patches", "boss"
        });
        const auto* ver = doc.find("version");
        if (!ver || ver->integer(1, 1) != 1)
            throw std::runtime_error("Custom stage requires version 1");

        Definition def;
        if (const auto* n = doc.find("name")) {
            def.name = n->text();
            if (def.name.empty()) throw std::runtime_error("Stage name must not be empty");
        }
        if (const auto* obj = doc.find("objective")) {
            const auto s = obj->text();
            if (s == "combat") def.objective = Objective::combat;
            else if (s == "landing") def.objective = Objective::landing;
            else if (s == "takeoff") def.objective = Objective::takeoff;
            else if (s == "refuel") def.objective = Objective::refuel;
            else throw std::runtime_error("Unsupported objective: " + s);
        }
        if (const auto* env = doc.find("environment")) {
            const auto s = env->text();
            if (s == "carrier") def.environment = Environment::carrier;
            else if (s == "runway") def.environment = Environment::runway;
            else if (s == "jungle") def.environment = Environment::jungle;
            else if (s == "sea") def.environment = Environment::sea;
            else if (s == "canyon") def.environment = Environment::canyon;
            else if (s == "night") def.environment = Environment::night;
            else throw std::runtime_error("Unsupported environment: " + s);
        }
        if (def.environment == Environment::sea && def.objective != Objective::refuel && def.objective != Objective::combat)
            throw std::runtime_error("Sea environment requires refuel or combat objective");

        if (const auto* sl = doc.find("slot"))
            def.slot = static_cast<unsigned>(sl->integer(1, 22));
        else
            def.slot = def.default_slot();
        if (const auto* src = doc.find("source_stage"))
            def.source_stage = static_cast<unsigned>(src->integer(1, 64));
        if (const auto* es = doc.find("enemy_stage"))
            def.enemy_stage = static_cast<unsigned>(es->integer(1, 64));
        if (const auto* scs = doc.find("scenery_stage"))
            def.scenery_stage = static_cast<unsigned>(scs->integer(1, 64));
        if (const auto* mus = doc.find("music")) {
            if (mus->kind == Json::Kind::number) {
                def.music_cmd = static_cast<uint8_t>(mus->integer(0, 255));
            } else {
                const auto m = mus->text();
                if (m == "maximum_power") def.music_cmd = 0x81u;
                else if (m == "final_take_off") def.music_cmd = 0x82u;
                else if (m == "red_out") def.music_cmd = 0x83u;
                else if (m == "super_stripe") def.music_cmd = 0x84u;
                else if (m == "after_burner") def.music_cmd = 0x85u;
                else if (m == "city_202") def.music_cmd = 0x86u;
                else if (m == "inherit") def.music_cmd = 0x80u;
                else throw std::runtime_error("Unknown music track: " + m);
            }
        }
        if (const auto* rs = doc.find("road_setup"))
            def.road_setup = static_cast<uint16_t>(rs->integer(0, 65535));
        if (const auto* ss = doc.find("sky_setup"))
            def.sky_setup = static_cast<uint16_t>(ss->integer(0, 65535));
        if (const auto* cs = doc.find("curve_setup"))
            def.curve_setup = static_cast<uint16_t>(cs->integer(0, 65535));
        if (const auto* re = doc.find("replace_events"))
            def.replace_events = re->flag();
        if (const auto* evs = doc.find("events")) {
            if (evs->kind != Json::Kind::array)
                throw std::runtime_error("events must be a JSON array");
            for (const auto& ev_node : evs->array) {
                ev_node.keys({"tick", "command", "params"});
                const auto* tick_node = ev_node.find("tick");
                const auto* cmd_node = ev_node.find("command");
                if (!tick_node || !cmd_node)
                    throw std::runtime_error("Event requires tick and command");
                EventRecord rec;
                rec.tick = static_cast<uint16_t>(tick_node->integer(0, 0xfffeu));
                if (cmd_node->kind == Json::Kind::number) {
                    rec.command = static_cast<uint8_t>(cmd_node->integer(0, 255));
                } else {
                    rec.command = EventRecord::parse_control_command(cmd_node->text());
                }
                if (const auto* p_node = ev_node.find("params")) {
                    if (p_node->kind != Json::Kind::array || p_node->array.size() > 5u)
                        throw std::runtime_error("Event params must be an array of at most 5 bytes");
                    for (size_t i = 0; i < p_node->array.size(); ++i)
                        rec.params[i] = static_cast<uint8_t>(p_node->array[i].integer(0, 255));
                }
                rec.validate_control();
                def.custom_events.push_back(rec);
            }
        }
        if (const auto* rew = doc.find("replace_enemy_waves"))
            def.replace_enemy_waves = rew->flag();
        if (const auto* waves = doc.find("enemy_waves")) {
            if (waves->kind != Json::Kind::array)
                throw std::runtime_error("enemy_waves must be a JSON array");
            for (const auto& w_node : waves->array) {
                w_node.keys({"tick", "command", "formation", "params"});
                const auto* tick_node = w_node.find("tick");
                if (!tick_node)
                    throw std::runtime_error("Enemy wave requires tick");
                EventRecord rec;
                rec.tick = static_cast<uint16_t>(tick_node->integer(0, 0xfffeu));
                if (const auto* cmd_node = w_node.find("command")) {
                    if (cmd_node->kind == Json::Kind::number) {
                        rec.command = static_cast<uint8_t>(cmd_node->integer(0, 255));
                    } else {
                        const auto c = cmd_node->text();
                        if (c == "standard" || c == "formation_0" || c == "enemy_wave") rec.command = 0u;
                        else if (c == "parameterized" || c == "formation_1") rec.command = 1u;
                        else if (c == "dual_param" || c == "formation_2") rec.command = 2u;
                        else if (c == "formation_3") rec.command = 3u;
                        else if (c == "formation_4") rec.command = 4u;
                        else if (c == "wave_param_5") rec.command = 5u;
                        else if (c == "wave_param_6") rec.command = 6u;
                        else if (c == "wave_param_7") rec.command = 7u;
                        else if (c == "wave_param_8") rec.command = 8u;
                        else if (c == "wave_nop_9") rec.command = 9u;
                        else throw std::runtime_error("Unknown enemy wave command: " + c);
                    }
                }
                const auto* form_node = w_node.find("formation");
                if (form_node)
                    rec.params[0] = static_cast<uint8_t>(form_node->integer(0, 255));
                if (const auto* p_node = w_node.find("params")) {
                    const size_t base = form_node ? 1u : 0u;
                    const size_t max_len = 5u - base;
                    if (p_node->kind != Json::Kind::array || p_node->array.size() > max_len)
                        throw std::runtime_error("Enemy wave params array exceeds 8-byte record capacity");
                    for (size_t i = 0; i < p_node->array.size(); ++i)
                        rec.params[base + i] = static_cast<uint8_t>(p_node->array[i].integer(0, 255));
                }
                rec.validate_enemy_wave();
                def.custom_enemy_waves.push_back(rec);
            }
        }

        auto range_num = [&](const char* key, double& field, double low, double high) {
            if (const auto* n = doc.find(key)) {
                const double v = n->numeric();
                if (v < low || v > high)
                    throw std::runtime_error(std::string("Field out of range: ") + key);
                field = v;
            }
        };
        range_num("length", def.length, 250.0, 10000.0);
        range_num("width", def.width, 20.0, 200.0);
        range_num("start_distance", def.start_distance, 0.0, 10000.0);
        range_num("start_height", def.start_height, -50.0, 2000.0);
        range_num("speed", def.speed, 0.0, 250.0);
        range_num("glideslope", def.slope, 0.01, 0.2);
        range_num("tanker_span", def.tanker_span, 20.0, 100.0);
        if (const auto* st = doc.find("seamless_takeoff"))
            def.seamless_takeoff = st->flag();
        if (const auto* pl = doc.find("player_landing"))
            def.player_landing = pl->flag();
        if (const auto* cl = doc.find("carrier_landing"))
            def.carrier_landing = cl->flag();

        if (const auto* pres = doc.find("presentation")) {
            pres->keys({"rom_environment", "ocean", "player_shadow", "tanker_variant", "clouds", "hud", "backdrop"});
            if (const auto* bd = pres->find("backdrop")) def.backdrop = parse_backdrop(*bd);
            if (const auto* re = pres->find("rom_environment")) def.rom_environment = re->flag();
            if (const auto* ps = pres->find("player_shadow")) def.player_shadow = ps->flag();
            if (const auto* cl = pres->find("clouds")) def.clouds = cl->flag();
            if (const auto* hd = pres->find("hud")) def.hud = hd->flag();
            if (const auto* tv = pres->find("tanker_variant")) {
                if (tv->kind == Json::Kind::string) {
                    if (tv->text() != "random")
                        throw std::runtime_error("tanker_variant string must be 'random'");
                    def.tanker_variant = 0u;
                } else {
                    def.tanker_variant = static_cast<unsigned>(tv->integer(1, 64));
                }
            }
            if (const auto* oc = pres->find("ocean")) {
                oc->keys({"sky", "haze", "water", "water_band"});
                def.custom_ocean = true;
                auto parse_rgb = [](const Json* node, RGBColor& dst) {
                    if (!node) return;
                    if (node->kind != Json::Kind::array || node->array.size() != 3u)
                        throw std::runtime_error("Ocean color requires 3 RGB integers");
                    dst.r = static_cast<uint8_t>(node->array[0].integer(0, 255));
                    dst.g = static_cast<uint8_t>(node->array[1].integer(0, 255));
                    dst.b = static_cast<uint8_t>(node->array[2].integer(0, 255));
                };
                parse_rgb(oc->find("sky"), def.ocean.sky);
                parse_rgb(oc->find("haze"), def.ocean.haze);
                parse_rgb(oc->find("water"), def.ocean.water);
                if (const auto* band = oc->find("water_band")) {
                    RGBColor color;
                    parse_rgb(band, color);
                    def.ocean.water_band = color;
                }
            }
        }
        if (const auto* lay = doc.find("layers"))
            def.layers = parse_layers(*lay);
        parse_external_content(doc, def);
        return def;
    }

    static Definition load(const std::string& path) {
        return from_json(Json::load(path));
    }

    static constexpr unsigned preset_count = 8;

    static Definition preset(unsigned index) {
        Definition d;
        switch (index % preset_count) {
        case 0: // Refueling rendezvous (Stage 3 donor)
            d.name = "TANKER / RENDEZVOUS";
            d.objective = Objective::refuel;
            d.environment = Environment::sea;
            d.length = 2200.0; d.width = 45.0;
            d.start_distance = 220.0; d.start_height = -5.0;
            d.speed = 140.0; d.slope = 0.06; d.tanker_span = 50.0;
            break;
        case 1: // Carrier landing + seamless takeoff (Stage 1 donor)
            d.name = "CARRIER / LANDING";
            d.objective = Objective::landing;
            d.environment = Environment::carrier;
            d.length = 300.0; d.width = 70.0;
            d.start_distance = 1200.0; d.start_height = 76.0;
            d.speed = 80.0; d.slope = 0.06; d.seamless_takeoff = true;
            d.custom_ocean = true;
            break;
        case 2: // Autumn runway landing + service + takeoff (Stage 5 donor)
            d.name = "AUTUMN RUNWAY / LANDING";
            d.objective = Objective::landing;
            d.environment = Environment::runway;
            d.length = 2200.0; d.width = 45.0;
            d.start_distance = 1400.0; d.start_height = 88.0;
            d.speed = 80.0; d.slope = 0.06; d.seamless_takeoff = true;
            break;
        case 3: // Jungle strip landing + service + takeoff (Stage 13 donor)
            d.name = "JUNGLE STRIP / LANDING";
            d.objective = Objective::landing;
            d.environment = Environment::jungle;
            d.length = 2200.0; d.width = 45.0;
            d.start_distance = 1400.0; d.start_height = 88.0;
            d.speed = 80.0; d.slope = 0.06; d.seamless_takeoff = true;
            break;
        case 4: // Autumn runway takeoff (Stage 5 donor)
            d.name = "AUTUMN RUNWAY / TAKEOFF";
            d.objective = Objective::takeoff;
            d.environment = Environment::runway;
            d.length = 2200.0; d.width = 45.0;
            d.start_distance = 0.0; d.start_height = 0.0;
            d.speed = 0.0; d.slope = 0.06;
            break;
        case 5: // Carrier catapult takeoff (Stage 1 donor)
            d.name = "CARRIER / TAKEOFF";
            d.objective = Objective::takeoff;
            d.environment = Environment::carrier;
            d.length = 300.0; d.width = 70.0;
            d.start_distance = 0.0; d.start_height = 0.0;
            d.speed = 0.0; d.slope = 0.06;
            d.custom_ocean = true;
            break;
        case 6: // Jungle strip takeoff (Stage 13 donor)
            d.name = "JUNGLE STRIP / TAKEOFF";
            d.objective = Objective::takeoff;
            d.environment = Environment::jungle;
            d.length = 2200.0; d.width = 45.0;
            d.start_distance = 0.0; d.start_height = 0.0;
            d.speed = 0.0; d.slope = 0.06;
            break;
        case 7: // Canyon combat custom stage (Stage 8 donor with custom event waves)
            d.name = "CANYON / AMBUSH";
            d.objective = Objective::combat;
            d.environment = Environment::canyon;
            d.music_cmd = 0x84u; // Super Stripe
            d.custom_events = {
                {16, 17, {0, 0, 0, 0, 0}}, // Special canyon road enter ($E074)
                {32, 11, {2, 0, 0, 0, 0}}, // Cloud control ($DFBC -> $FFFF8D6A = 2)
                {64, 20, {0, 0, 128, 0, 0}}, // Horizon scroll ($DF7E -> $FFFF8D38 = 0x0080)
                {96, 16, {0, 0, 0, 0, 0}}, // Special road exit ($E062)
            };
            d.custom_enemy_waves = {
                {16, 0, {6, 0, 0, 0, 0}},
                {48, 0, {8, 0, 0, 0, 0}},
            };
            break;
        }
        d.slot = d.default_slot();
        return d;
    }

    static Definition preset_by_name(std::string_view name) {
        if (name == "refuel") return preset(0);
        if (name == "landing" || name == "carrier-landing") return preset(1);
        if (name == "runway" || name == "runway-landing") return preset(2);
        if (name == "runway-jungle" || name == "jungle-landing") return preset(3);
        if (name == "runway-takeoff") return preset(4);
        if (name == "carrier-takeoff") return preset(5);
        if (name == "jungle-takeoff") return preset(6);
        if (name == "canyon" || name == "canyon-combat") return preset(7);
        throw std::runtime_error("Unknown custom stage preset: " + std::string(name));
    }
};

} // namespace CustomStage

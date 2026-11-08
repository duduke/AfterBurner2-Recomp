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
#include <optional>
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
    // X-Board 16-bit palette word: 0000 BBBB BGGG GGRR RRR0 (or standard 5-5-5 low bits).
    // HWPalette::pen decodes 5-bit channels from bits 0..4 (R), 5..9 (G), 10..14 (B).
    constexpr uint16_t rgb555() const {
        return uint16_t(((uint16_t(b >> 3) & 0x1fu) << 10) |
                        ((uint16_t(g >> 3) & 0x1fu) << 5) |
                        (uint16_t(r >> 3) & 0x1fu));
    }
};

struct OceanPalette {
    RGBColor sky{0, 170, 255};
    RGBColor haze{220, 250, 255};
    RGBColor water{0, 0, 255};

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

struct LayerElement {
    enum class Type { rectangle, image };
    Type type = Type::rectangle;
    std::string asset;
    double x{}, y{}, z{};
    double width{32.0}, height{32.0}, rotation{};
    unsigned columns{1}, rows{1};
    std::array<uint8_t, 4> color{255, 255, 255, 255};
};

struct SceneLayer {
    std::string name;
    double order{};
    enum class Space { screen, world };
    Space space = Space::screen;
    std::vector<LayerElement> elements;
};

// Native 8-byte X-Board event record ($DE68..$DE8C).
// Bytes 0..1: 16-bit stage timer tick (shifted left 4 when compared against $29C006).
// Byte 2: event command index (0..20, where 12 is stage advance and 18 is palette blend).
// Bytes 3..7: command-specific parameters (formation ID, scenery table index, etc.).
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
};

struct Definition {
    enum class Objective { combat, landing, takeoff, refuel };
    enum class Environment { carrier, runway, jungle, sea, canyon, night };

    std::string name = "CARRIER / LANDING";
    Objective objective = Objective::landing;
    Environment environment = Environment::carrier;

    // Native ROM compilation targets:
    unsigned slot = 1;                  // 1-based stage slot in the ROM progression (1..22)
    unsigned source_stage = 0;          // 0 = auto-select donor stage from environment/objective
    std::optional<uint8_t> music_cmd;   // Optional override for $2B604 music command byte
    std::optional<uint16_t> road_setup; // Optional override for $32196 road word
    std::optional<uint16_t> sky_setup;  // Optional override for $32324 setup word
    std::optional<uint16_t> curve_setup;// Optional override for $32452 setup word
    bool replace_events = false;        // True = replace donor events with custom_events; False = splice
    std::vector<EventRecord> custom_events;

    // Interactive flight/sequence parameters:
    double length = 300.0;
    double width = 70.0;
    double start_distance = 1200.0;
    double start_height = 76.0;
    double speed = 80.0;
    double slope = 0.06;
    double tanker_span = 50.0;
    bool seamless_takeoff = true;

    // Presentation & layer options:
    bool rom_environment = true;
    bool player_shadow = true;
    bool clouds = true;
    bool hud = true;
    bool custom_ocean = false;
    OceanPalette ocean{};
    unsigned tanker_variant = 0; // 0 = random cycle across {3, 9, 11, 16, 19, 21}
    std::vector<SceneLayer> layers;

    // Resolve the canonical donor stage (1..23) used by the ROM-view compiler.
    unsigned donor_stage() const {
        if (source_stage) return source_stage;
        if (objective == Objective::refuel)
            return tanker_variant ? tanker_variant : 3u;
        switch (environment) {
        case Environment::carrier: return 1u;
        case Environment::runway:  return 5u;
        case Environment::jungle:  return 13u;
        case Environment::sea:     return 3u;
        case Environment::canyon:  return 8u;
        case Environment::night:   return 9u;
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
                elem_node.keys({"type", "asset", "x", "y", "z", "width", "height", "rotation", "color", "repeat"});
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
                layer.elements.push_back(std::move(elem));
            }
            out.push_back(std::move(layer));
        }
        std::stable_sort(out.begin(), out.end(), [](const SceneLayer& a, const SceneLayer& b) {
            return a.order < b.order;
        });
        return out;
    }

    static Definition from_json(const Json& doc) {
        doc.keys({
            "$schema", "version", "name", "objective", "environment",
            "slot", "source_stage", "music", "road_setup", "sky_setup", "curve_setup",
            "replace_events", "events",
            "length", "width", "start_distance", "start_height",
            "speed", "glideslope", "tanker_span", "seamless_takeoff",
            "presentation", "layers"
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
        if (const auto* src = doc.find("source_stage"))
            def.source_stage = static_cast<unsigned>(src->integer(1, 64));
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
                    const auto c = cmd_node->text();
                    if (c == "enemy_wave") rec.command = 2u;
                    else if (c == "scenery") rec.command = 3u;
                    else if (c == "special_road_exit") rec.command = 10u;
                    else if (c == "special_road_enter") rec.command = 11u;
                    else if (c == "stage_advance") rec.command = 12u;
                    else if (c == "refuel") rec.command = 14u;
                    else if (c == "landing") rec.command = 15u;
                    else throw std::runtime_error("Unknown event command: " + c);
                }
                if (rec.command == 18u)
                    throw std::runtime_error("Event command 18 is reserved for boundary palette installation");
                if (const auto* p_node = ev_node.find("params")) {
                    if (p_node->kind != Json::Kind::array || p_node->array.size() > 5u)
                        throw std::runtime_error("Event params must be an array of at most 5 bytes");
                    for (size_t i = 0; i < p_node->array.size(); ++i)
                        rec.params[i] = static_cast<uint8_t>(p_node->array[i].integer(0, 255));
                }
                def.custom_events.push_back(rec);
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

        if (const auto* pres = doc.find("presentation")) {
            pres->keys({"rom_environment", "ocean", "player_shadow", "tanker_variant", "clouds", "hud"});
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
                oc->keys({"sky", "haze", "water"});
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
            }
        }
        if (const auto* lay = doc.find("layers"))
            def.layers = parse_layers(*lay);
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
                {16, 11, {0, 0, 0, 0, 0}}, // Special canyon road enter ($E074)
                {32, 2,  {1, 0, 0, 0, 0}}, // Enemy wave spawn
                {64, 3,  {2, 0, 0, 0, 0}}, // Scenery formation
                {96, 10, {0, 0, 0, 0, 0}}, // Special road exit ($E062)
            };
            break;
        }
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

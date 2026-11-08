#pragma once

#include "engine/custom_stage_json.hpp"
#include "engine/game_controls.hpp"
#include "engine/mainbus.hpp"
#include "engine/stage_catalog.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace CustomStage {

struct Point {
    double x{}, y{};
};

inline Point rotate_point(Point p, double degrees) {
    const double rad = degrees * 3.14159265358979323846 / 180.0;
    return {p.x * std::cos(rad) - p.y * std::sin(rad),
            p.x * std::sin(rad) + p.y * std::cos(rad)};
}

inline Point anchored_image(Point anchor, Point local_root, double scale, double degrees, bool flip) {
    if (flip) local_root.x = -local_root.x;
    const auto d = rotate_point({local_root.x * scale, local_root.y * scale}, degrees);
    return {anchor.x - d.x, anchor.y - d.y};
}

struct Camera {
    double x{}, height{20.0}, horizon{198.0}, focal{480.0}, behind{34.0};

    struct Projected {
        float x{}, y{};
        bool visible{};
    };

    Projected project(double wx, double wy, double wz) const {
        const double depth = wz + behind;
        if (depth <= 1.0) return {0.0f, 0.0f, false};
        return {
            float(480.0 + (wx - x) * focal / depth),
            float(horizon + (height + 20.0 - wy) * focal / depth),
            true
        };
    }

    double span(double meters, double z) const {
        return meters * focal / std::max(2.0, z + behind);
    }
};

struct ShadowState {
    Point offset{};
    double scale{1.0}, opacity{0.5}, softness{};

    static ShadowState at_height(double height, double bank_radians) {
        const double h = std::max(0.0, height);
        const double norm = std::clamp(h / 90.0, 0.0, 1.0);
        const double foreshorten = std::clamp(1.0 - std::abs(bank_radians) * 0.18, 0.72, 1.0);
        return {
            {h * 0.22, h * 0.35},
            std::clamp((1.0 - norm * 0.45) * foreshorten, 0.35, 1.0),
            std::clamp(0.62 * (1.0 - norm * 0.68), 0.12, 0.62),
            std::clamp(norm * 4.5, 0.0, 4.5)
        };
    }

    struct Vertex {
        Point point{};
        Point uv{};
    };

    static std::vector<Vertex> clip(std::vector<Vertex> poly, const std::array<Point, 4>& quad) {
        auto clip_edge = [](const std::vector<Vertex>& input, Point a, Point b) {
            std::vector<Vertex> out;
            if (input.empty()) return out;
            auto inside = [&](Point p) {
                return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) >= -1e-9;
            };
            auto intersect = [&](const Vertex& v1, const Vertex& v2) -> Vertex {
                const double dx = v2.point.x - v1.point.x, dy = v2.point.y - v1.point.y;
                const double ex = b.x - a.x, ey = b.y - a.y;
                const double denom = dx * ey - dy * ex;
                const double t = std::abs(denom) < 1e-12 ? 0.0 :
                    ((a.x - v1.point.x) * ey - (a.y - v1.point.y) * ex) / denom;
                const double ct = std::clamp(t, 0.0, 1.0);
                return {
                    {v1.point.x + dx * ct, v1.point.y + dy * ct},
                    {v1.uv.x + (v2.uv.x - v1.uv.x) * ct, v1.uv.y + (v2.uv.y - v1.uv.y) * ct}
                };
            };
            for (size_t i = 0; i < input.size(); ++i) {
                const Vertex& cur = input[i];
                const Vertex& prev = input[(i + input.size() - 1u) % input.size()];
                const bool cur_in = inside(cur.point);
                const bool prev_in = inside(prev.point);
                if (cur_in) {
                    if (!prev_in) out.push_back(intersect(prev, cur));
                    out.push_back(cur);
                } else if (prev_in) {
                    out.push_back(intersect(prev, cur));
                }
            }
            return out;
        };
        for (size_t i = 0; i < 4u; ++i)
            poly = clip_edge(poly, quad[i], quad[(i + 1u) % 4u]);
        return poly;
    }
};

struct Guidance {
    static constexpr double probe_forward = 12.0;
    static constexpr double probe_right = 1.8;
    static constexpr double probe_up = 0.3;
    static constexpr double capture_radius = 0.45;
    static constexpr Point ils_reference{480.0, 300.0};

    static unsigned pitch_pose(double pitch) {
        return unsigned(std::clamp(std::lround(2.0 + std::clamp(pitch, -1.0, 1.0) * 2.0), 0l, 4l));
    }

    static double contact_distance(double x, double height, double range) {
        return std::sqrt(x * x + height * height + (range - probe_forward) * (range - probe_forward));
    }

    static double closure(double range, double initial) {
        if (range <= probe_forward) return 1.0;
        return std::clamp((initial - range) / std::max(1.0, initial - probe_forward), 0.0, 1.0);
    }

    struct Landing {
        Point cross{}, velocity{};
        double lateral_angle{}, glide_angle{};
        bool localizer_centered() const { return std::abs(cross.x - ils_reference.x) <= 3.0; }
        bool glideslope_centered() const { return std::abs(cross.y - ils_reference.y) <= 3.0; }
    };

    struct Tanker {
        Point offset{};
        double radius{};
        bool locked{};
    };

    static Tanker tanker(double x, double height, double range, double initial, bool locked, bool acquiring = false) {
        locked = locked || acquiring;
        return {
            locked ? Point{} : Point{x * 2.0, -height * 2.0},
            locked ? 84.0 : std::min(83.0, 10.0 + 74.0 * closure(range, initial)),
            locked
        };
    }

    static Landing landing(double x, double height, double range, double lateral, double vertical,
                           double speed, double slope, double horizon, double focal) {
        const double ahead = std::max(45.0, range);
        const double safe_speed = std::max(1.0, speed);
        const double lateral_angle = std::atan2(-x, ahead);
        const double flare_slope = height < 4.0 ? 1.7 / safe_speed : slope;
        const double glide_angle = std::atan2(height - 2.0, ahead) - std::atan(flare_slope);
        return {
            {ils_reference.x + std::clamp(focal * lateral_angle, -100.0, 100.0),
             ils_reference.y + std::clamp(focal * glide_angle, -90.0, 90.0)},
            {480.0 + std::clamp(focal * lateral / safe_speed, -190.0, 190.0),
             horizon - std::clamp(focal * vertical / safe_speed, -160.0, 160.0)},
            lateral_angle,
            glide_angle
        };
    }
};

// Native board-coupled interactive sequence and custom-stage controller.
// Operates alongside XBoardGame inside GameSession (or standalone for fast
// ROM-free regression checks). Never buffers 600 RGB frames: the full-throttle
// departure gate holds the native 68000 sequence timer in place until armed.
class InteractiveController {
public:
    using Objective = Definition::Objective;
    using Environment = Definition::Environment;

    enum class Phase {
        briefing,
        combat,
        approach,
        contact,
        rollout,
        service,
        taxi,
        ready,
        accelerating,
        climb,
        success,
        failed
    };

    static constexpr double dt = 1.0 / 60.0;

    Definition definition;
    Phase phase = Phase::briefing;
    double x{}, height{}, range{}, speed{}, pitch{}, bank{};
    double vertical{}, lateral{}, elapsed{}, fuel{1.0}, rearm_progress{}, power{};
    bool departing{}, coupled{}, throttle_armed{};
    double phase_time{}, surface_offset{}, departure_origin{};
    unsigned missiles{50}, missile_max{100}, ticks{};
    uint32_t board_sequence_owner{};
    bool board_gate_held{};
    // Player-flare state (ROM airbase object, handler $1122E state 4).
    bool flare_seen{};
    unsigned flare_frames{};
    unsigned flare_last_delta{};
    double flare_k{};
    const char* message = "ENTER / START TO BEGIN";

    explicit InteractiveController(Definition def = Definition::preset(1))
        : definition(std::move(def)) {
        reset();
    }

    void reset() {
        phase = Phase::briefing;
        x = (definition.objective == Objective::takeoff || definition.objective == Objective::combat) ? 0.0 : 8.0;
        height = definition.start_height;
        range = definition.start_distance;
        speed = definition.speed;
        pitch = bank = vertical = lateral = elapsed = power = rearm_progress = 0.0;
        fuel = 1.0;
        departing = coupled = throttle_armed = false;
        phase_time = 0.0;
        surface_offset = (definition.objective == Objective::takeoff) ? 0.0 : aim_offset();
        departure_origin = range;
        missiles = (definition.objective == Objective::takeoff || definition.objective == Objective::combat) ? 100u : 50u;
        missile_max = 100u;
        ticks = 0u;
        board_sequence_owner = 0u;
        board_gate_held = false;
        flare_seen = false;
        flare_frames = 0u;
        flare_last_delta = 0u;
        flare_k = 0.0;
        message = "ENTER / START TO BEGIN";
    }

    void start() {
        if (phase != Phase::briefing) return;
        if (definition.objective == Objective::combat) {
            phase = Phase::combat;
            message = "INTERCEPT HOSTILES";
        } else if (definition.objective == Objective::takeoff) {
            phase = Phase::ready;
            message = "FULL THROTTLE TO START TAKEOFF";
        } else {
            phase = Phase::approach;
            message = "FLY THE STAGE";
        }
    }

    bool finished() const { return phase == Phase::success || phase == Phase::failed; }
    bool carrier() const { return definition.environment == Environment::carrier; }
    bool refuel() const { return definition.objective == Objective::refuel; }
    bool combat() const { return definition.objective == Objective::combat; }
    bool takeoff() const { return definition.objective == Objective::takeoff || departing; }
    bool servicing() const { return phase == Phase::service || phase == Phase::taxi; }
    double surface_start() const { return range - surface_offset; }
    double ground_run() const { return departure_origin - range; }
    double contact_distance() const { return Guidance::contact_distance(x, height, range); }
    bool gear_down() const { return !refuel() && !combat() && (height < 15.0 || servicing() || phase == Phase::ready); }
    double service_progress() const { return std::clamp((phase_time - 2.0) / 6.0, 0.0, 1.0); }
    double aim_offset() const { return carrier() ? 55.0 : 180.0; }
    double target_height() const { return refuel() ? 0.0 : std::max(0.0, range) * definition.slope + 2.0; }

    bool aligned() const {
        return contact_distance() <= Guidance::capture_radius &&
               std::abs(bank) < 0.16 &&
               std::abs(pitch) < 0.16 &&
               std::abs(speed - 140.0) < 1.0;
    }

    bool burner() const {
        return (takeoff() || combat()) && power >= 0.95 &&
               phase != Phase::ready && phase != Phase::briefing && phase != Phase::failed;
    }

    void end(bool good, const char* why) {
        phase = good ? Phase::success : Phase::failed;
        message = why;
    }

    void tick(GameControls input) {
        if (phase == Phase::briefing || finished()) return;
        const auto clean = [](float v) {
            return std::isfinite(v) ? std::clamp(double(v), -1.0, 1.0) : 0.0;
        };
        power = clean(input.throttle);
        const double py = clean(input.stick_y);
        const double bx = clean(input.stick_x);
        pitch += (py - pitch) * std::min(1.0, dt * 5.0);
        bank += (bx - bank) * std::min(1.0, dt * 5.0);
        elapsed += dt;
        ++ticks;
        phase_time += dt;

        if (phase == Phase::combat) {
            lateral = bank * 24.0;
            x = std::clamp(x + lateral * dt, -160.0, 160.0);
            vertical = pitch * 18.0;
            height = std::clamp(height + vertical * dt, 5.0, 500.0);
            const double target_spd = 140.0 + power * 40.0;
            speed += (target_spd - speed) * dt * 2.0;
            range = std::max(0.0, range - speed * dt);
            return;
        }

        if (phase == Phase::service) {
            power = 0.0;
            pitch = bank = vertical = lateral = 0.0;
            if (phase_time >= 2.0)
                missiles = std::min(missile_max, 50u + unsigned(service_progress() * 50.0));
            message = phase_time < 2.0 ? "REARMING CREW APPROACHING" :
                      phase_time < 8.0 ? "REARMING MISSILES" :
                                         "REARM COMPLETE / CREW CLEARING";
            if (phase_time >= 10.0) {
                missiles = missile_max;
                phase_time = 0.0;
                departing = true;
                departure_origin = range;
                phase = carrier() ? Phase::taxi : Phase::ready;
                message = carrier() ? "DECK CREW / TOWING TO CATAPULT" :
                                      "REARM COMPLETE / FULL THROTTLE TO TAKE OFF";
            }
            return;
        }

        if (phase == Phase::taxi) {
            power = 0.0;
            pitch = bank = vertical = lateral = 0.0;
            range = std::min(surface_offset, range + 35.0 * dt);
            if (range >= surface_offset) {
                phase = Phase::ready;
                phase_time = 0.0;
                departure_origin = range;
                message = "CATAPULT READY / FULL THROTTLE TO TAKE OFF";
            }
            return;
        }

        lateral = bank * (refuel() ? 13.0 : 20.0);
        x += lateral * dt;

        if (phase == Phase::rollout) {
            speed = std::max(0.0, speed - (carrier() ? 36.0 : 16.0) * dt);
            range -= speed * dt;
            vertical = 0.0;
            if (std::abs(x) > definition.width * 0.45) {
                end(false, "LEFT THE LANDING SURFACE");
            } else if (range < -(definition.length - aim_offset())) {
                end(false, "ROLLOUT OVERRUN");
            } else if (speed == 0.0) {
                if (definition.seamless_takeoff) {
                    phase = Phase::service;
                    phase_time = 0.0;
                    message = "LANDED / REARM MISSILES";
                } else {
                    end(true, "LANDING COMPLETE / AIRCRAFT STOPPED");
                }
            }
            return;
        }

        if (takeoff()) {
            if (phase == Phase::ready) {
                speed = 0.0;
                pitch = bank = 0.0;
                message = carrier() ? "CATAPULT READY / FULL THROTTLE TO TAKE OFF" :
                                      "REARM COMPLETE / FULL THROTTLE TO TAKE OFF";
                if (!throttle_armed && power >= 0.25)
                    message = "RELEASE THROTTLE / THEN FULL POWER TO TAKE OFF";
                if (power < 0.25)
                    throttle_armed = true;
                if (throttle_armed && power >= 0.95) {
                    phase = Phase::accelerating;
                    message = "FULL POWER / BUILD SPEED";
                }
                return;
            }
            speed = std::clamp(speed + ((carrier() ? 37.0 : 14.0) * std::max(0.0, power) -
                                        2.0 * std::max(0.0, -power)) * dt, 0.0, 150.0);
            range -= speed * dt;
            if (speed > 68.0 && pitch > 0.12) {
                vertical = std::max(0.0, pitch * 22.0);
                height += vertical * dt;
                phase = Phase::climb;
            } else if (height > 0.0) {
                vertical = -3.0;
                height = std::max(0.0, height + vertical * dt);
            }
            message = speed < 70.0 ? "FULL POWER / BUILD SPEED" : "PULL BACK TO ROTATE / CLIMB";
            if (std::abs(x) > definition.width * 0.45 && height < 4.0)
                end(false, "LEFT THE DEPARTURE SURFACE");
            else if (definition.length + surface_start() < 30.0 && height < 5.0)
                end(false, "OVERRUN / ROTATE EARLIER");
            else if (height > 40.0)
                end(true, "TAKEOFF COMPLETE / POSITIVE CLIMB");
            return;
        }

        const double target_speed = refuel() ? 140.0 + power * 25.0 : definition.speed + power * 25.0;
        speed += (target_speed - speed) * dt * 1.5;

        if (refuel()) {
            if (!coupled) {
                vertical = pitch * 10.0;
                height += vertical * dt;
                range -= (speed - 140.0) * dt;
                if (aligned()) coupled = true;
            }
            if (coupled) {
                x = height = bank = pitch = vertical = lateral = power = 0.0;
                range = Guidance::probe_forward;
                speed = 140.0;
                phase = Phase::contact;
                rearm_progress = std::min(1.0, rearm_progress + dt / 4.0);
                missiles = std::min(missile_max, 50u + unsigned(rearm_progress * 50.0));
            } else {
                phase = Phase::approach;
            }
            message = coupled ? "DIAMONDS LOCKED / RELOADING MISSILES" :
                      contact_distance() <= Guidance::capture_radius ? "MATCH SPEED / LEVEL WINGS TO DOCK" :
                                                                       "MATCH THE DIAMONDS / DOCK THE PROBE";
            if (missiles >= missile_max)
                end(true, "REARM COMPLETE / PROBE CONNECTED");
            else if (range < 3.0)
                end(false, "TOO CLOSE TO TANKER");
        } else {
            vertical = -speed * definition.slope + pitch * 9.0;
            if (height < 10.0 && pitch > 0.0)
                vertical = std::max(vertical, -2.2);
            height += vertical * dt;
            range -= speed * dt;
            message = height < 12.0 ? "FLARE / WINGS LEVEL" : "FOLLOW THE CROSS / FLIGHT PATH TO AIM POINT";
            if (height <= 0.0) {
                height = 0.0;
                const bool window = range < 25.0 && range > -(definition.length - aim_offset() - 30.0);
                if (window && std::abs(x) < (carrier() ? 14.0 : definition.width * 0.4) &&
                    vertical >= -5.0 && std::abs(bank) < 0.55 && speed >= 55.0 && speed <= 115.0) {
                    phase = Phase::rollout;
                    phase_time = 0.0;
                    message = "TOUCHDOWN / BRAKING";
                } else {
                    end(false, window ? "HARD OR MISALIGNED TOUCHDOWN" : "MISSED THE LANDING SURFACE");
                }
            } else if (range < -(definition.length - aim_offset())) {
                end(false, "MISSED APPROACH / GO AROUND");
            }
        }
        if (elapsed > 100.0)
            end(false, "STAGE TIME LIMIT");
    }

    // Player-controlled flare. In state 4 ($1134A) before touchdown the ROM
    // subtracts $40 from +36(A5) every frame ($113B2) and derives the plane's
    // screen height (+44) from it; touchdown is +36 <= $20 ($11372). At the
    // frame boundary we undo the ROM's $40 and apply a pitch-scaled delta
    // instead: neutral stick == ROM behaviour, pull back slows the sink (or
    // climbs), push forward sinks faster. Touchdown with a last-frame sink
    // delta above hard_sink_delta is a hard landing.
    static constexpr unsigned rom_sink_delta = 0x40u;
    static constexpr unsigned hard_sink_delta = 0x60u;
    static constexpr unsigned flare_frame_limit = 900u;

    void player_flare(MainBus::Memory& memory, uint32_t p, uint16_t state, const GameControls& input) {
        const uint8_t flags27 = memory.bytes.at(MainBus::memory_offset(p + 27u));
        const bool airborne = state == 4u && !(flags27 & 1u);
        if (!airborne) {
            if (flare_seen && state == 4u && (flags27 & 1u)) {
                phase = flare_last_delta > hard_sink_delta ? Phase::failed : Phase::rollout;
                message = flare_last_delta > hard_sink_delta ? "HARD LANDING" : "TOUCHDOWN / BRAKING";
            }
            if (state != 4u) flare_seen = false;
            return;
        }
        const double stick = std::isfinite(input.stick_y) ? std::clamp(double(input.stick_y), -1.0, 1.0) : 0.0;
        const auto q_of = [](int depth) { return double(depth - 0x20) / double(rom_sink_delta); };
        const int cur = int(int16_t(peek16(memory, p + 36u)));
        if (flare_seen && flare_frames < flare_frame_limit) {
            // ROM already removed rom_sink_delta this frame; replace it with the player's.
            const int wanted = int(std::lround(double(rom_sink_delta) * (1.0 - stick)));
            const int delta = std::clamp(wanted, -int(rom_sink_delta), 2 * int(rom_sink_delta));
            const int next = std::clamp(cur + int(rom_sink_delta) - delta, 0, 0x400);
            seed16(memory, p + 36u, uint16_t(next));
            // $1139E..$113AE integrates +44 -= +44/q (q = (+36-$20)/$40), which only
            // telescopes to +44 ~ q for exact $40 steps. Keep the same proportionality
            // for arbitrary steps so the plane's screen height tracks +36.
            const double q = std::max(0.0, q_of(next));
            seed16(memory, p + 44u, uint16_t(int(std::lround(flare_k * q))));
            flare_last_delta = delta > 0 ? unsigned(delta) : 0u;
            message = stick > 0.2 ? "FLARING / EASE DOWN" : stick < -0.2 ? "STEEP SINK" : "HOLD PITCH / LAND";
        } else if (!flare_seen) {
            phase = Phase::approach;
            const double q0 = q_of(cur);
            flare_k = q0 > 0.0 ? double(int16_t(peek16(memory, p + 44u))) / q0 : 0.0;
        }
        flare_seen = true;
        ++flare_frames;
        board_sequence_owner = p;
    }

    static uint16_t peek16(const MainBus::Memory& memory, uint32_t address) {
        const auto offset = MainBus::memory_offset(address);
        if (offset == MainBus::UNMAPPED) throw std::runtime_error("Unmapped custom-stage observation");
        return (uint16_t(memory.bytes.at(offset)) << 8) | memory.bytes.at(offset + 1);
    }

    static uint32_t peek32(const MainBus::Memory& memory, uint32_t address) {
        return (uint32_t(peek16(memory, address)) << 16) | peek16(memory, address + 2);
    }

    static void seed16(MainBus::Memory& memory, uint32_t address, uint16_t value) {
        const auto offset = MainBus::memory_offset(address);
        if (offset == MainBus::UNMAPPED) throw std::runtime_error("Unmapped custom-stage operand");
        memory.bytes.at(offset) = uint8_t(value >> 8);
        memory.bytes.at(offset + 1) = uint8_t(value & 0xffu);
    }

    // Synchronize the interactive sequence controller with the live X-Board
    // 68000 state at the completed frame boundary when an enhanced custom stage
    // is active in GameSession. Holds pre-takeoff state counters in place for
    // the full-throttle gate instead of buffering RGB frames.
    void sync_memory(MainBus::Memory& memory, const Roms& rom, const GameControls& input) {
        const uint16_t mode = peek16(memory, 0x29c09cu);
        if (rom.maincpu.rom && 0x3218au + 2u <= rom.maincpu.length)
            missile_max = std::max<unsigned>(1u, StageCatalog::Detail::word(rom, 0x3218au));
        board_gate_held = false;
        if (mode != 15u && mode != 16u) return;

        if (phase == Phase::briefing) start();
        if (!definition.player_landing)
            tick(input);

        // 1. Carrier sequence ($29C09C == 15): hold state 2 ($104D2) wait timer
        // ($FFFF90AE) at 0 while waiting for full throttle, then release to 19.
        if (mode == 15u) {
            if (definition.objective == Objective::takeoff && definition.environment == Environment::carrier) {
                const uint16_t carrier_state = peek16(memory, 0xffff90b0u);
                if (carrier_state == 2u && (phase == Phase::ready || servicing())) {
                    seed16(memory, 0xffff90aeu, 0u);
                    board_gate_held = true;
                } else if (carrier_state == 2u && phase == Phase::accelerating) {
                    seed16(memory, 0xffff90aeu, 19u);
                }
            }
            return;
        }

        if (mode != 16u) return;

        // 2. Airbase runway sequence ($111BE / $1122E in object pool $FFFFA000..$FFFFDFFF):
        // Hold state 8 parked countdown after missile reload completes until full throttle.
        board_sequence_owner = 0u;
        for (unsigned i = 0; i < 256u; ++i) {
            const uint32_t p = 0xffffa000u + i * 64u;
            const uint8_t flags = memory.bytes.at(MainBus::memory_offset(p));
            if (!(flags & 0x80u)) continue;
            const uint32_t handler = peek32(memory, p + 16u);
            if (handler == 0x111beu || handler == 0x1122eu) {
                board_sequence_owner = p;
                const uint16_t base_state = peek16(memory, p + 40u);
                if (definition.player_landing && handler == 0x1122eu)
                    player_flare(memory, p, base_state, input);
                const uint16_t reload_pending = peek16(memory, 0xffff9100u);
                const uint8_t reload_active = memory.bytes.at(MainBus::memory_offset(0xffff90ffu));
                if (handler == 0x1122eu && base_state == 8u && !reload_pending && !reload_active) {
                    if (phase == Phase::ready || servicing()) {
                        // Hold the state-8 timer (+42) above zero so $114E8 does not advance to state 12.
                        seed16(memory, p + 42u, 30u);
                        board_gate_held = true;
                    }
                }
                break;
            }
        }
    }

    template<class Host>
    void sync_board(Host& host, const Roms& rom, const GameControls& input) {
        sync_memory(host.memory, rom, input);
    }

    GameControls demo() const {
        GameControls c;
        c.stick_x = float(std::clamp(-x * 0.18 - bank * 0.65, -1.0, 1.0));
        if (servicing()) return c;
        if (combat()) {
            c.throttle = 1.0f;
            c.cannon = true;
            c.missile = (ticks % 30u) < 4u;
            return c;
        }
        if (takeoff()) {
            c.throttle = (phase == Phase::ready && !throttle_armed) ? 0.0f : 1.0f;
            c.stick_y = speed > 72.0 ? 0.65f : 0.0f;
        } else if (refuel()) {
            c.stick_y = float(std::clamp(-height * 0.3 - pitch * 0.7, -1.0, 1.0));
            c.throttle = float(std::clamp((range - Guidance::probe_forward) * 0.045 - (speed - 140.0) * 0.09, -1.0, 1.0));
        } else {
            const double sink = height < 4.0 ? 1.7 :
                std::clamp((height - target_height()) * 0.75 + speed * definition.slope, 1.0, 10.0);
            c.stick_y = float(std::clamp((speed * definition.slope - sink) / 9.0, -1.0, 1.0));
        }
        return c;
    }
};

} // namespace CustomStage

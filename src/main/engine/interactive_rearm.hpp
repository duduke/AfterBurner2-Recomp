#pragma once

// Interactive rearm sequences. The arcade plays its airbase landing and its
// tanker hook-up as short cut scenes. With this enabled the board still draws
// and sequences them, but the player flies the critical part:
//
//  Landing  The ROM swaps the player record's handler to $111BE (fly-in) and
//           then $1122E (state 0 descent, 4 flare/rollout, 8 parked rearm,
//           12/16 departure). We own the record's lateral X (+4), stretch the
//           descent ($29C322), drive the flare sink (+36/+44) and grade the
//           touchdown. Carrier rules add a wire window: floating long is a bolter.
//  Refuel   The ROM swaps it to $16236 with selector $90E0 (0..7) and nozzle
//           pose $90F0. We own the plane's X (+4) and Y (+50/+6) against
//           turbulence, hold the nozzle short of contact until the plane is
//           lined up, and meter the reload sequencer ($9100 pending, $9103
//           delay) so missiles only flow while connected, inside a time window.
//
// Everything is written at the completed-frame boundary, the same place the
// custom-stage controller already synchronizes with the board.

#include "engine/game_controls.hpp"
#include "engine/mainbus.hpp"
#include "roms.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace Rearm {

enum class Kind : uint8_t { none, landing, refuel };
enum class Phase : uint8_t {
    idle,
    approach,       // landing fly-in, before the descent begins
    final_approach, // landing descent: steer, glide path, airspeed
    flare,          // landing: last seconds before the wheels touch
    rollout,
    service,        // parked, crew reloading
    departure,
    closing,        // refuel: tanker closing, line up under the boom
    contact,        // refuel: nozzle waits for a steady plane
    transfer,       // refuel: missiles flow while connected
    complete
};
enum class Grade : uint8_t { none, perfect, good, rough, bolter, crash, full, partial, missed };

struct Hud {
    Kind kind{Kind::none};
    Phase phase{Phase::idle};
    bool carrier{};
    // Landing guidance, all normalised to -1..1 (positive = right / high).
    float localizer{}, glideslope{}, wind{};
    float airspeed{}, airspeed_low{}, airspeed_high{};
    float sink{};          // 0..1, 1 = hard landing threshold
    // Refuel guidance: plane offset from the boom, 1 = connection breaks.
    float offset_x{}, offset_y{};
    bool connected{};
    float time_left{};     // 0..1 of the transfer window
    unsigned delivered{}, owed{};
    const char* prompt{""};
    const char* detail{""};
    Grade grade{Grade::none};
    float result_age{};    // seconds since the grade was decided
    unsigned frame{};
};

class Controller {
public:
    bool enabled = true;
    bool carrier = false; // carrier landing rules for the current run

    static constexpr double dt = 1.0 / 60.0;

    // Tuning (ROM units: record X/Y are roughly 1.5 screen pixels per unit).
    static constexpr double steer_accel = 150.0, steer_damping = 3.2;
    static constexpr double refuel_contact = 9.0, refuel_break = 17.0;
    static constexpr double transfer_window = 9.0, delivery_spacing = 1.0;
    static constexpr double descent_stretch = 2.6;
    static constexpr int flare_base_sink = 0x16, hard_sink = 0x24, crash_sink = 0x34;

    void reset() {
        kind_ = Kind::none;
        hud_ = {};
        record_ = 0;
        result_hold_ = kill_pending_ = 0;
    }

    const Hud& hud() const { return hud_; }
    Kind active() const { return kind_; }

    void sync(MainBus::Memory& memory, const Roms& rom, const GameControls& input) {
        ++frame_;
        hud_.frame = frame_;
        if (!enabled) {
            if (kind_ != Kind::none) reset();
            return;
        }
        uint32_t handler = 0;
        const uint32_t record = find(memory, handler);
        const Kind now = handler == 0x16236u ? Kind::refuel :
                         (handler == 0x111beu || handler == 0x1122eu) ? Kind::landing : Kind::none;
        if (now != Kind::none && (now != kind_ || record != record_)) begin(now, record);
        if (now == Kind::none) {
            if (kind_ != Kind::none) finish(memory);
            if (kill_pending_) collide(memory);
            age_result();
            return;
        }
        const Input in = clean(input);
        if (now == Kind::landing) landing(memory, handler, in);
        else refuel(memory, rom, in);
        age_result();
    }

    // The ROM helpers are public so tests can drive a synthetic board.
    static uint16_t peek16(const MainBus::Memory& m, uint32_t a) {
        const auto o = MainBus::memory_offset(a);
        if (o == MainBus::UNMAPPED) throw std::runtime_error("Rearm read outside work RAM");
        return uint16_t(m.bytes.at(o) << 8 | m.bytes.at(o + 1));
    }
    static void poke16(MainBus::Memory& m, uint32_t a, uint16_t v) {
        const auto o = MainBus::memory_offset(a);
        if (o == MainBus::UNMAPPED) throw std::runtime_error("Rearm write outside work RAM");
        m.bytes.at(o) = uint8_t(v >> 8);
        m.bytes.at(o + 1) = uint8_t(v);
    }
    static uint8_t peek8(const MainBus::Memory& m, uint32_t a) {
        return m.bytes.at(MainBus::memory_offset(a));
    }
    static void poke8(MainBus::Memory& m, uint32_t a, uint8_t v) {
        m.bytes.at(MainBus::memory_offset(a)) = v;
    }
    static uint32_t peek32(const MainBus::Memory& m, uint32_t a) {
        return uint32_t(peek16(m, a)) << 16 | peek16(m, a + 2);
    }
    static void poke32(MainBus::Memory& m, uint32_t a, uint32_t v) {
        poke16(m, a, uint16_t(v >> 16));
        poke16(m, a + 2, uint16_t(v));
    }

private:
    struct Input { double x{}, y{}, throttle{}; };

    Kind kind_{Kind::none};
    Hud hud_{};
    uint32_t record_{}, frame_{}, seed_{0x2468aceu};
    unsigned result_hold_{}, kill_pending_{};
    double t_{};
    // Landing state.
    double lat_{}, lat_vel_{}, wind_{}, glide_{}, glide_drift_{}, airspeed_{};
    uint32_t last_distance_{};
    bool distance_valid_{}, touched_{}, flare_seen_{}, outcome_applied_{}, service_cut_{};
    unsigned flare_frames_{}, flare_steps_{}, rollout_frames_{};
    int last_sink_{}, depth_{};
    double flare_k_{};
    // Refuel state.
    double off_x_{}, off_y_{}, vel_x_{}, vel_y_{}, window_{};
    bool transferring_{}, released_{};
    unsigned missiles_seen_{}, delivered_{}, owed_{};
    double phase_[4]{};

    static constexpr uint32_t pool_begin = 0xffffa000u;

    static Input clean(const GameControls& c) {
        auto f = [](float v) { return std::isfinite(v) ? std::clamp(double(v), -1.0, 1.0) : 0.0; };
        return {f(c.stick_x), f(c.stick_y), f(c.throttle)};
    }

    static uint32_t find(const MainBus::Memory& m, uint32_t& handler) {
        for (unsigned i = 0; i < 256u; ++i) {
            const uint32_t p = pool_begin + i * 64u;
            if (!(peek8(m, p) & 0x80u)) continue;
            const uint32_t h = peek32(m, p + 16u);
            if (h == 0x111beu || h == 0x1122eu || h == 0x16236u) { handler = h; return p; }
        }
        return 0;
    }

    double random_unit() { // xorshift, deterministic per run
        seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
        return double(seed_ & 0xffffu) / 65535.0;
    }

    double gust(double t, double scale) const {
        return scale * (0.6 * std::sin(0.83 * t + phase_[0]) + 0.3 * std::sin(2.1 * t + phase_[1]) +
                        0.1 * std::sin(4.7 * t + phase_[2]));
    }
    double gust2(double t, double scale) const {
        return scale * (0.6 * std::sin(0.71 * t + phase_[3]) + 0.3 * std::sin(1.9 * t + phase_[0] * 1.7) +
                        0.1 * std::sin(5.3 * t + phase_[1] * 0.6));
    }

    void begin(Kind kind, uint32_t record) {
        kind_ = kind;
        record_ = record;
        seed_ ^= frame_ * 2654435761u;
        for (auto& p : phase_) p = random_unit() * 6.2831853;
        hud_ = {};
        hud_.kind = kind;
        hud_.carrier = carrier && kind == Kind::landing;
        hud_.frame = frame_;
        result_hold_ = 0;
        t_ = 0;
        lat_ = lat_vel_ = glide_ = 0;
        const double side = random_unit() < 0.5 ? -1.0 : 1.0;
        wind_ = side * (0.45 + 0.55 * random_unit());
        glide_drift_ = (random_unit() - 0.5) * 0.5;
        airspeed_ = 168.0;
        distance_valid_ = touched_ = flare_seen_ = outcome_applied_ = service_cut_ = false;
        flare_frames_ = flare_steps_ = rollout_frames_ = 0;
        last_sink_ = 0;
        flare_k_ = 0;
        off_x_ = off_y_ = vel_x_ = vel_y_ = window_ = 0;
        transferring_ = released_ = connected_ = false;
        missiles_seen_ = delivered_ = owed_ = 0;
    }

    // Raise the collision the player handler polls at $98C0 ($8D60 bit 0, honoured
    // once the $8D76 grace timer is zero), so the ROM's own damage, explosion and
    // life flow run. Stops as soon as the hit is counted ($29C09A).
    void collide(MainBus::Memory& m) {
        --kill_pending_;
        if (peek16(m, 0x29c09au)) { kill_pending_ = 0; return; }
        if (peek16(m, 0x29c09cu) != 0x10u) return;
        poke16(m, 0xffff8d76u, 0u);
        poke8(m, 0xffff8d60u, uint8_t(peek8(m, 0xffff8d60u) | 1u));
    }

    void finish(MainBus::Memory&) {
        kind_ = Kind::none;
        record_ = 0;
        if (hud_.grade == Grade::none) hud_ = {};
        else { hud_.phase = Phase::complete; result_hold_ = 1; }
    }

    void age_result() {
        if (hud_.grade != Grade::none) hud_.result_age += float(dt);
        if (kind_ == Kind::none && hud_.grade != Grade::none && hud_.result_age > 4.0f) hud_ = {};
    }

    void decide(Grade grade, const char* prompt, const char* detail) {
        if (hud_.grade != Grade::none) return;
        hud_.grade = grade;
        hud_.prompt = prompt;
        hud_.detail = detail;
        hud_.result_age = 0;
    }

    // ---------------------------------------------------------------- landing
    void landing(MainBus::Memory& m, uint32_t handler, const Input& in) {
        t_ += dt;
        const uint32_t p = record_;
        const uint16_t state = handler == 0x1122eu ? peek16(m, p + 40u) : 0xffffu;
        // +27 bit 0 is the wheels-down flag only in state 4 ($11360); state 0
        // sets it too ($1128A) while still descending.
        const bool on_ground = handler == 0x1122eu && (state >= 8u || (state == 4u && (peek8(m, p + 27u) & 1u)));
        const bool carrier_rules = hud_.carrier;

        // Airspeed: throttle sets the target; the approach starts hot.
        const double target = 140.0 + 34.0 * in.throttle;
        if (!touched_) airspeed_ += (target - airspeed_) * std::min(1.0, 0.9 * dt);
        else airspeed_ = std::max(0.0, airspeed_ - 38.0 * dt);
        hud_.airspeed = float(airspeed_);
        hud_.airspeed_low = carrier_rules ? 126.f : 120.f;
        hud_.airspeed_high = carrier_rules ? 146.f : 152.f;
        hud_.wind = float(wind_);

        if (handler == 0x111beu) {
            // $111E2 waits for X:Y == 0:$6C before it commits $1122E, so X
            // stays the ROM's here. The pilot sees the briefing only.
            hud_.phase = Phase::approach;
            hud_.prompt = carrier_rules ? "CARRIER IN SIGHT" : "AIRBASE IN SIGHT";
            hud_.detail = "SLOW DOWN AND LINE UP";
            return;
        }

        const bool airborne = !touched_ && !on_ground;
        if (airborne) {
            // Crosswind and gusts push the plane; the stick pushes back.
            lat_vel_ += (in.x * steer_accel - lat_vel_ * steer_damping) * dt;
            lat_ += (lat_vel_ + wind_ * 16.0 + gust(t_, 10.0)) * dt;
            lat_ = std::clamp(lat_, -96.0, 96.0);
        } else if (!outcome_applied_ || hud_.grade != Grade::crash) {
            lat_ += (0.0 - lat_) * std::min(1.0, 1.6 * dt); // roll back to the centreline
        }
        poke16(m, p + 4u, uint16_t(int16_t(std::lround(lat_))));
        const double lane = carrier_rules ? 20.0 : 34.0;
        hud_.localizer = float(std::clamp(lat_ / lane, -1.5, 1.5));

        if (state == 0u) {
            hud_.phase = Phase::final_approach;
            // Stretch the ROM descent so there is time to fly it.
            const uint32_t distance = peek32(m, 0x29c322u);
            if (distance_valid_ && int32_t(distance) < int32_t(last_distance_)) {
                const double step = double(int32_t(last_distance_) - int32_t(distance));
                const uint32_t slowed = uint32_t(int32_t(last_distance_) - int32_t(std::lround(step / descent_stretch)));
                poke32(m, 0x29c322u, slowed);
                last_distance_ = slowed;
            } else {
                last_distance_ = distance;
            }
            distance_valid_ = true;
            glide_ += (in.y * 0.62 + glide_drift_ + 0.18 * std::sin(0.9 * t_ + phase_[2])) * dt;
            glide_ = std::clamp(glide_, -1.6, 1.6);
            hud_.glideslope = float(std::clamp(glide_, -1.5, 1.5));
            hud_.prompt = "FOLLOW THE GLIDE PATH";
            hud_.detail = std::abs(lat_) > lane * 0.6 ? (lat_ > 0 ? "STEER LEFT" : "STEER RIGHT") :
                          airspeed_ > hud_.airspeed_high ? "TOO FAST - THROTTLE BACK" :
                          airspeed_ < hud_.airspeed_low ? "TOO SLOW - ADD POWER" :
                          glide_ > 0.45 ? "TOO HIGH - NOSE DOWN" :
                          glide_ < -0.45 ? "TOO LOW - NOSE UP" : "ON GLIDE PATH";
            return;
        }

        if (state == 4u) {
            if (airborne) {
                flare(m, p, in);
                hud_.phase = Phase::flare;
                hud_.prompt = "FLARE";
                hud_.detail = in.y > 0.2 ? "EASE IT DOWN" : "PULL BACK GENTLY";
                return;
            }
            if (!touched_) touchdown(m, p);
            hud_.phase = Phase::rollout;
            ++rollout_frames_;
            if ((hud_.grade == Grade::bolter || hud_.grade == Grade::crash) && rollout_frames_ == 24u && !outcome_applied_) {
                // Touch and go: hand the record straight to the parked state with a
                // one-tick timer, so $114E8 starts the departure and no crew reload runs.
                poke16(m, p + 40u, 8u);
                poke8(m, p + 26u, 1u);
                outcome_applied_ = true;
            }
            return;
        }

        if (state == 8u) {
            hud_.phase = Phase::service;
            if (hud_.grade == Grade::bolter || hud_.grade == Grade::crash) return;
            // A rough landing only earns half a reload.
            if (hud_.grade == Grade::rough && !service_cut_ && peek8(m, 0xffff90ffu) && int16_t(peek16(m, 0xffff9100u)) > 1) {
                poke16(m, 0xffff9100u, uint16_t(peek16(m, 0xffff9100u) / 2u));
                service_cut_ = true;
            }
            if (hud_.result_age > 3.0f) { hud_.prompt = "REARMING"; hud_.detail = "CREW ON DECK"; }
            return;
        }

        hud_.phase = Phase::departure;
        if (hud_.result_age <= 3.0f) return;
        if (state != 12u) { hud_.prompt = ""; hud_.detail = ""; return; }
        if (hud_.grade == Grade::crash) { hud_.prompt = "AIRCRAFT DAMAGED"; hud_.detail = "LOSING CONTROL"; }
        else if (hud_.grade == Grade::bolter) { hud_.prompt = "WAVE OFF"; hud_.detail = "NO REARM THIS PASS"; }
        else { hud_.prompt = "CLEARED FOR TAKEOFF"; hud_.detail = ""; }
    }

    // Player flare. In state 4 the ROM subtracts $40 from +36 each frame and
    // derives screen height +44 from it; touchdown is +36 <= $20 ($11372).
    // We undo the ROM step and apply a slower, stick-scaled one instead.
    void flare(MainBus::Memory& m, uint32_t p, const Input& in) {
        const auto q_of = [](int depth) { return double(depth - 0x20) / 64.0; };
        const int cur = int(int16_t(peek16(m, p + 36u)));
        if (!flare_seen_) {
            flare_seen_ = true;
            depth_ = cur + 0x40;
            const double q0 = q_of(depth_);
            flare_k_ = q0 > 0.0 ? double(int16_t(peek16(m, p + 44u))) / q0 : 0.0;
        }
        ++flare_frames_;
        // Neutral stick sinks at flare_base_sink per ROM step; pulling back slows
        // it, pushing roughly doubles it, and arriving low adds extra sink.
        const double wanted = flare_base_sink * (1.0 - 0.85 * in.y) + std::max(0.0, glide_ * -10.0);
        const int delta = std::clamp(int(std::lround(wanted)), 2, 0x60);
        hud_.sink = float(std::clamp(double(delta) / hard_sink, 0.0, 1.5));
        // The handler does not run every frame: only replace a step it took.
        const int rom_steps = (depth_ - cur) / 0x40;
        if (rom_steps <= 0) return;
        depth_ = std::clamp(depth_ - delta * rom_steps, 0x10, 0x600);
        // $11368 only touches down at exactly <= $20 before it subtracts $40;
        // anything between would wrap negative and skip the rollout ($114AA).
        if (depth_ < 0x60) depth_ = 0x20;
        // Floating forever is not an option: the deck (or runway) comes up anyway.
        if (flare_frames_ > 300u) depth_ = 0x20;
        poke16(m, p + 36u, uint16_t(depth_));
        flare_steps_ += unsigned(rom_steps);
        const double q = std::max(0.0, q_of(depth_));
        poke16(m, p + 44u, uint16_t(int16_t(std::lround(flare_k_ * q))));
        last_sink_ = delta;
        // Floating keeps the plane off the deck past the wires.
        if (flare_steps_ > 70u) glide_ += 0.9 * dt;
    }

    void touchdown(MainBus::Memory& m, uint32_t p) {
        touched_ = true;
        const bool c = hud_.carrier;
        const double lat = std::abs(lat_);
        const double good = c ? 10.0 : 16.0, rough = c ? 20.0 : 34.0, off = c ? 28.0 : 46.0;
        const bool fast = airspeed_ > hud_.airspeed_high, slow = airspeed_ < hud_.airspeed_low;
        if (lat > off) {
            decide(Grade::crash, c ? "OFF THE DECK" : "OFF THE RUNWAY", "AIRCRAFT LOST");
        } else if (last_sink_ >= crash_sink) {
            decide(Grade::crash, "HARD IMPACT", "AIRCRAFT LOST");
        } else if (c && glide_ < -0.75) {
            decide(Grade::crash, "RAMP STRIKE", "AIRCRAFT LOST");
        } else if (c && (glide_ > 0.6 || flare_steps_ > 80u || fast)) {
            decide(Grade::bolter, "BOLTER", "MISSED THE WIRES - NO REARM");
        } else {
            unsigned issues = 0;
            issues += lat > good;
            issues += last_sink_ >= hard_sink;
            issues += std::abs(glide_) > 0.55;
            issues += fast || slow;
            issues += lat > rough;
            if (issues == 0) decide(Grade::perfect, "PERFECT LANDING", c ? "TRAPPED ON THE 3 WIRE" : "FULL REARM");
            else if (issues == 1) decide(Grade::good, "GOOD LANDING", "FULL REARM");
            else decide(Grade::rough, "ROUGH LANDING", "HALF REARM");
        }
        // A wrecked landing bounces straight back into the air; the damage is
        // dealt once the record is the normal player again (see sync()).
        if (hud_.grade == Grade::crash) kill_pending_ = 240u;
    }

    // ----------------------------------------------------------------- refuel
    static int rom_word(const Roms& rom, uint32_t a) {
        if (!rom.maincpu.rom || a + 2u > rom.maincpu.length) return 0;
        return int(int16_t(uint16_t(rom.maincpu.rom[a] << 8 | rom.maincpu.rom[a + 1])));
    }

    void refuel(MainBus::Memory& m, const Roms& rom, const Input& in) {
        t_ += dt;
        const uint32_t p = record_;
        const uint16_t state = peek16(m, 0xffff90e0u) & 7u;
        const uint16_t pose = peek16(m, 0xffff90f0u);
        const bool flying = state <= 3u;

        if (flying) {
            const double turbulence = state >= 3u ? 1.0 : 0.6;
            vel_x_ += (in.x * steer_accel - vel_x_ * steer_damping) * dt;
            vel_y_ += (in.y * steer_accel - vel_y_ * steer_damping) * dt;
            off_x_ += (vel_x_ + gust(t_, 16.0 * turbulence)) * dt;
            off_y_ += (vel_y_ + gust2(t_, 11.0 * turbulence)) * dt;
            off_x_ = std::clamp(off_x_, -60.0, 60.0);
            off_y_ = std::clamp(off_y_, -40.0, 40.0);
            // $16A02..$16A46 slews +4/+50 toward a per-pose target; write the
            // target plus our offset so the ROM's own slew keeps it there.
            const int code = std::abs((int(int16_t(peek16(m, 0xffff90e4u))) >> 3) - 0x200) >> 5;
            int tx = 0, ty = -5;
            if (code >= 10 && pose < 20u) {
                tx = rom_word(rom, 0x16c36u + pose * 4u);
                ty = rom_word(rom, 0x16c36u + pose * 4u + 2u);
            }
            // Screen Y grows downward, so a raised stick lowers +50.
            poke16(m, p + 4u, uint16_t(int16_t(tx + int(std::lround(off_x_)))));
            poke16(m, p + 50u, uint16_t(int16_t(ty - int(std::lround(off_y_)))));
        }

        const double err = std::hypot(off_x_, off_y_ * 1.3);
        if (connected_ && err > refuel_break) connected_ = false;
        else if (!connected_ && err < refuel_contact) connected_ = true;
        hud_.offset_x = float(std::clamp(off_x_ / refuel_break, -2.0, 2.0));
        hud_.offset_y = float(std::clamp(off_y_ / refuel_break, -2.0, 2.0));
        hud_.connected = connected_;

        const unsigned missiles = peek16(m, 0xffff8d66u);
        if (transferring_ && missiles > missiles_seen_) delivered_ += missiles - missiles_seen_;
        const bool delivered_now = missiles != missiles_seen_;
        missiles_seen_ = missiles;

        if (state <= 3u && pose < 10u) {
            hud_.phase = Phase::closing;
            hud_.prompt = "MOVE UNDER THE BOOM";
            hud_.detail = connected_ ? "HOLD POSITION" : "MATCH THE MARKERS";
        }
        // Pose 9 is the last nozzle frame before the receptacle; when its timer
        // runs out $165A6 couples ($9102) and starts pose 10. Wait for a steady plane.
        if (pose == 9u && !connected_) {
            if (int16_t(peek16(m, 0xffff90f2u)) < 2) poke16(m, 0xffff90f2u, 2u);
            hud_.phase = Phase::contact;
            hud_.prompt = "HOLD STEADY";
            hud_.detail = "WAITING FOR CONTACT";
        }
        if (state == 3u && pose == 10u) {
            if (!transferring_) {
                transferring_ = true;
                delivered_ = 0;
            }
            const unsigned pending = int16_t(peek16(m, 0xffff9100u)) > 0 ? peek16(m, 0xffff9100u) : 0u;
            owed_ = std::max(owed_, pending + delivered_);
            window_ += dt;
            const bool time_up = window_ >= transfer_window;
            if (!time_up && pending > 0) {
                // $165CE counts $90F2 down to undock; keep the tanker on station.
                if (int16_t(peek16(m, 0xffff90f2u)) < 3) poke16(m, 0xffff90f2u, 3u);
                // $16CE0 only delivers once the signed delay byte $9103 reaches 1.
                if (!connected_) poke8(m, 0xffff9103u, 0x80u);
                else if (delivered_now) poke8(m, 0xffff9103u, uint8_t(int8_t(-int(delivery_spacing))));
                else if (int8_t(peek8(m, 0xffff9103u)) < -int(delivery_spacing)) poke8(m, 0xffff9103u, 0u);
            } else if (pending > 0 && !released_) {
                poke16(m, 0xffff9100u, 0u); // whatever did not transfer is lost
                released_ = true;
            }
            hud_.phase = Phase::transfer;
            hud_.time_left = float(std::clamp(1.0 - window_ / transfer_window, 0.0, 1.0));
            hud_.prompt = connected_ ? "CONTACT" : "DISCONNECTED";
            hud_.detail = connected_ ? "TRANSFERRING MISSILES" : "GET BACK UNDER THE BOOM";
        }
        hud_.delivered = delivered_;
        hud_.owed = owed_;
        if (state >= 4u) {
            hud_.phase = Phase::complete;
            if (transferring_ && hud_.grade == Grade::none) {
                if (delivered_ >= owed_) decide(Grade::full, "REARM COMPLETE", "FULL LOAD");
                else if (delivered_ > 0u) decide(Grade::partial, "PARTIAL REARM", "TIME EXPIRED");
                else decide(Grade::missed, "NO CONTACT", "NO MISSILES TRANSFERRED");
            }
        }
    }

    bool connected_{};
};

} // namespace Rearm

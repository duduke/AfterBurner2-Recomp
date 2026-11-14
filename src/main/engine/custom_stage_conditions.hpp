#pragma once

// Procedural conditions for custom stages: the mission, art and behavior
// interfaces of a stage file (mission / conditions / formations / entry /
// mutators). The board keeps flying the stage; this director adds:
//
//  - native squadrons laid out from a seed (V, line, echelon, diamond) that
//    enter from the front, overtake from behind, cross from either side or
//    dive in, fire tracers, and die to the player's cannon and missiles
//  - naval targets for sea-skimming assaults, putting up flak
//  - night with sweeping searchlights; held in a beam, the plane draws a
//    flak barrage; ambient flak; rain, lightning, fog, turbulence and wind
//  - an entry maneuver at the start, a mission tracker with a bonus, and
//    run mutators (applied to the definition when it loads)
//
// It talks to the board only through RAM the arcade already uses (see
// custom_stage_boss.hpp): the player record for aim and hits, $8D66 for
// missile launches, the $8D60 collision request for damage, $29C006 to hold
// the stage while a mission runs and $8884 for the score. Input shaping
// (turbulence, wind, entry, afterburner) is applied before the board reads
// the controls, so the arcade's own flight model does the flying.

#include "engine/custom_stage_json.hpp"
#include "engine/game_controls.hpp"
#include "engine/interactive_rearm.hpp"
#include "engine/mainbus.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace CustomStage {

class ConditionsDirector {
public:
    enum class UnitKind : uint8_t { fighter, ship };
    struct Unit {
        UnitKind kind{};
        double x{}, y{}, scale{1}, angle{};
        float flash{};
        bool alive{true}, locked{};
    };
    struct Threat {
        enum class Kind : uint8_t { tracer, flak, missile, burst } kind{};
        double x{}, y{}, x2{}, y2{}, radius{};
        float progress{};
    };
    struct Disk { double x{}, y{}, radius{}; uint8_t r{}, g{}, b{}; float alpha{1}; };
    struct Beam { double base_x{}, angle{}, width{}; bool on_player{}; };
    struct View {
        bool active{};
        double roll{};
        double night{}, fog{}, rain{};
        float lightning{};                 // flash brightness, 0..1
        std::vector<std::pair<double, double>> bolt; // jagged bolt in the sky while flashing
        std::vector<Beam> beams;
        std::vector<Unit> units;           // far to near
        std::vector<Disk> disks;           // explosions and smoke (behind sprites)
        std::vector<Threat> threats;       // above everything
        double aim_x{}, aim_y{};
        bool sea{};
        double time{};
        // Mission tracker.
        bool mission{};
        const std::string* briefing{};
        const std::string* detail{};
        unsigned kills{}, target{};
        float mission_time{}, time_limit{}, briefing_age{9.f}, result_age{9.f};
        const char* result{""};
        const char* alert{""};
        float alert_age{9.f}, hit_flash{};
    };

    static constexpr double dt = 1.0 / 60.0;

    explicit ConditionsDirector(const Definition& definition) : def_(definition) { reset(); }

    void reset() {
        view_ = {};
        units_.clear();
        state_.clear();
        tracers_.clear();
        flak_.clear();
        bursts_.clear();
        effects_.clear();
        missiles_.clear();
        beams_.clear();
        rng_ = def_.formations ? def_.formations->seed * 2654435761u + 1u : 0x9e3779b9u;
        t_ = mission_t_ = 0;
        flight_t_ = -1;
        next_wave_ = 0;
        wave_clock_ = 0;
        kills_ = 0;
        spotted_ = spotted_for_ = 0;
        flak_clock_ = 2.0;
        lightning_clock_ = 3.0 + rand() * 4.0;
        lightning_ = 0;
        grace_ = 0;
        kill_ = 0;
        missiles_seen_ = 0;
        result_ = "";
        result_age_ = 9.f;
        briefing_age_ = 9.f;
        alert_ = "";
        alert_age_ = 9.f;
        hit_flash_ = 0;
        mission_done_ = false;
        gust_ = 0;
        for (unsigned i = 0; i < def_.conditions.searchlights; ++i)
            beams_.push_back({40.0 + 240.0 * (double(i) + 0.5) / double(def_.conditions.searchlights), rand() * 6.28, 0.35 + rand() * 0.4});
        plan_waves();
    }

    const View& view() const { return view_; }

    // Before the board reads the controls: the run's handling.
    void shape_input(GameControls& c) {
        if (flight_t_ < 0) return;
        const auto& k = def_.conditions;
        // Entry maneuver for the first seconds of the stage.
        const double e = flight_t_;
        if (e < 2.4) {
            if (def_.entry == "dive") c.stick_y = float(e < 1.2 ? 0.9 : -0.6);
            else if (def_.entry == "climb") c.stick_y = float(e < 1.2 ? -0.9 : 0.5);
            else if (def_.entry == "roll") c.stick_x = float(e < 1.6 ? 1.0 : -0.4);
        }
        // Turbulence and crosswind push on the stick; the player fights back.
        if (k.turbulence > 0 || k.wind != 0) {
            const double jolt = k.turbulence * (0.45 * std::sin(t_ * 7.3) * std::sin(t_ * 1.7) + 0.35 * gust_);
            c.stick_x = float(std::clamp(double(c.stick_x) + jolt + k.wind * 0.25, -1.0, 1.0));
            c.stick_y = float(std::clamp(double(c.stick_y) + k.turbulence * 0.3 * std::sin(t_ * 5.1 + 1.0), -1.0, 1.0));
        }
        if (std::find(def_.mutators.begin(), def_.mutators.end(), "afterburner") != def_.mutators.end())
            c.throttle = 1.0f;
    }

    void sync(MainBus::Memory& m, uint16_t stage_tick, const GameControls& input) {
        using R = Rearm::Controller;
        t_ += dt;
        hit_flash_ = std::max(0.f, hit_flash_ - float(dt) * 1.5f);
        alert_age_ += float(dt);
        result_age_ += float(dt);
        briefing_age_ += float(dt);
        if (grace_ > 0) grace_ -= dt;
        if (int(t_ * 60) % 40 == 0) gust_ = rand() * 2.0 - 1.0;

        const bool flying = R::peek16(m, 0x29c09cu) == 0x10u;
        view_.active = flying;
        if (!flying) return;
        if (flight_t_ < 0) { flight_t_ = 0; briefing_age_ = 0; }
        flight_t_ += dt;

        const auto& k = def_.conditions;
        const Player player = find_player(m);
        const int16_t cam_x = int16_t(R::peek16(m, 0x29c08au)), cam_y = int16_t(R::peek16(m, 0x29c088u));
        const double roll = double(int8_t(R::peek16(m, 0x29c09eu) & 0xffu)) * 6.283185307179586 / 256.0;
        view_.roll = roll;
        const auto unroll = [&](double x, double y) {
            const double dx = x - 160.0, dy = y - 112.0, c = std::cos(roll), s = std::sin(roll);
            return std::pair{160.0 + dx * c - dy * s, 112.0 + dx * s + dy * c};
        };
        const auto [aim_x, aim_y] = unroll(player.x, player.y - 10.0);
        const auto [px, py] = unroll(player.x, player.y);
        view_.aim_x = aim_x;
        view_.aim_y = aim_y;
        const double pan_x = -cam_x * 1.2, pan_y = -cam_y * 0.4;

        // Mission clock and the stage hold.
        const bool mission_running = def_.mission && !mission_done_;
        if (mission_running && def_.formations && stage_tick >= def_.formations->start_tick) mission_t_ += dt;
        if (mission_running && def_.mission->hold_tick && stage_tick >= *def_.mission->hold_tick)
            R::poke32(m, 0x29c006u, uint32_t(*def_.mission->hold_tick) << 4);

        // Waves come in on the plan once the formations start.
        if (def_.formations && stage_tick >= def_.formations->start_tick) {
            wave_clock_ += dt;
            while (next_wave_ < plan_.size() && wave_clock_ >= plan_[next_wave_].time) spawn(plan_[next_wave_++]);
        }

        // Fly the squadrons.
        int locked = -1;
        double best = 1e9;
        for (size_t i = 0; i < units_.size(); ++i) {
            auto& u = units_[i];
            auto& st = state_[i];
            if (!u.alive) continue;
            st.t += dt;
            fly(u, st, pan_x, pan_y);
            u.flash = std::max(0.f, u.flash - float(dt) * 4.f);
            u.locked = false;
            const bool in_view = u.x > -40 && u.x < 360 && u.y > -40 && u.y < 264;
            if (st.done || (!in_view && st.t > 2.0)) { u.alive = false; continue; }
            const double reach = std::max(14.0, 22.0 * u.scale);
            const double d = std::hypot(u.x - aim_x, u.y - aim_y);
            if (in_view && d < reach && d < best) { best = d; locked = int(i); }
            // Fire at the player while in front and coming on.
            st.fire -= dt;
            const double aggression = def_.formations ? def_.formations->aggression : 0.5;
            if (player.found && grace_ <= 0 && in_view && st.fire <= 0 && u.scale > 0.35 && u.scale < 1.6) {
                st.fire = 2.4 - 1.6 * aggression + rand() * 1.5;
                if (u.kind == UnitKind::fighter && rand() < 0.4 + 0.6 * aggression) {
                    const double dx = px - u.x, dy = py - u.y, dd = std::max(1.0, std::hypot(dx, dy));
                    for (int b = 0; b < 2; ++b)
                        tracers_.push_back({u.x, u.y, dx / dd * 130.0, dy / dd * 130.0, -0.12 * b});
                    alert("GUNFIRE");
                } else if (u.kind == UnitKind::ship) {
                    flak_.push_back({px + (rand() - 0.5) * 70, py + (rand() - 0.5) * 36, 0.0, 1.3});
                    alert("FLAK");
                }
            }
        }
        if (locked >= 0) units_[size_t(locked)].locked = true;

        // Cannon and the arcade's own missiles.
        if (input.cannon && locked >= 0 && int(t_ * 60) % 3 == 0) damage(size_t(locked), 1);
        const unsigned missiles = R::peek16(m, 0xffff8d66u);
        if (missiles < missiles_seen_ && missiles_seen_ - missiles < 4u && locked >= 0)
            missiles_.push_back({px, py, locked, 0.0});
        missiles_seen_ = missiles;
        step_missiles();

        // Searchlights sweep; a beam held on the plane calls in the guns.
        bool lit = false;
        for (auto& b : beams_) {
            b.angle += dt * (0.35 + 0.15 * std::sin(t_ * 0.3 + b.base_x));
            const double a = -1.5708 + 0.75 * std::sin(b.angle);   // pointing up, sweeping
            const double bx = b.base_x + pan_x * 0.6, by = 250.0;
            const double dx = px - bx, dy = py - by;
            const double along = dx * std::cos(a) + dy * std::sin(a);
            const double across = std::abs(-dx * std::sin(a) + dy * std::cos(a));
            const bool on = player.found && along > 0 && across < 6.0 + along * b.width * 0.18;
            lit = lit || on;
            view_beam(b, bx, a, on);
        }
        spotted_for_ = lit ? spotted_for_ + dt : std::max(0.0, spotted_for_ - dt * 2);
        if (spotted_for_ > 0.7 && spotted_ <= 0) { spotted_ = 3.0; alert("SPOTTED"); }
        if (spotted_ > 0) {
            spotted_ -= dt;
            if (int(spotted_ * 60) % 22 == 0 && player.found && grace_ <= 0)
                flak_.push_back({px + (rand() - 0.5) * 50, py + (rand() - 0.5) * 26, 0.0, 1.1});
        }
        // Ambient flak: bursts across the sky, now and then close enough to hurt.
        if (k.flak > 0) {
            flak_clock_ -= dt * (0.4 + 1.6 * k.flak);
            if (flak_clock_ <= 0) {
                flak_clock_ = 1.0 + rand();
                const bool aimed = player.found && grace_ <= 0 && rand() < 0.35 * k.flak;
                if (aimed) flak_.push_back({px + (rand() - 0.5) * 90, py + (rand() - 0.5) * 50, 0.0, 1.4});
                else bursts_.push_back({30 + rand() * 260, 20 + rand() * 120, 10 + rand() * 10, 0.f});
            }
        }
        // Lightning.
        if (k.lightning > 0) {
            lightning_clock_ -= dt * k.lightning;
            if (lightning_clock_ <= 0) {
                lightning_clock_ = 3.0 + rand() * 6.0;
                lightning_ = 1.0;
                bolt_.clear();
                double x = 40 + rand() * 240, y = -10;
                while (y < 120) { bolt_.push_back({x, y}); x += (rand() - 0.5) * 26; y += 8 + rand() * 12; }
            }
        }
        lightning_ = std::max(0.0, lightning_ - dt * 3.0);

        step_threats(m, px, py, player.found);
        step_mission(m);

        // Publish.
        view_.night = k.night * (1.0 - 0.7 * lightning_);
        view_.fog = k.fog;
        view_.rain = k.rain;
        view_.lightning = float(lightning_);
        view_.bolt = lightning_ > 0.3 ? bolt_ : std::vector<std::pair<double, double>>{};
        view_.sea = k.sea;
        view_.time = t_;
        view_.units.clear();
        for (const auto& u : units_) if (u.alive) view_.units.push_back(u);
        std::sort(view_.units.begin(), view_.units.end(), [](const Unit& a, const Unit& b) { return a.scale < b.scale; });
        view_.disks.clear();
        step_effects(view_.disks);
        view_.mission = bool(def_.mission);
        if (def_.mission) {
            view_.briefing = &def_.mission->briefing;
            view_.detail = &def_.mission->detail;
            view_.target = def_.mission->type == MissionDef::Type::destroy ? def_.mission->target : 0u;
            view_.time_limit = float(def_.mission->time_limit);
        }
        view_.kills = kills_;
        view_.mission_time = float(mission_t_);
        view_.briefing_age = briefing_age_;
        view_.result = result_;
        view_.result_age = result_age_;
        view_.alert = alert_;
        view_.alert_age = alert_age_;
        view_.hit_flash = hit_flash_;
    }

private:
    struct Player { double x{160}, y{120}; bool found{}; };
    struct Wave { double time; std::string pattern, entry; unsigned size, ships; };
    struct Flight { double t{}; std::string entry; double ox{}, oy{}, delay{}; double fire{2.0}; int hp{3}; bool done{}; double start_x{}, start_y{}; };
    struct Tracer { double x, y, vx, vy, life; };
    struct Flak { double x, y, t, fuse; };
    struct Burst { double x, y, r; float age; };
    struct Effect { double x, y, r, life, cr, cg, cb, vy; float age; };
    struct Missile { double x, y; int target; double t; };

    const Definition& def_;
    View view_;
    std::vector<Wave> plan_;
    std::vector<Unit> units_;
    std::vector<Flight> state_;
    std::vector<Tracer> tracers_;
    std::vector<Flak> flak_;
    std::vector<Burst> bursts_;
    std::vector<Effect> effects_;
    std::vector<Missile> missiles_;
    std::vector<Beam> beams_;
    std::vector<std::pair<double, double>> bolt_;
    uint32_t rng_{};
    double t_{}, flight_t_{-1}, mission_t_{}, wave_clock_{}, spotted_{}, spotted_for_{}, flak_clock_{}, lightning_clock_{}, lightning_{}, grace_{}, gust_{};
    size_t next_wave_{};
    unsigned kills_{}, missiles_seen_{}, kill_{};
    const char* result_{""};
    const char* alert_{""};
    float result_age_{9.f}, briefing_age_{9.f}, alert_age_{9.f}, hit_flash_{};
    bool mission_done_{};

    double rand() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return double(rng_ & 0xffffu) / 65535.0;
    }

    void alert(const char* text) {
        if (alert_age_ < 0.8f && alert_ == text) return;
        alert_ = text;
        alert_age_ = 0;
    }

    // The whole run's waves, decided up front from the seed.
    void plan_waves() {
        plan_.clear();
        if (!def_.formations) return;
        const auto& f = def_.formations.value();
        for (unsigned w = 0; w < f.waves; ++w) {
            const auto& pattern = f.patterns[size_t(rand() * f.patterns.size()) % f.patterns.size()];
            const auto& entry = f.entries[size_t(rand() * f.entries.size()) % f.entries.size()];
            const unsigned size = f.min_size + unsigned(rand() * double(f.max_size - f.min_size + 1)) % (f.max_size - f.min_size + 1);
            plan_.push_back({1.0 + w * f.spacing * (0.8 + 0.4 * rand()), pattern, entry, size, f.ships});
        }
    }

    static std::pair<double, double> slot(const std::string& pattern, unsigned i) {
        const double k = double((i + 1) / 2) * ((i % 2) ? -1.0 : 1.0);
        if (pattern == "line") return {k * 30.0, 0.0};
        if (pattern == "echelon") return {double(i) * 24.0 - 36.0, double(i) * 8.0};
        if (pattern == "diamond") {
            static const double d[][2] = {{0, -16}, {-26, 0}, {26, 0}, {0, 16}, {-52, 0}, {52, 0}, {0, -32}, {0, 32}};
            return {d[i % 8][0], d[i % 8][1]};
        }
        return {k * 26.0, std::abs(k) * 9.0}; // v
    }

    void spawn(const Wave& w) {
        for (unsigned i = 0; i < w.size; ++i) {
            const auto [ox, oy] = slot(w.pattern, i);
            units_.push_back({UnitKind::fighter, -100, -100, 0.1, 0, 0.f, true, false});
            state_.push_back({0.0, w.entry, ox, oy, i * 0.12, 2.0 + rand() * 2.0, 3, false, 0, 0});
        }
        for (unsigned i = 0; i < w.ships; ++i) {
            units_.push_back({UnitKind::ship, -100, -100, 0.1, 0, 0.f, true, false});
            state_.push_back({0.0, "sea", (i % 2 ? -1.0 : 1.0) * (40.0 + 50.0 * double(i / 2)), 0.0, i * 0.6, 1.5 + rand(), 5, false, 0, 0});
        }
        static const char* names[] = {"BANDITS AHEAD", "BANDITS ON YOUR SIX", "BANDITS LEFT", "BANDITS RIGHT", "BANDITS HIGH"};
        const int which = w.entry == "rear" ? 1 : w.entry == "left" ? 2 : w.entry == "right" ? 3 : w.entry == "dive" ? 4 : 0;
        alert(names[which]);
    }

    // Scripted paths in screen space; scale stands for distance.
    void fly(Unit& u, Flight& st, double pan_x, double pan_y) {
        const double t = std::max(0.0, st.t - st.delay);
        const double ease = [](double k) { k = std::clamp(k, 0.0, 1.0); return k * k * (3 - 2 * k); }(t / 3.0);
        double x = 160, y = 80, s = 0.6, bank = 0;
        if (u.kind == UnitKind::ship) {
            // Ships come up out of the haze on the water and pass below.
            const double k = t / 9.0;
            s = 0.15 + 1.9 * k * k;
            x = 160 + st.ox * s;
            y = 128 + 120 * k * k;
            if (k > 1.0) st.done = true;
        } else if (st.entry == "front") {
            s = 0.12 + 0.75 * ease;
            x = 160 + st.ox * s * 1.4 + 50 * std::sin(t * 0.7) * ease;
            y = 96 + st.oy * s - 22 * ease;
            if (t > 6.5) { const double k = (t - 6.5) / 1.5; s += 1.6 * k; x += (st.ox >= 0 ? 1 : -1) * 220 * k; y += 60 * k; bank = (st.ox >= 0 ? 0.6 : -0.6) * std::min(1.0, k * 2); }
            if (t > 8.0) st.done = true;
        } else if (st.entry == "rear") {
            // Overtakes from behind: huge, then shrinking ahead.
            s = 2.4 - 1.8 * ease;
            x = 160 + st.ox * s * 1.2 + 30 * std::sin(t * 0.9) * ease;
            y = 250 - (250 - 74) * ease + st.oy * s;
            if (t > 7.0) { const double k = (t - 7.0) / 2.0; s *= std::max(0.05, 1.0 - k); y -= 30 * k; }
            if (t > 9.0) st.done = true;
        } else if (st.entry == "left" || st.entry == "right") {
            const double side = st.entry == "left" ? -1.0 : 1.0;
            const double k = std::clamp(t / 6.0, 0.0, 1.0);
            s = 0.55 + 0.25 * std::sin(k * 3.14);
            x = 160 + side * (230 - 460 * k) + st.ox * 0.4;
            y = 70 + st.oy * s + 20 * std::sin(k * 3.14);
            bank = -side * 0.35;
            if (k >= 1.0) st.done = true;
        } else { // dive
            const double k = std::clamp(t / 5.0, 0.0, 1.0);
            s = 0.5 + 0.9 * k;
            x = 160 + st.ox * s + 40 * std::sin(k * 4);
            y = -50 + 300 * k * k + st.oy * s;
            if (k >= 1.0) st.done = true;
        }
        const double prev_x = u.x;
        u.x = x + pan_x * (u.kind == UnitKind::ship ? 0.6 : 1.0);
        u.y = y + pan_y;
        u.scale = s;
        const double vx = (u.x - prev_x) / dt;
        u.angle = bank + (std::abs(vx) < 400 ? std::clamp(vx * 0.0025, -0.5, 0.5) : 0.0);
    }

    void damage(size_t i, int amount) {
        auto& u = units_[i];
        auto& st = state_[i];
        if (!u.alive) return;
        st.hp -= amount;
        u.flash = 1.f;
        effects_.push_back({u.x, u.y, 3, 0.15, 255, 240, 180, 0, 0.f});
        if (st.hp <= 0) {
            u.alive = false;
            ++kills_;
            for (int k = 0; k < 4; ++k)
                effects_.push_back({u.x + (rand() - 0.5) * 14 * u.scale, u.y + (rand() - 0.5) * 8 * u.scale, (10 + rand() * 12) * std::max(0.5, u.scale), 0.7, 255, 170, 60, 0, 0.f});
            effects_.push_back({u.x, u.y, 8 * std::max(0.5, u.scale), 1.2, 54, 52, 58, -14, 0.f});
        }
    }

    void step_missiles() {
        for (auto& ms : missiles_) {
            ms.t += dt;
            if (ms.target < 0 || size_t(ms.target) >= units_.size() || !units_[size_t(ms.target)].alive) { ms.t = 99; continue; }
            const auto& u = units_[size_t(ms.target)];
            const double k = std::min(1.0, dt / std::max(dt, 0.5 - ms.t));
            ms.x += (u.x - ms.x) * k;
            ms.y += (u.y - ms.y) * k;
            effects_.push_back({ms.x, ms.y, 2.5, 0.3, 200, 200, 210, 0, 0.f});
            if (ms.t >= 0.5) { damage(size_t(ms.target), 5); ms.t = 99; }
        }
        missiles_.erase(std::remove_if(missiles_.begin(), missiles_.end(), [](const Missile& m) { return m.t >= 99; }), missiles_.end());
    }

    void hit(const char* why) {
        if (grace_ > 0) return;
        kill_ = 90;
        grace_ = 5.0;
        hit_flash_ = 1.f;
        alert_ = why;
        alert_age_ = 0;
        tracers_.clear();
        flak_.clear();
    }

    void step_threats(MainBus::Memory& m, double px, double py, bool found) {
        using R = Rearm::Controller;
        view_.threats.clear();
        for (auto& tr : tracers_) {
            tr.life += dt;
            if (tr.life < 0) continue;
            tr.x += tr.vx * dt;
            tr.y += tr.vy * dt;
            if (found && std::hypot(tr.x - px, (tr.y - py) * 1.4) < 7.0) { hit("HIT BY GUNFIRE"); tr.life = 9; }
        }
        tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(), [](const Tracer& t) {
            return t.life > 3 || t.x < -40 || t.x > 360 || t.y < -40 || t.y > 260; }), tracers_.end());
        for (const auto& tr : tracers_)
            if (tr.life >= 0) view_.threats.push_back({Threat::Kind::tracer, tr.x, tr.y, tr.x - tr.vx * 0.15, tr.y - tr.vy * 0.15, 0, 0});
        for (auto& f : flak_) {
            const double before = f.t;
            f.t += dt;
            if (before < f.fuse && f.t >= f.fuse) {
                bursts_.push_back({f.x, f.y, 18, 0.f});
                effects_.push_back({f.x, f.y, 9, 1.2, 60, 58, 62, -6, 0.f});
                if (found && std::hypot(f.x - px, (f.y - py) * 1.3) < 15.0) hit("HIT BY FLAK");
            }
        }
        flak_.erase(std::remove_if(flak_.begin(), flak_.end(), [](const Flak& f) { return f.t >= f.fuse; }), flak_.end());
        for (const auto& f : flak_) view_.threats.push_back({Threat::Kind::flak, f.x, f.y, 0, 0, 15.0, float(f.t / f.fuse)});
        for (auto& b : bursts_) b.age += float(dt);
        bursts_.erase(std::remove_if(bursts_.begin(), bursts_.end(), [](const Burst& b) { return b.age > 0.6f; }), bursts_.end());
        for (const auto& b : bursts_) view_.threats.push_back({Threat::Kind::burst, b.x, b.y, 0, 0, b.r, b.age / 0.6f});
        if (kill_) {
            --kill_;
            if (R::peek16(m, 0x29c09au)) kill_ = 0;
            else {
                R::poke16(m, 0xffff8d76u, 0u);
                R::poke8(m, 0xffff8d60u, uint8_t(R::peek8(m, 0xffff8d60u) | 1u));
            }
        }
    }

    void step_mission(MainBus::Memory& m) {
        if (!def_.mission || mission_done_) return;
        const auto& ms = *def_.mission;
        const bool timeout = mission_t_ >= ms.time_limit;
        if (ms.type == MissionDef::Type::destroy && kills_ >= ms.target) finish(m, true);
        else if (ms.type == MissionDef::Type::survive && timeout) finish(m, true);
        else if (ms.type == MissionDef::Type::destroy && timeout) finish(m, false);
    }

    void finish(MainBus::Memory& m, bool success) {
        mission_done_ = true;
        result_ = success ? "MISSION COMPLETE" : "MISSION FAILED";
        result_age_ = 0;
        if (success) add_score(m, def_.mission->bonus);
    }

    void view_beam(const Beam& b, double bx, double a, bool on) {
        if (view_.beams.size() >= beams_.size()) view_.beams.clear();
        view_.beams.push_back({bx, a, b.width, on});
    }

    void step_effects(std::vector<Disk>& out) {
        for (auto& e : effects_) { e.age += float(dt); e.y += e.vy * dt; }
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Effect& e) { return e.age >= e.life; }), effects_.end());
        if (effects_.size() > 240u) effects_.erase(effects_.begin(), effects_.end() - 240);
        for (const auto& e : effects_) {
            const double k = e.age / e.life;
            const bool fire = e.cr > 200;
            out.push_back({e.x, e.y, fire ? e.r * (0.4 + 0.8 * k) : e.r * (1 + k), uint8_t(e.cr),
                           uint8_t(fire ? e.cg * (1 - 0.6 * k) : e.cg), uint8_t(fire ? e.cb * (1 - k) : e.cb),
                           float(fire ? 1.0 - k * k : 0.5 * (1 - k))});
        }
    }

    static Player find_player(const MainBus::Memory& m) {
        using R = Rearm::Controller;
        for (unsigned i = 0; i < 256u; ++i) {
            const uint32_t p = 0xffffa000u + i * 64u;
            if (!(R::peek8(m, p) & 0x80u) || R::peek32(m, p + 16u) != 0x98c0u) continue;
            return {160.0 + double(int16_t(R::peek16(m, p + 4u))) * 1.1,
                    113.0 + double(int16_t(R::peek16(m, p + 6u)) - 0x50) * 1.0, true};
        }
        return {};
    }

    static void add_score(MainBus::Memory& m, unsigned points) {
        using R = Rearm::Controller;
        uint32_t score = R::peek32(m, 0xffff8884u), add = points / 10u, out = 0, carry = 0;
        for (unsigned d = 0; d < 8; ++d) {
            uint32_t digit = ((score >> (4 * d)) & 15u) + (add % 10u) + carry;
            add /= 10u;
            carry = digit >= 10u;
            if (carry) digit -= 10u;
            out |= digit << (4 * d);
        }
        R::poke32(m, 0xffff8884u, out);
    }
};

} // namespace CustomStage

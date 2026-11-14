#pragma once

// A native boss for custom stages: a large flying carrier that overtakes the
// player and holds station ahead, maneuvering while it fights. The board keeps
// running the stage (its own fighters still come in); this controller adds the
// boss on top and talks to the board only through RAM the arcade already uses:
//
//   $29C006 (long)   stage timer, held at hold_tick while the boss lives so
//                    the stage cannot end around it
//   $FFFF8D66        missiles, to see the player fire one
//   player record    +4/+6 position (handler $98C0) for aim and hits
//   $FFFF8D60 bit 0  collision request (with $8D76 grace cleared), so a hit
//                    uses the arcade's own damage, explosion and life flow
//   $FFFF8884 (BCD)  score, for the destruction bonus (stored / 10)
//
// Everything visual is described in a 320x224 frame before the horizon roll.
// The aircraft and its fire effects are painted behind the sprites; everything
// that can hit the player is listed separately (threats) so the frontend can
// draw it on top, where it is always visible (frontend/native_boss.hpp).

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

class BossFight {
public:
    enum class Phase { waiting, approach, battle, dying, destroyed, escaped };

    struct Sprite { const std::string* asset{}; double x{}, y{}, width{}, height{}, angle{}; float alpha{1}, flash{}; };
    struct Disk { double x{}, y{}, radius{}; uint8_t r{}, g{}, b{}; float alpha{1}; };
    struct PartView { const BossPart* part{}; double x{}, y{}, width{}, height{}; float health{}; bool alive{}, shielded{}, locked{}; };
    // Things that can hit the player, drawn above everything.
    struct Threat {
        enum class Kind { tracer, flak, missile, burst } kind{};
        double x{}, y{};       // head / center
        double x2{}, y2{};     // tracer tail / missile trail start
        double radius{};       // flak warning ring or burst size
        float progress{};      // flak: 0..1 until it bursts; burst: 0..1 fade
    };
    struct View {
        bool active{};
        Phase phase{Phase::waiting};
        double roll{};                 // radians, the board's horizon roll
        std::vector<Sprite> sprites;   // back to front
        std::vector<Disk> disks;       // explosions, smoke, glows (behind sprites)
        std::vector<Threat> threats;   // above everything
        std::vector<PartView> parts;
        double aim_x{}, aim_y{};
        float health{}, time_left{}, banner_age{};
        const char* banner{""};
        const char* detail{""};
        const char* alert{""};         // MISSILE / FLAK warnings
        float alert_age{9.f};
        float hit_flash{};             // 1 when the player was just hit
        const std::string* name{};
    };

    static constexpr double dt = 1.0 / 60.0;

    explicit BossFight(const BossDefinition& definition) : def_(definition) { reset(); }

    void reset() {
        phase_ = Phase::waiting;
        t_ = phase_t_ = 0;
        hp_.assign(def_.parts.size(), 0);
        for (size_t i = 0; i < def_.parts.size(); ++i) hp_[i] = def_.parts[i].hp;
        flash_.assign(def_.parts.size(), 0.f);
        effects_.clear();
        tracers_.clear();
        flak_.clear();
        missiles_.clear();
        rockets_.clear();
        bursts_.clear();
        banner_ = detail_ = alert_ = "";
        banner_age_ = 0;
        alert_age_ = 9.f;
        hit_flash_ = 0;
        turret_clock_ = 1.6;
        flak_clock_ = 2.4;
        rocket_clock_ = 6.0;
        grace_ = 0;
        missiles_seen_ = 0;
        kill_ = 0;
        rng_ = 0x51ed27u;
        bx_ = 160; by_ = 92; scale_ = 1; bank_ = 0; prev_bx_ = 160;
        maneuver_ = 0; maneuver_t_ = 0;
        view_ = {};
    }

    const View& view() const { return view_; }
    Phase phase() const { return phase_; }
    bool fighting() const { return phase_ == Phase::approach || phase_ == Phase::battle || phase_ == Phase::dying; }

    void sync(MainBus::Memory& m, uint16_t stage_tick, const GameControls& input) {
        using R = Rearm::Controller;
        t_ += dt;
        phase_t_ += dt;
        banner_age_ += float(dt);
        alert_age_ += float(dt);
        hit_flash_ = std::max(0.f, hit_flash_ - float(dt) * 1.5f);
        if (grace_ > 0) grace_ -= dt;
        for (auto& f : flash_) f = std::max(0.f, f - float(dt) * 4.f);

        const bool flying = R::peek16(m, 0x29c09cu) == 0x10u;
        if (phase_ == Phase::waiting) {
            view_.active = false;
            if (stage_tick >= def_.appear_tick && flying) enter(Phase::approach, "WARNING", "LARGE AIRCRAFT CLOSING FROM ASTERN");
            return;
        }
        // Continue screens, game over and attract show no boss.
        if (!flying) { view_.active = false; return; }

        // Hold the stage timer while the boss is in the fight (and while its result shows).
        const bool settling = (phase_ == Phase::destroyed || phase_ == Phase::escaped) && phase_t_ < 3.5;
        if ((fighting() || settling) && stage_tick >= def_.hold_tick)
            R::poke32(m, 0x29c006u, uint32_t(def_.hold_tick) << 4);

        const Player player = find_player(m);
        const int16_t cam_x = int16_t(R::peek16(m, 0x29c08au)), cam_y = int16_t(R::peek16(m, 0x29c088u));
        const double roll = double(int8_t(R::peek16(m, 0x29c09eu) & 0xffu)) * 6.283185307179586 / 256.0;
        view_.roll = roll;
        // Undo the horizon roll so aim and boss share one frame. The frontend
        // draws world points at R(-roll) about the screen center, like the backdrop.
        const auto unroll = [&](double x, double y) {
            const double dx = x - 160.0, dy = y - 112.0, c = std::cos(roll), s = std::sin(roll);
            return std::pair{160.0 + dx * c - dy * s, 112.0 + dx * s + dy * c};
        };
        const auto [aim_x, aim_y] = unroll(player.x, player.y - 10.0);
        const auto [plane_x, plane_y] = unroll(player.x, player.y);
        view_.aim_x = aim_x;
        view_.aim_y = aim_y;

        if (phase_ == Phase::destroyed) {
            view_.active = phase_t_ < 4.0;
            view_.sprites.clear();
            view_.parts.clear();
            view_.disks.clear();
            view_.threats.clear();
            step_effects(view_.disks);
            publish_text();
            return;
        }

        move(cam_x, cam_y);
        if (phase_ == Phase::battle && phase_t_ > def_.time_limit)
            enter(Phase::escaped, "TARGET ESCAPED", "THE CARRIER WITHDREW");

        // Parts in screen space, rotated with the hull's bank.
        const double c = std::cos(bank_), s = std::sin(bank_);
        const auto place = [&](double px, double py) {
            return std::pair{bx_ + (px * c - py * s) * scale_, by_ + (px * s + py * c) * scale_};
        };
        view_.parts.clear();
        const bool pods_left = pods_alive() > 0;
        int locked = -1;
        double best = 1e9;
        for (size_t i = 0; i < def_.parts.size(); ++i) {
            const auto& p = def_.parts[i];
            const auto [x, y] = place(p.x, p.y);
            PartView v{&p, x, y, p.width * scale_, p.height * scale_, float(std::max(0, hp_[i])) / float(p.hp), hp_[i] > 0,
                       p.kind == BossPart::Kind::core && pods_left, false};
            if (phase_ == Phase::battle && v.alive && !v.shielded) {
                const double d = std::hypot(v.x - aim_x, v.y - aim_y);
                if (d < std::max(28.0, v.width * 0.6) && d < best) { best = d; locked = int(i); }
            }
            view_.parts.push_back(v);
        }
        if (locked >= 0) view_.parts[size_t(locked)].locked = true;

        if (phase_ == Phase::battle) {
            if (input.cannon && locked >= 0 && int(t_ * 60) % 3 == 0) {
                damage(size_t(locked), 2);
                const auto& v = view_.parts[size_t(locked)];
                spark(v.x + (rand() - 0.5) * v.width * 0.6, v.y + (rand() - 0.5) * v.height * 0.6);
            }
            // Missiles: the arcade's own launch, followed by a homing hit on the locked part.
            const unsigned missiles = R::peek16(m, 0xffff8d66u);
            if (missiles < missiles_seen_ && missiles_seen_ - missiles < 4u)
                missiles_.push_back({plane_x, plane_y, locked, 0.0});
            missiles_seen_ = missiles;
            if (player.found) attack(plane_x, plane_y);
        } else {
            missiles_seen_ = R::peek16(m, 0xffff8d66u);
        }
        step_missiles();
        step_threats(m, plane_x, plane_y, player.found, input);

        if (phase_ == Phase::battle && hp_[core_index()] <= 0) {
            enter(Phase::dying, "CORE DESTROYED", "");
            tracers_.clear();
            flak_.clear();
            rockets_.clear();
        }
        if (phase_ == Phase::dying && int(phase_t_ * 60) % 5 == 0) {
            const auto [x, y] = place((rand() - 0.5) * def_.width, (rand() - 0.5) * def_.height * 0.6);
            boom(x, y, 16 + rand() * 26);
        }
        if (phase_ == Phase::dying && phase_t_ > 4.0) {
            add_score(m, def_.bonus);
            for (int k = 0; k < 10; ++k) boom(bx_ + (rand() - 0.5) * 160, by_ + (rand() - 0.5) * 50, 30 + rand() * 30);
            enter(Phase::destroyed, "TARGET DESTROYED", "MISSION ACCOMPLISHED");
            return;
        }

        draw_aircraft(place);
        publish_text();
    }

private:
    struct Player { double x{160}, y{120}; bool found{}; };
    struct Effect { double x, y, r, life, cr, cg, cb, vy; float age; };
    struct Tracer { double x, y, vx, vy, life; };
    struct Flak { double x, y, t, fuse; };
    struct Missile { double x, y; int target; double t; };
    struct Rocket { double x, y, vx, vy, life; std::vector<std::pair<double, double>> trail; };
    struct Burst { double x, y, r; float age; };

    const BossDefinition& def_;
    Phase phase_{Phase::waiting};
    double t_{}, phase_t_{}, turret_clock_{}, flak_clock_{}, rocket_clock_{}, grace_{};
    double bx_{160}, by_{92}, scale_{1}, bank_{}, prev_bx_{160}, maneuver_t_{};
    int maneuver_{};
    std::vector<int> hp_;
    std::vector<float> flash_;
    std::vector<Effect> effects_;
    std::vector<Tracer> tracers_;
    std::vector<Flak> flak_;
    std::vector<Missile> missiles_;
    std::vector<Rocket> rockets_;
    std::vector<Burst> bursts_;
    const char* banner_{""};
    const char* detail_{""};
    const char* alert_{""};
    float banner_age_{}, alert_age_{9.f}, hit_flash_{};
    unsigned missiles_seen_{}, kill_{};
    uint32_t rng_{};
    View view_;

    double rand() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return double(rng_ & 0xffffu) / 65535.0;
    }

    void enter(Phase p, const char* banner, const char* detail) {
        phase_ = p;
        phase_t_ = 0;
        banner_ = banner;
        detail_ = detail;
        banner_age_ = 0;
    }

    void alert(const char* text) {
        if (alert_age_ < 0.8f && alert_ == text) return;
        alert_ = text;
        alert_age_ = 0;
    }

    unsigned pods_alive() const {
        unsigned n = 0;
        for (size_t i = 0; i < def_.parts.size(); ++i) n += def_.parts[i].kind == BossPart::Kind::pod && hp_[i] > 0;
        return n;
    }

    size_t core_index() const {
        for (size_t i = 0; i < def_.parts.size(); ++i)
            if (def_.parts[i].kind == BossPart::Kind::core) return i;
        return 0;
    }

    // Flight path: overtakes from above and behind, then cruises ahead with a
    // weave, occasionally lunging closer or sweeping across, banking as it turns.
    void move(int16_t cam_x, int16_t cam_y) {
        double x = 160, y = 92, sc = 1.0;
        const double wx = 160 + 64 * std::sin(0.31 * t_) + 18 * std::sin(0.83 * t_);
        const double wy = 90 + 12 * std::sin(0.47 * t_ + 1.0);
        switch (phase_) {
        case Phase::approach: {
            // From huge and overhead (it has just passed over you) down to station.
            const double k = std::clamp(phase_t_ / 5.0, 0.0, 1.0), e = k * k * (3 - 2 * k);
            sc = 2.6 - 1.6 * e;
            x = 160 + (wx - 160) * e;
            y = -120 + (wy + 120) * e;
            if (k >= 1.0) enter(Phase::battle, def_.name.c_str(), "DESTROY THE ENGINES, THEN THE BRIDGE");
            break;
        }
        case Phase::battle: {
            maneuver_t_ += dt;
            double lunge = 0, sweep = 0;
            if (maneuver_ == 1) { // lunge: comes in closer for a moment
                const double k = std::clamp(maneuver_t_ / 3.0, 0.0, 1.0);
                lunge = std::sin(k * 3.14159);
            } else if (maneuver_ == 2) { // sweep: slides across the sky and back
                const double k = std::clamp(maneuver_t_ / 4.0, 0.0, 1.0);
                sweep = std::sin(k * 6.28318) * 90;
            }
            if (maneuver_t_ > (maneuver_ ? 4.0 : 5.5)) {
                maneuver_ = maneuver_ ? 0 : 1 + int(rand() * 2);
                maneuver_t_ = 0;
            }
            sc = 1.0 + 0.28 * lunge;
            x = wx + sweep;
            y = wy + 10 * lunge;
            break;
        }
        case Phase::dying:
            sc = std::max(0.4, 1.0 - 0.08 * phase_t_);
            x = bx_ + 14 * dt * (bank_ > 0 ? 1 : -1) + (wx - bx_) * 0.01;
            y = by_ + (20 + 40 * phase_t_) * dt;
            break;
        case Phase::escaped:
            sc = std::max(0.08, 1.0 - 0.35 * phase_t_);
            x = bx_;
            y = by_ - 50 * dt;
            break;
        default:
            break;
        }
        // The world slides opposite the camera so the player can line up a target.
        if (phase_ == Phase::approach || phase_ == Phase::battle) {
            x -= cam_x * 1.6;
            y -= cam_y * 0.5;
        }
        const double vx = (x - prev_bx_) / dt;
        prev_bx_ = x;
        bx_ = x;
        by_ = y;
        scale_ = sc;
        const double target_bank = phase_ == Phase::dying ? 0.5 * std::sin(phase_t_ * 1.3) + 0.35 :
                                   std::clamp(vx * 0.0025, -0.32, 0.32);
        bank_ += (target_bank - bank_) * std::min(1.0, 2.5 * dt);
    }

    static Player find_player(const MainBus::Memory& m) {
        using R = Rearm::Controller;
        for (unsigned i = 0; i < 256u; ++i) {
            const uint32_t p = 0xffffa000u + i * 64u;
            if (!(R::peek8(m, p) & 0x80u) || R::peek32(m, p + 16u) != 0x98c0u) continue;
            // +4 is lateral, +6 the screen row (rest $50 sits about 113 px down).
            return {160.0 + double(int16_t(R::peek16(m, p + 4u))) * 1.1,
                    113.0 + double(int16_t(R::peek16(m, p + 6u)) - 0x50) * 1.0, true};
        }
        return {};
    }

    void damage(size_t i, int amount) {
        if (hp_[i] <= 0) return;
        hp_[i] -= amount;
        flash_[i] = 1.f;
        if (hp_[i] <= 0) {
            const auto& v = view_.parts[i];
            for (int k = 0; k < 8; ++k) boom(v.x + (rand() - 0.5) * v.width, v.y + (rand() - 0.5) * v.height, 10 + rand() * 20);
            if (def_.parts[i].kind == BossPart::Kind::pod && pods_alive() == 0) {
                banner_ = "BRIDGE EXPOSED";
                detail_ = "TARGET THE BRIDGE";
                banner_age_ = 0;
            }
        }
    }

    void boom(double x, double y, double r) { effects_.push_back({x, y, r, 0.75, 255, 170, 60, 0, 0.f}); }
    void spark(double x, double y) { effects_.push_back({x, y, 4, 0.18, 255, 240, 180, 0, 0.f}); }
    void smoke(double x, double y, double r) { effects_.push_back({x, y, r, 1.1, 54, 52, 58, -16, 0.f}); }

    void step_effects(std::vector<Disk>& out) {
        for (auto& e : effects_) { e.age += float(dt); e.y += e.vy * dt; }
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Effect& e) { return e.age >= e.life; }), effects_.end());
        if (effects_.size() > 260u) effects_.erase(effects_.begin(), effects_.end() - 260);
        for (const auto& e : effects_) {
            const double k = e.age / e.life;
            const bool fire = e.cr > 200;
            const double radius = fire ? e.r * (0.4 + 0.8 * k) : e.r * (1 + 1.2 * k);
            const float alpha = float(fire ? 1.0 - k * k : 0.5 * (1 - k));
            const uint8_t g = uint8_t(fire ? e.cg * (1 - 0.6 * k) : e.cg);
            out.push_back({e.x, e.y, radius, uint8_t(e.cr), g, uint8_t(fire ? e.cb * (1 - k) : e.cb), alpha});
            if (fire && k < 0.35) out.push_back({e.x, e.y, radius * 0.45, 255, 255, 230, float(1 - k * 2.5)});
        }
    }

    void step_missiles() {
        for (auto& ms : missiles_) {
            ms.t += dt;
            double tx = ms.x, ty = ms.y - 200;
            if (ms.target >= 0 && size_t(ms.target) < view_.parts.size()) { tx = view_.parts[size_t(ms.target)].x; ty = view_.parts[size_t(ms.target)].y; }
            const double k = std::min(1.0, dt / std::max(dt, 0.55 - ms.t));
            ms.x += (tx - ms.x) * k;
            ms.y += (ty - ms.y) * k;
            effects_.push_back({ms.x, ms.y, 2.5, 0.35, 200, 200, 210, 0, 0.f});
            if (ms.t >= 0.55) {
                if (ms.target >= 0) { damage(size_t(ms.target), 40); boom(ms.x, ms.y, 18); }
                ms.t = 99;
            }
        }
        missiles_.erase(std::remove_if(missiles_.begin(), missiles_.end(), [](const Missile& m) { return m.t >= 99; }), missiles_.end());
    }

    void attack(double px, double py) {
        if (grace_ > 0) return;
        turret_clock_ -= dt;
        flak_clock_ -= dt;
        rocket_clock_ -= dt;
        const unsigned pods = pods_alive();
        if (turret_clock_ <= 0) {
            turret_clock_ = 2.4;
            for (const auto& v : view_.parts) {
                if (v.part->kind != BossPart::Kind::turret || !v.alive) continue;
                for (int k = 0; k < 3; ++k) {
                    // Aim where the plane is, spread a little: move and they miss.
                    const double tx = px + (rand() - 0.5) * 24, ty = py + (rand() - 0.5) * 14;
                    const double d = std::max(1.0, std::hypot(tx - v.x, ty - v.y));
                    tracers_.push_back({v.x, v.y, (tx - v.x) / d * 120.0, (ty - v.y) / d * 120.0, -0.16 * k});
                }
                alert("GUNFIRE");
            }
        }
        if (flak_clock_ <= 0) {
            flak_clock_ = pods ? 2.6 - 0.25 * pods : 1.6;
            const int shells = pods ? 2 : 3;
            for (int k = 0; k < shells; ++k)
                flak_.push_back({px + (rand() - 0.5) * 80, py + (rand() - 0.5) * 40, 0.0, 1.4 + 0.2 * k});
            alert("FLAK");
        }
        // Heavy missiles from the bridge once the engines are failing.
        if (rocket_clock_ <= 0 && pods <= 2) {
            rocket_clock_ = 5.5;
            const auto& core = view_.parts[core_index()];
            for (int side : {-1, 1})
                rockets_.push_back({core.x + side * 14.0, core.y, side * 40.0, 30.0, 0.0, {}});
            alert("MISSILE");
        }
    }

    void hit(const char* why) {
        if (grace_ > 0 || phase_ != Phase::battle) return;
        kill_ = 90;
        grace_ = 6.0;
        hit_flash_ = 1.f;
        alert_ = why;
        alert_age_ = 0;
        tracers_.clear();
        flak_.clear();
        rockets_.clear();
    }

    void step_threats(MainBus::Memory& m, double px, double py, bool found, const GameControls& input) {
        using R = Rearm::Controller;
        view_.threats.clear();
        for (auto& tr : tracers_) {
            tr.life += dt;
            if (tr.life < 0) continue;
            tr.x += tr.vx * dt;
            tr.y += tr.vy * dt;
            if (found && std::hypot(tr.x - px, (tr.y - py) * 1.4) < 8.0) { hit("HIT BY GUNFIRE"); tr.life = 9; }
        }
        tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(), [](const Tracer& t) {
            return t.life > 3 || t.x < -40 || t.x > 360 || t.y < -40 || t.y > 260; }), tracers_.end());
        for (const auto& tr : tracers_)
            if (tr.life >= 0) view_.threats.push_back({Threat::Kind::tracer, tr.x, tr.y, tr.x - tr.vx * 0.16, tr.y - tr.vy * 0.16, 0, 0});

        for (auto& f : flak_) {
            const double before = f.t;
            f.t += dt;
            if (before < f.fuse && f.t >= f.fuse) {
                bursts_.push_back({f.x, f.y, 18, 0.f});
                if (found && std::hypot(f.x - px, (f.y - py) * 1.3) < 16.0) hit("HIT BY FLAK");
            }
        }
        flak_.erase(std::remove_if(flak_.begin(), flak_.end(), [](const Flak& f) { return f.t >= f.fuse; }), flak_.end());
        for (const auto& f : flak_)
            view_.threats.push_back({Threat::Kind::flak, f.x, f.y, 0, 0, 16.0, float(f.t / f.fuse)});

        for (auto& r : rockets_) {
            r.life += dt;
            // Slow homing with a limited turn: keep moving and it overshoots.
            const double dx = px - r.x, dy = py - r.y, d = std::max(1.0, std::hypot(dx, dy));
            const double speed = 70.0 + 25.0 * std::min(1.0, r.life);
            r.vx += (dx / d * speed - r.vx) * std::min(1.0, 1.1 * dt);
            r.vy += (dy / d * speed - r.vy) * std::min(1.0, 1.1 * dt);
            r.x += r.vx * dt;
            r.y += r.vy * dt;
            r.trail.push_back({r.x, r.y});
            if (r.trail.size() > 18u) r.trail.erase(r.trail.begin());
            if (found && std::hypot(r.x - px, (r.y - py) * 1.3) < 10.0) { hit("HIT BY MISSILE"); r.life = 99; bursts_.push_back({r.x, r.y, 22, 0.f}); }
            // The cannon can shoot a missile down if it is in the sight.
            if (input.cannon && std::hypot(r.x - view_.aim_x, r.y - view_.aim_y) < 14.0) { r.life = 99; bursts_.push_back({r.x, r.y, 12, 0.f}); }
        }
        rockets_.erase(std::remove_if(rockets_.begin(), rockets_.end(), [](const Rocket& r) { return r.life > 5.0; }), rockets_.end());
        for (const auto& r : rockets_) {
            const auto& tail = r.trail.front();
            view_.threats.push_back({Threat::Kind::missile, r.x, r.y, tail.first, tail.second, 0, std::min(1.f, float(r.life))});
        }
        for (auto& b : bursts_) b.age += float(dt);
        bursts_.erase(std::remove_if(bursts_.begin(), bursts_.end(), [](const Burst& b) { return b.age > 0.6f; }), bursts_.end());
        for (const auto& b : bursts_)
            view_.threats.push_back({Threat::Kind::burst, b.x, b.y, 0, 0, b.r, b.age / 0.6f});

        // Raise the arcade's own collision until it is counted ($29C09A).
        if (kill_) {
            --kill_;
            if (R::peek16(m, 0x29c09au)) kill_ = 0;
            else {
                R::poke16(m, 0xffff8d76u, 0u);
                R::poke8(m, 0xffff8d60u, uint8_t(R::peek8(m, 0xffff8d60u) | 1u));
            }
        }
    }

    template <class Place>
    void draw_aircraft(const Place& place) {
        view_.active = true;
        view_.phase = phase_;
        view_.sprites.clear();
        view_.disks.clear();
        const float fade = phase_ == Phase::dying ? float(std::clamp(1.0 - (phase_t_ - 3.0) / 1.0, 0.0, 1.0)) : 1.f;
        // Engine glow behind each live pod, pulsing.
        for (size_t i = 0; i < def_.parts.size(); ++i) {
            const auto& v = view_.parts[i];
            if (def_.parts[i].kind != BossPart::Kind::pod || !v.alive) continue;
            const double pulse = 0.75 + 0.25 * std::sin(t_ * 23.0 + double(i) * 1.7);
            view_.disks.push_back({v.x, v.y, v.width * 0.75 * pulse, 255, 150, 60, 0.35f * fade});
        }
        view_.sprites.push_back({&def_.hull_asset, bx_, by_, def_.width * scale_, def_.height * scale_, bank_, fade, 0.f});
        for (size_t i = 0; i < def_.parts.size(); ++i) {
            const auto& v = view_.parts[i];
            const auto& p = def_.parts[i];
            if (!v.alive && p.broken_asset.empty()) continue;
            const std::string* asset = v.alive || p.broken_asset.empty() ? &p.asset : &p.broken_asset;
            double angle = bank_;
            if (p.kind == BossPart::Kind::turret && v.alive) // turrets swing toward the player
                angle += std::clamp(std::atan2(view_.aim_x - v.x, 120.0), -0.7, 0.7);
            view_.sprites.push_back({asset, v.x, v.y, v.width, v.height, angle, fade, flash_[i]});
            if (!v.alive && int(t_ * 30 + double(i) * 7) % 6 == 0) smoke(v.x + (rand() - 0.5) * 6, v.y, 3 + rand() * 3);
            if (!v.alive && int(t_ * 60 + double(i) * 5) % 9 == 0) effects_.push_back({v.x, v.y, 4 + rand() * 4, 0.3, 255, 140, 40, 0, 0.f});
        }
        // Strobes at the wingtips and tail, blinking out of step.
        const double wing = def_.width * 0.48;
        for (const auto& [ox, oy, phase, r, g, b] : {std::tuple{-wing, def_.height * 0.20, 0.0, 255, 70, 60},
                                                      std::tuple{wing, def_.height * 0.20, 0.0, 80, 255, 120},
                                                      std::tuple{0.0, -def_.height * 0.45, 0.5, 255, 255, 255}}) {
            if (std::fmod(t_ + phase, 1.0) < 0.12) {
                const auto [x, y] = place(ox, oy);
                view_.disks.push_back({x, y, 4.0 * scale_, uint8_t(r), uint8_t(g), uint8_t(b), fade});
            }
        }
        step_effects(view_.disks);
        float health = 0, total = 0;
        for (size_t i = 0; i < def_.parts.size(); ++i) { health += float(std::max(0, hp_[i])); total += float(def_.parts[i].hp); }
        view_.health = total > 0 ? health / total : 0;
        view_.time_left = phase_ == Phase::battle ? float(std::clamp(1.0 - phase_t_ / def_.time_limit, 0.0, 1.0)) : 1.f;
    }

    void publish_text() {
        view_.banner = banner_;
        view_.detail = detail_;
        view_.banner_age = banner_age_;
        view_.alert = alert_;
        view_.alert_age = alert_age_;
        view_.hit_flash = hit_flash_;
        view_.name = &def_.name;
    }

    // Score is packed BCD at $8884, stored in tens.
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

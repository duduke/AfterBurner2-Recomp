#pragma once

// A native boss for custom stages: a large flying carrier that holds station
// ahead of the player. The board keeps running the stage (its own fighters
// still come in); this controller adds the boss on top and talks to the board
// only through RAM the arcade already uses:
//
//   $29C006 (long)   stage timer, held at hold_tick while the boss lives so
//                    the stage cannot end around it
//   $FFFF8D66        missiles, to see the player fire one
//   player record    +4/+6 position (handler $98C0) for aim and hits
//   $FFFF8D60 bit 0  collision request (with $8D76 grace cleared), so a hit
//                    uses the arcade's own damage, explosion and life flow
//   $FFFF8884 (BCD)  score, for the destruction bonus (stored / 10)
//
// Everything visual is described in a 320x224 frame before the horizon roll;
// the frontend draws it (frontend/native_boss.hpp).

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

    struct Sprite { const std::string* asset{}; double x{}, y{}, width{}, height{}; float alpha{1}, flash{}; };
    struct Disk { double x{}, y{}, radius{}; uint8_t r{}, g{}, b{}; float alpha{1}; };
    struct PartView { const BossPart* part{}; double x{}, y{}, width{}, height{}; float health{}; bool alive{}, shielded{}, locked{}; };
    struct View {
        bool active{};
        Phase phase{Phase::waiting};
        double roll{};                 // radians, the board's horizon roll
        std::vector<Sprite> sprites;   // back to front
        std::vector<Disk> disks;       // explosions, flak, tracers, missiles
        std::vector<PartView> parts;
        double aim_x{}, aim_y{};
        float health{}, time_left{}, banner_age{};
        const char* banner{""};
        const char* detail{""};
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
        banner_ = "";
        detail_ = "";
        banner_age_ = 0;
        turret_clock_ = 1.2;
        flak_clock_ = 2.0;
        grace_ = 0;
        missiles_seen_ = 0;
        kill_ = 0;
        rng_ = 0x51ed27u;
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
        if (grace_ > 0) grace_ -= dt;
        for (auto& f : flash_) f = std::max(0.f, f - float(dt) * 4.f);

        const bool flying = R::peek16(m, 0x29c09cu) == 0x10u;
        if (phase_ == Phase::waiting) {
            view_.active = false;
            if (stage_tick >= def_.appear_tick && flying) enter(Phase::approach, "WARNING", "LARGE AIRCRAFT APPROACHING");
            return;
        }

        // Continue screens, game over and attract show no boss.
        if (!flying) { view_.active = false; return; }

        // Hold the stage timer while the boss is in the fight.
        const bool settling = (phase_ == Phase::destroyed || phase_ == Phase::escaped) && phase_t_ < 3.5;
        if ((fighting() || settling) && stage_tick >= def_.hold_tick) {
            const uint32_t held = uint32_t(def_.hold_tick) << 4;
            R::poke32(m, 0x29c006u, held);
        }

        // Player plane and aim, from the live player record.
        Player player = find_player(m);
        const int16_t cam_x = int16_t(R::peek16(m, 0x29c08au)), cam_y = int16_t(R::peek16(m, 0x29c088u));
        const double roll = double(int8_t(R::peek16(m, 0x29c09eu) & 0xffu)) * 6.283185307179586 / 256.0;
        view_.roll = roll;
        // Undo the horizon roll so aim and boss share one frame. The frontend
        // draws world points at R(-roll) about the screen center, like the backdrop.
        const auto unroll = [&](double x, double y) {
            const double dx = x - 160.0, dy = y - 112.0, c = std::cos(roll), s = std::sin(roll);
            return std::pair{160.0 + dx * c - dy * s, 112.0 + dx * s + dy * c};
        };
        auto [aim_x, aim_y] = unroll(player.x, player.y - 10.0);
        view_.aim_x = aim_x;
        view_.aim_y = aim_y;

        // Boss placement: grows in from the distance, then weaves ahead.
        double scale = 1.0, bx = 0, by = 0;
        switch (phase_) {
        case Phase::approach: {
            const double k = std::clamp(phase_t_ / 4.5, 0.0, 1.0);
            scale = 0.12 + 0.88 * (k * k * (3 - 2 * k));
            by = 112 - (112 - 92) * k;
            bx = 160;
            if (k >= 1.0) enter(Phase::battle, def_.name.c_str(), "DESTROY THE ENGINES, THEN THE BRIDGE");
            break;
        }
        case Phase::dying:
            bx = 160 + 70 * std::sin(0.23 * t_);
            by = 92 + 14 * std::sin(0.41 * t_) + 26 * phase_t_ * phase_t_;
            scale = 1.0 - 0.05 * phase_t_;
            if (phase_t_ > 4.0) {
                phase_ = Phase::destroyed;
                add_score(m, def_.bonus);
                enter(Phase::destroyed, "TARGET DESTROYED", "MISSION ACCOMPLISHED");
                return;
            }
            break;
        case Phase::escaped:
            bx = 160 + 70 * std::sin(0.23 * t_);
            by = 92 - 60 * phase_t_;
            scale = std::max(0.1, 1.0 - 0.3 * phase_t_);
            break;
        case Phase::destroyed:
            view_.active = phase_t_ < 4.0;
            view_.sprites.clear();
            view_.parts.clear();
            view_.disks.clear();
            step_effects(view_.disks);
            view_.banner = banner_;
            view_.detail = detail_;
            view_.banner_age = banner_age_;
            return;
        default:
            bx = 160 + 70 * std::sin(0.23 * t_);
            by = 92 + 14 * std::sin(0.41 * t_);
            break;
        }
        // The world slides opposite the camera so the player can line up a target.
        bx -= cam_x * 1.6;
        by -= cam_y * 0.5;

        if (phase_ == Phase::battle && phase_t_ > def_.time_limit)
            enter(Phase::escaped, "TARGET ESCAPED", "THE CARRIER WITHDREW");

        // Parts in screen space.
        view_.parts.clear();
        const bool pods_left = std::any_of(def_.parts.begin(), def_.parts.end(), [&](const BossPart& p) {
            return p.kind == BossPart::Kind::pod && hp_[size_t(&p - def_.parts.data())] > 0; });
        int locked = -1;
        double best = 1e9;
        for (size_t i = 0; i < def_.parts.size(); ++i) {
            const auto& p = def_.parts[i];
            PartView v{&p, bx + p.x * scale, by + p.y * scale, p.width * scale, p.height * scale,
                       float(std::max(0, hp_[i])) / float(p.hp), hp_[i] > 0,
                       p.kind == BossPart::Kind::core && pods_left, false};
            if (phase_ == Phase::battle && v.alive && !v.shielded) {
                const double d = std::hypot(v.x - aim_x, v.y - aim_y);
                if (d < std::max(26.0, v.width * 0.6) && d < best) { best = d; locked = int(i); }
            }
            view_.parts.push_back(v);
        }
        if (locked >= 0) view_.parts[size_t(locked)].locked = true;

        if (phase_ == Phase::battle) {
            // Cannon: continuous damage on the part under the sight.
            if (input.cannon && locked >= 0 && int(t_ * 60) % 3 == 0) {
                damage(size_t(locked), 2);
                spark(view_.parts[size_t(locked)].x + (rand() - 0.5) * 10, view_.parts[size_t(locked)].y + (rand() - 0.5) * 8);
            }
            // Missiles: the arcade's own launch, followed by a homing hit on the locked part.
            const unsigned missiles = R::peek16(m, 0xffff8d66u);
            if (missiles + 1u == missiles_seen_ || (missiles < missiles_seen_ && missiles_seen_ - missiles < 4u))
                missiles_.push_back({player.x, player.y, locked, 0.0});
            missiles_seen_ = missiles;
            attack(player, bx, by, scale);
        } else {
            missiles_seen_ = R::peek16(m, 0xffff8d66u);
        }
        step_missiles();
        step_threats(m, player);

        if (phase_ == Phase::battle) {
            const size_t core = core_index();
            if (hp_[core] <= 0) {
                enter(Phase::dying, "CORE DESTROYED", "");
                tracers_.clear();
                flak_.clear();
            }
        }
        if (phase_ == Phase::dying && int(phase_t_ * 60) % 6 == 0)
            boom(bx + (rand() - 0.5) * def_.width * scale, by + (rand() - 0.5) * def_.height * scale * 0.7, 18 + rand() * 22);

        // Picture, back to front.
        view_.active = true;
        view_.phase = phase_;
        view_.sprites.clear();
        const float fade = phase_ == Phase::dying ? float(std::clamp(1.0 - (phase_t_ - 2.5) / 1.5, 0.0, 1.0)) : 1.f;
        view_.sprites.push_back({&def_.hull_asset, bx, by, def_.width * scale, def_.height * scale, fade, 0.f});
        for (size_t i = 0; i < def_.parts.size(); ++i) {
            const auto& v = view_.parts[i];
            const auto& p = def_.parts[i];
            const std::string* asset = v.alive || p.broken_asset.empty() ? &p.asset : &p.broken_asset;
            if (!v.alive && p.broken_asset.empty()) continue;
            view_.sprites.push_back({asset, v.x, v.y, v.width, v.height, fade, flash_[i]});
            if (!v.alive && int(t_ * 30 + double(i) * 7) % 9 == 0) // smoke from wrecked parts
                effects_.push_back({v.x + (rand() - 0.5) * 6, v.y, 2 + rand() * 3, 0.8, 52, 50, 54, 0.f});
        }
        view_.disks.clear();
        step_effects(view_.disks);
        for (const auto& f : flak_) {
            const bool burst = f.t >= f.fuse;
            if (!burst) {
                const float blink = (int(f.t * 12) % 2) ? 0.9f : 0.35f;
                view_.disks.push_back({f.x, f.y, 3.0, 255, 80, 60, blink});
            }
        }
        for (const auto& tr : tracers_) view_.disks.push_back({tr.x, tr.y, 1.8, 255, 236, 140, 1.f});
        for (const auto& ms : missiles_) view_.disks.push_back({ms.x, ms.y, 2.2, 240, 240, 255, 1.f});
        view_.health = 0;
        float total = 0;
        for (size_t i = 0; i < def_.parts.size(); ++i) {
            view_.health += float(std::max(0, hp_[i]));
            total += float(def_.parts[i].hp);
        }
        view_.health = total > 0 ? view_.health / total : 0;
        view_.time_left = phase_ == Phase::battle ? float(std::clamp(1.0 - phase_t_ / def_.time_limit, 0.0, 1.0)) : 1.f;
        view_.banner = banner_;
        view_.detail = detail_;
        view_.banner_age = banner_age_;
        view_.name = &def_.name;
    }

private:
    struct Player { double x{160}, y{120}; bool found{}; };
    struct Effect { double x, y, r, life, cr, cg, cb; float age; };
    struct Tracer { double x, y, vx, vy, life; };
    struct Flak { double x, y, t, fuse; };
    struct Missile { double x, y; int target; double t; };

    const BossDefinition& def_;
    Phase phase_{Phase::waiting};
    double t_{}, phase_t_{}, turret_clock_{}, flak_clock_{}, grace_{};
    std::vector<int> hp_;
    std::vector<float> flash_;
    std::vector<Effect> effects_;
    std::vector<Tracer> tracers_;
    std::vector<Flak> flak_;
    std::vector<Missile> missiles_;
    const char* banner_{""};
    const char* detail_{""};
    float banner_age_{};
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

    size_t core_index() const {
        for (size_t i = 0; i < def_.parts.size(); ++i)
            if (def_.parts[i].kind == BossPart::Kind::core) return i;
        return 0;
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
            for (int k = 0; k < 6; ++k) boom(v.x + (rand() - 0.5) * v.width, v.y + (rand() - 0.5) * v.height, 10 + rand() * 18);
            if (def_.parts[i].kind == BossPart::Kind::pod &&
                std::none_of(def_.parts.begin(), def_.parts.end(), [&](const BossPart& p) {
                    return p.kind == BossPart::Kind::pod && hp_[size_t(&p - def_.parts.data())] > 0; })) {
                banner_ = "BRIDGE EXPOSED";
                detail_ = "TARGET THE BRIDGE";
                banner_age_ = 0;
            }
        }
    }

    void boom(double x, double y, double r) { effects_.push_back({x, y, r, 0.7, 255, 170, 60, 0.f}); }
    void spark(double x, double y) { effects_.push_back({x, y, 4, 0.18, 255, 240, 180, 0.f}); }

    void step_effects(std::vector<Disk>& out) {
        for (auto& e : effects_) e.age += float(dt);
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Effect& e) { return e.age >= e.life; }), effects_.end());
        if (effects_.size() > 240u) effects_.erase(effects_.begin(), effects_.end() - 240);
        for (const auto& e : effects_) {
            const double k = e.age / e.life;
            const bool fire = e.cr > 200;
            const double radius = fire ? e.r * (0.4 + 0.8 * k) : e.r * (1 + 0.9 * k);
            const float alpha = float(fire ? 1.0 - k * k : 0.45 * (1 - k));
            const uint8_t g = uint8_t(fire ? e.cg * (1 - 0.6 * k) : e.cg);
            out.push_back({e.x, e.y - (fire ? 0 : 14 * k), radius, uint8_t(e.cr), g, uint8_t(fire ? e.cb * (1 - k) : e.cb), alpha});
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
            effects_.push_back({ms.x, ms.y, 2.5, 0.35, 200, 200, 210, 0.f});
            if (ms.t >= 0.55) {
                if (ms.target >= 0) { damage(size_t(ms.target), 40); boom(ms.x, ms.y, 16); }
                ms.t = 99;
            }
        }
        missiles_.erase(std::remove_if(missiles_.begin(), missiles_.end(), [](const Missile& m) { return m.t >= 99; }), missiles_.end());
    }

    void attack(const Player& player, double bx, double by, double scale) {
        if (grace_ > 0 || !player.found) return;
        turret_clock_ -= dt;
        flak_clock_ -= dt;
        if (turret_clock_ <= 0) {
            turret_clock_ = 2.3;
            for (size_t i = 0; i < def_.parts.size(); ++i) {
                const auto& p = def_.parts[i];
                if (p.kind != BossPart::Kind::turret || hp_[i] <= 0) continue;
                const double sx = bx + p.x * scale, sy = by + p.y * scale;
                for (int k = 0; k < 4; ++k) {
                    const double tx = player.x + (rand() - 0.5) * 30, ty = player.y + (rand() - 0.5) * 18;
                    const double d = std::max(1.0, std::hypot(tx - sx, ty - sy));
                    tracers_.push_back({sx, sy, (tx - sx) / d * 125.0, (ty - sy) / d * 125.0, -0.14 * k});
                }
            }
        }
        unsigned pods = 0;
        for (size_t i = 0; i < def_.parts.size(); ++i) pods += def_.parts[i].kind == BossPart::Kind::pod && hp_[i] > 0;
        if (flak_clock_ <= 0) {
            flak_clock_ = pods ? 2.4 - 0.25 * pods : 1.4;
            const int shells = pods ? 2 : 3;
            for (int k = 0; k < shells; ++k)
                flak_.push_back({player.x + (rand() - 0.5) * 70, player.y + (rand() - 0.5) * 36, 0.0, 1.25 + 0.15 * k});
        }
    }

    void step_threats(MainBus::Memory& m, const Player& player) {
        using R = Rearm::Controller;
        bool hit = false;
        for (auto& tr : tracers_) {
            tr.life += dt;
            if (tr.life < 0) continue;
            tr.x += tr.vx * dt;
            tr.y += tr.vy * dt;
            if (player.found && std::hypot(tr.x - player.x, (tr.y - player.y) * 1.4) < 8.0) { hit = true; tr.life = 9; }
        }
        tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(), [](const Tracer& t) {
            return t.life > 3 || t.x < -40 || t.x > 360 || t.y < -40 || t.y > 260; }), tracers_.end());
        for (auto& f : flak_) {
            const double before = f.t;
            f.t += dt;
            if (before < f.fuse && f.t >= f.fuse) {
                boom(f.x, f.y, 20);
                effects_.push_back({f.x, f.y, 10, 1.4, 70, 66, 70, 0.f});
                if (player.found && std::hypot(f.x - player.x, (f.y - player.y) * 1.3) < 16.0) hit = true;
            }
        }
        flak_.erase(std::remove_if(flak_.begin(), flak_.end(), [](const Flak& f) { return f.t > f.fuse + 0.1; }), flak_.end());
        if (hit && grace_ <= 0 && phase_ == Phase::battle) {
            kill_ = 90;
            grace_ = 6.0;
            tracers_.clear();
            flak_.clear();
        }
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

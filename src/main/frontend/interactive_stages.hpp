#pragma once

#include "engine/game_controls.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace Frontend {
// NativeInput reports forward/Up as positive Y. Flight-school controls use
// aircraft pitch: pulling back/Down raises the nose. Keep the model's named
// positive-pitch convention and the main game's input mapping unchanged.
inline GameControls interactive_stage_controls(GameControls input) {
    input.stick_y = -input.stick_y;
    return input;
}

// Deliberately authored enhancement rules, NOT a ROM translation. This model
// has no GameSession, bus, score, lives or native-sequence access. One tick is
// 1/60 second of prototype time; callers own pause and display pacing.
class InteractiveStage {
public:
    enum class Kind { refuel, landing, runway, jungle };
    enum class Phase { briefing, approach, contact, success, failed, bolter, rollout, rearming, ready, takeoff };
    static constexpr double dt = 1.0 / 60.0;
    explicit InteractiveStage(Kind kind = Kind::refuel) { reset(kind); }
    void reset(Kind kind) {
        type = kind; phase = Phase::briefing; elapsed = hold = fuel = 0;
        x = kind == Kind::refuel ? 12 : 16;
        height = kind == Kind::refuel ? -8 : 60;
        range = kind == Kind::refuel ? 100 : 900;
        speed = kind == Kind::refuel ? 140 : 95;
        bank = pitch = sink = 0; message = "";
        sequence_time=0;missiles=50;missile_maximum=100;native_continuation=false;
    }
    void start() { if (phase == Phase::briefing) phase = Phase::approach; }
    bool continuing()const{return phase==Phase::rollout || phase==Phase::rearming || phase==Phase::ready || phase==Phase::takeoff;}
    bool active() const { return phase == Phase::approach || phase == Phase::contact || continuing(); }
    bool finished() const { return phase == Phase::success || phase == Phase::failed || phase == Phase::bolter; }
    double target_x() const { return type == Kind::refuel ? 1.4 * std::sin(elapsed * .65) : 0; }
    double target_height() const { return type == Kind::refuel ? 1.0 * std::sin(elapsed * .43) : std::max(2., range * .06 + 2.); }
    bool aligned() const {
        return std::abs(x - target_x()) <= 2 && std::abs(height - target_height()) <= 2 &&
               std::abs(speed - 140) <= 3 && std::abs(bank) <= .18;
    }
    void tick(const GameControls& input) {
        // Reject non-finite input even during briefing/results, before mutation.
        if (!std::isfinite(input.stick_x) || !std::isfinite(input.stick_y) || !std::isfinite(input.throttle))
            throw std::invalid_argument("Prototype controls must be finite");
        if (!active()) return;
        elapsed += dt;
        if(continuing()){
            sequence_time+=dt;
            if(native_continuation)return; // The isolated native player owns these phases.
            if(phase==Phase::rollout){speed=std::max(0.,speed-65*dt);bank*=.9;pitch*=.9;x*=.98;
                if(sequence_time>=1.5){phase=Phase::rearming;sequence_time=0;}}
            else if(phase==Phase::rearming){missiles=std::min(missile_maximum,50+unsigned(sequence_time*15));
                if(missiles==missile_maximum){phase=Phase::ready;sequence_time=0;}}
            else if(phase==Phase::ready){if(input.throttle>=.95f){phase=Phase::takeoff;sequence_time=0;}}
            else if(phase==Phase::takeoff){speed=std::min(140.,speed+50*dt);range-=speed*dt;height+=std::max(0.,speed-60)*.08*dt;
                if(sequence_time>=3)finish(Phase::success,"TAKEOFF COMPLETE - CLEAR TO RESUME");}
            return;
        }
        const double sx = std::clamp(double(input.stick_x), -1., 1.);
        const double sy = std::clamp(double(input.stick_y), -1., 1.);
        const double throttle = std::clamp(double(input.throttle), -1., 1.);
        bank += (sx - bank) * dt * 4;
        pitch += (sy - pitch) * dt * 4;
        x += bank * (type == Kind::refuel ? 14 : 22) * dt;
        const double goal_speed = type == Kind::refuel ? 140 + 30 * throttle : 90 + 35 * throttle;
        speed += (goal_speed - speed) * dt * 2;
        if (type == Kind::refuel) {
            height += pitch * 10 * dt;
            range -= (speed - 140) * dt;
            if (range < -3) return finish(Phase::failed, "OVERSHOOT - BREAK AWAY EARLIER");
            if (range <= 9 && range >= 3 && aligned()) {
                phase = Phase::contact; hold += dt;
                fuel = std::min(1., hold / 4.);
                if (hold >= 4) return finish(Phase::success, "TRANSFER COMPLETE - CLEAR TO DEPART");
            } else {
                // Contact must be continuous; a disconnect resets the transfer.
                phase = Phase::approach; hold = fuel = 0;
            }
            if (elapsed >= 60) finish(Phase::failed, "RENDEZVOUS WINDOW EXPIRED");
        } else {
            sink = 6 - pitch * 7;
            // Beginner flare assist: near the deck, holding back settles into
            // a gentle descent instead of climbing away from the landing area.
            if (height < 12 && pitch > 0) sink = std::max(2.5, sink);
            height -= sink * dt;
            range -= speed * dt;
            if (height <= 2) {
                if (range > 25 || range < -260 || std::abs(x) > 12)
                    return finish(Phase::failed, "MISSED THE DECK - GO AROUND EARLIER");
                if (sink > 5 || std::abs(bank) > .5 || pitch < .05 || speed < 60 || speed > 120)
                    return finish(Phase::failed, "HARD LANDING - FLARE AND CONTROL SPEED");
                height = 2;
                phase=Phase::rollout;sequence_time=0;
                message=type==Kind::landing?"WIRE CAUGHT - ROLLING TO REARM":"TOUCHDOWN - ROLLING TO REARM";
                return;
            }
            if (range < -260) return finish(Phase::bolter, "BOLTER - ADD POWER AND GO AROUND");
            if (speed < 58 && height < 20) return finish(Phase::failed, "TOO SLOW ON FINAL - ADD POWER");
        }
    }

    Kind type{};
    Phase phase{};
    double elapsed{}, x{}, height{}, range{}, speed{}, bank{}, pitch{}, sink{}, hold{}, fuel{};
    double sequence_time{};
    unsigned missiles=50,missile_maximum=100;
    bool native_continuation{};
    std::string_view message;
private:
    void finish(Phase next, std::string_view reason) { phase = next; message = reason; }
};
}

#pragma once

#include "engine/game_session.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace Frontend {
// Application-owned, ephemeral directed startup. The ROM owns stage setup;
// this target is only the retry choice and is never a progression/save value.
class NativePractice {
public:
    unsigned stage() const { return active_stage; }

    void start(GameSession& game, unsigned requested,
               std::array<uint8_t, 3> switches,
               const std::function<void()>& after_frame = {}) {
        // Reject an invalid candidate before clearing the current board,
        // pause state, extension playback or successful retry target.
        if (!requested || requested > game.stage_count())
            throw std::invalid_argument("Practice stage number out of range");
        game.restart();
        game.set_dip_switches(switches);
        const std::function<void()> completed = [&] {
            if (after_frame) after_frame();
            else {
                game.take_audio();
                game.take_music();
                game.take_feedback();
            }
        };
        // The established physical cold-boot coin/start sequence. Stage entry
        // then uses the same ROM event setup as the directed debug launcher.
        for (unsigned frame = 0; frame < 600; ++frame) {
            GameControls input;
            input.coin = frame >= 360 && frame < 372;
            input.start = frame >= 420 && frame < 432;
            if (!game.advance(input))
                throw std::runtime_error("Practice startup paused before stage entry");
            completed();
        }
        game.enter_practice_stage(requested, completed);
        active_stage = requested;
    }

    void arcade(GameSession& game, std::array<uint8_t, 3> switches) {
        game.restart();
        game.set_dip_switches(switches);
        active_stage = 0;
    }

private:
    unsigned active_stage{};
};
}

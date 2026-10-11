#pragma once

#include "engine/game_controls.hpp"
#include "engine/xboard_game.hpp"
#include "engine/native_stage_skip.hpp"
#include <functional>
#include <span>

// Application-facing boundary. The literal board remains the gameplay owner;
// extensions observe completed output or process a separate PCM output copy.
class GameSession {
public:
    static constexpr unsigned width = 320, height = 224, sample_rate = 48000;
    static constexpr double frame_seconds = XBoardTimeline::FRAME_NS / 1.0e9;

    struct FrameInfo { uint64_t number{}, time_ns{}; };
    struct FrameView {
        FrameInfo info;
        unsigned width{}, height{};
        std::span<const RGB888> pixels;
    };
    struct Extensions {
        // Views last only for the callback. Copy data needed asynchronously.
        std::function<void(FrameView)> frame_complete;
        // Interleaved signed 16-bit stereo, 48 kHz. Does not alter chip state.
        std::function<void(std::span<int16_t>, FrameInfo)> process_audio;
        // Clear extension-owned playback/history when starting a fresh board.
        std::function<void()> restarted;
    };

    explicit GameSession(Roms& rom, Extensions extensions = {})
        : game(rom), rom(rom), extensions(std::move(extensions)) {}

    // Opt-in debug startup, after the normal physical coin/start boot.
    void debug_skip_to_stage(unsigned stage) {
        NativeStageSkip::enter(game.board(), rom, stage, [&] {
            advance({}); take_audio();
        });
    }

    // One physical frame per advance, independent of display refresh. Pausing
    // admits no inputs and advances no CPU, audio device or gameplay timer.
    bool advance(const GameControls& controls) {
        if (paused_) return false;
        const auto physical = arcade_inputs(controls);
        XBoardGame::Host::Input input;
        input.buttons = physical.buttons;
        input.analog = physical.analog;
        game.tick(input);
        auto samples = game.take_audio();
        if (extensions.process_audio && !samples.empty()) extensions.process_audio(samples, info());
        if (audio.empty()) audio = std::move(samples);
        else audio.insert(audio.end(), samples.begin(), samples.end());
        if (extensions.frame_complete) extensions.frame_complete(frame());
        return true;
    }

    void set_paused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }
    void restart() {
        game.restart();
        audio.clear();
        paused_ = false;
        if (extensions.restarted) extensions.restarted();
    }
    FrameInfo info() const {
        const auto& board = game.board();
        return {board.frames, board.time()};
    }
    FrameView frame() const { return {info(), width, height, game.pixels()}; }
    std::vector<int16_t> take_audio() {
        auto result = std::move(audio);
        audio.clear();
        return result;
    }

private:
    XBoardGame game;
    Roms& rom;
    Extensions extensions;
    bool paused_ = false;
    std::vector<int16_t> audio;
};
